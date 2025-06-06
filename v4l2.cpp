#include "v4l2.hpp"

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <memory.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <libv4l2.h>
#include <linux/videodev2.h>
#include <thread>

#include <vector>
#include <array>
#include <string>


bool V4l2Device::open_not_closing_on_failure() {
    v4l2_fd = ::open(device.c_str(), O_RDWR);
    if (v4l2_fd < 0) {
        std::cerr << "Failed to open video device" << std::endl;
        return false;
    }
    v4l2_capability cap{};
    if (ioctl(v4l2_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "Failed to query video capabilities" << std::endl;
        return false;
    }
    is_mplane = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
    if (!is_mplane) {
        std::cerr << "This tool only supports multi-plane (mplane) devices" << std::endl;
        return false;
    }
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(v4l2_fd, VIDIOC_G_FMT, &fmt) < 0) {
        std::cerr << "Failed to get video format" << std::endl;
        return false;
    }
    std::cout << "Input video format: " << fmt.fmt.pix_mp.width << "x" 
              << fmt.fmt.pix_mp.height << ", pixel format: "
              << (char)(fmt.fmt.pix_mp.pixelformat & 0xFF) 
              << (char)((fmt.fmt.pix_mp.pixelformat >> 8) & 0xFF)
              << (char)((fmt.fmt.pix_mp.pixelformat >> 16) & 0xFF)
              << (char)((fmt.fmt.pix_mp.pixelformat >> 24) & 0xFF) << std::endl;
    
    pixfmt = fmt.fmt.pix_mp.pixelformat;
    width = fmt.fmt.pix_mp.width;
    height = fmt.fmt.pix_mp.height;
    // request mmap buffers, then map them to userspace and export DMABUF fds
    v4l2_requestbuffers reqbuf{};
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = buf_count;

    if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        std::cerr << "Failed to request buffer" << std::endl;
        return false;
    }

    if (reqbuf.count < 1) {
        std::cerr << "No buffers available" << std::endl;
        return false;
    }
    buf_count = reqbuf.count;   // driver can have a minimum value defined.

    int n_planes = fmt.fmt.pix_mp.num_planes;

    bool buf_err = false;
    for (int i=0; i<buf_count && !buf_err; i++) {
        v4l2_buffer vbuf{};
        std::vector<v4l2_plane> planes(n_planes);
        vbuf.type = reqbuf.type;
        vbuf.index = i;
        // for mplane
        vbuf.length = n_planes;
        vbuf.m.planes = planes.data();
        if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &vbuf) < 0) {
            std::cerr << "Failed to query buffer" << std::endl;
            return false;
        }

        user_buffers_t buf(i, n_planes);
        for (int j = 0; j < n_planes && !buf_err; ++j) {
            auto &mem = buf.mem[j];

            // length
            auto &plane = vbuf.m.planes[j];
            mem.size = plane.length;

            // mmap
            void* userptr = ::mmap(nullptr, plane.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, plane.m.mem_offset);
            if (userptr == MAP_FAILED) {
                printf("Failed to mmap buffer %d, plane %d, offset: %d: %s\n", i, j, plane.m.mem_offset, strerror(errno));
                mem.ptr = nullptr;
                buf_err = true;
            } else {
                mem.ptr = static_cast<unsigned char*>(userptr);
            }

            // export DMABUF fd for each plane
            struct v4l2_exportbuffer expbuf{};
            expbuf.type = reqbuf.type;
            expbuf.index = i;
            expbuf.plane = j;
            if (ioctl(v4l2_fd, VIDIOC_EXPBUF, &expbuf) == -1) {
                printf("Failed to export buffer %d, plane %d: %s\n", i, j, strerror(errno));
                mem.dma_fd = -1;
                buf_err = true;
            } else {
                mem.dma_fd = expbuf.fd;
            }
        }
        buffers.push_back(buf);

        // qbuf
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &vbuf) < 0) {
            std::cerr << "Failed to queue buffer" << std::endl;
            buf_err = true;
        }
    }

    return true;
}

V4l2Device::V4l2Device(const std::string device, int buf_count) : device(device), buf_count(buf_count), v4l2_fd(-1), is_mplane(false), is_streaming(false) {
    open();
};

bool V4l2Device::open() {
  if (!open_not_closing_on_failure()) {
    close();
    return false;
  } else {
    return true;
  }
}

bool V4l2Device::close() {
  if (!is_open()) {
    return false;
  }
  for (auto &buf : buffers) {
    for (auto &mem : buf.mem) {
      if (mem.dma_fd >= 0) {
        ::close(mem.dma_fd);
        mem.dma_fd = -1;
      }
      if (mem.ptr) {
        ::munmap(mem.ptr, mem.size);
        mem.ptr = nullptr;
      }
    }
  }
  ::close(v4l2_fd);
  v4l2_fd = -1;
  return true;
}

bool V4l2Device::stream_on(bool& run_loop, std::function<void(user_buffers_t&, v4l2_buffer&)> on_data) { 
    if (is_streaming) {
        return true;
    }
    v4l2_buf_type type = is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(v4l2_fd, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "Failed to start streaming" << std::endl;
        return false;
    }

    is_streaming = true;
    while(run_loop && this->is_open()) {
        // process data
        v4l2_buffer vbuf{};
        vbuf.type = is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vbuf.memory = V4L2_MEMORY_MMAP;
        std::vector<v4l2_plane> planes(buffers[0].num_planes());
        vbuf.m.planes = planes.data();
        vbuf.length = buffers[0].num_planes();
        // this will block until a buffer is available
        if(ioctl(v4l2_fd, VIDIOC_DQBUF, &vbuf)) {
            if (!is_streaming) {
                break;
            }
            std::cerr << "Failed to dequeue buffer: " << strerror(errno) << std::endl;
            continue;
        }
        if (on_data) {
            on_data(buffers[vbuf.index], vbuf);
        }
        // queue the buffer back
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &vbuf)) {
            std::cerr << "Failed to queue buffer: " << strerror(errno) << std::endl;
            continue;
        }
    }
    return true;
}

bool V4l2Device::stream_off() {
    if (!is_streaming) {
        return true;
    }
    is_streaming = false;

    v4l2_buf_type type = is_mplane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type) < 0) {
        std::cerr << "Failed to stop streaming" << std::endl;
        return false;
    }
    return true;
}
