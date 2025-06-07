#pragma once

#include <string>
#include <vector>
#include <functional>
#include <linux/videodev2.h>

class V4l2Device {
public:
    V4l2Device(const std::string device, int buf_count);
    ~V4l2Device() {
        stream_off();
        close();
    }

    struct user_buf_info_t {
        user_buf_info_t() : ptr(nullptr), size(0), dma_fd(-1) {}

        unsigned char* ptr;
        size_t size;
        int dma_fd;
    };
    struct user_buffers_t {
        user_buffers_t(int index, size_t num_planes)
            : index(index), mem(num_planes) {}
        
        int index;
        std::vector<user_buf_info_t> mem;

        size_t num_planes() const { return mem.size(); }
    };

    /**
     * set up buffers, map to userspace, export DMABUF fd
     */
    bool open();
    bool close();

    bool stream_on(bool& run_loop, std::function<void(user_buffers_t&, v4l2_buffer&)> on_data);
    bool stream_off();

    bool is_open() const { return v4l2_fd >= 0; }
    
    // public
    std::string device;
    int v4l2_fd;
    bool is_mplane;
    std::vector<user_buffers_t> buffers;
    int buf_count;

    int pixfmt;
    int width;
    int height;

private:
    bool open_not_closing_on_failure();

    bool is_streaming;
};

inline int print_hex(void* ptr, size_t size, size_t line_size = 16) {
    unsigned char* memview = static_cast<unsigned char*>(ptr);
    for (size_t i = 0; i < size; i++) {
        printf("%02x ", memview[i]);
        if ((i + 1) % line_size == 0) {
            printf("\n");
        }
    }
    if (size % line_size != 0) {
        printf("\n");
    }
    return 0;
}
