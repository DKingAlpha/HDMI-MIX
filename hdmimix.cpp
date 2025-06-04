#include "v4l2.hpp"
#include "drm.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <memory.h>
#include <sys/mman.h>
#include <signal.h>

class TwoDimensionalBuffer {
public:
    TwoDimensionalBuffer(unsigned char* data, size_t size, int width, int height) 
        : data(data), size(size), width(width), height(height) {
    }
    
    unsigned char* get(int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) {
            return nullptr; // Out of bounds
        }
        return data + (y * width + x) * 4; // Assuming 4 bytes per pixel (ARGB)
    }
private:
    int width;
    int height;
    unsigned char* data;
    size_t size;
};

class FreqMonitor {
    std::string name;
public:
    int interval_ms;
    uint64_t count = 0;
    uint64_t last_print_time = 0;
    FreqMonitor(std::string name, int interval_ms = 5000): name(name), interval_ms(interval_ms) {
        last_print_time = get_current_time();
    }
    void increment() {
        count++;
        uint64_t current_time = get_current_time();
        if (current_time - last_print_time >= interval_ms) {
            float freq = count * 1000.0 / (current_time - last_print_time);
            printf("Frequency: %s %.2fHz\n", name.c_str(), freq);
            count = 0;
            last_print_time = current_time;
        }
    }
    uint64_t get_current_time() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000 + ts.tv_nsec / 1000000; // milliseconds
    }
};

void debug_on_v4l2_data(V4l2Device::user_buffers_t& buf, v4l2_buffer& vbuf) {
    std::cout << "Buffer index: " << buf.index << std::endl;
    for (size_t i = 0; i < buf.num_planes(); ++i) {
        auto& mem = buf.mem[i];
        std::cout << "  Plane " << i << ": size=" << mem.size 
                    << ", DMA FD=" << mem.dma_fd 
                    << ", Pointer=" << static_cast<void*>(mem.ptr) << std::endl;
        if (mem.ptr) {
            printf("    ");
            print_hex(mem.ptr, 16);
        }
    }
};

bool caught_interruption = false;
void signal_handler(int signum) {
    if (signum == SIGINT) {
        caught_interruption = true;
        std::cout << "Caught signal " << signum << ", exiting..." << std::endl;
    }
}

int main(int argc, char** argv) {
    struct sigaction sigact;
    sigact.sa_handler = signal_handler;
    
    sigaction(SIGINT, &sigact, nullptr);
    V4l2Device v4l2_device("/dev/video0", 4);
    if (!v4l2_device.is_open()) {
        std::cerr << "Failed to open video device" << std::endl;
        return 1;
    }

    // v4l2_device.on_data = debug_on_v4l2_data;
    
    v4l2_device.stream_on();

    DRMDevice drm_device("/dev/dri/card0", v4l2_device.width, v4l2_device.height, v4l2_device.pixfmt);
    for (int i=0; i<v4l2_device.buf_count; i++) {
        int index = drm_device.import_dmabuf(v4l2_device.buffers[i].mem[0].dma_fd);
    }

    if (drm_device.create_canvas_buf() < 0) {
        std::cerr << "Failed to create cursor buffer" << std::endl;
        return 1;
    }

    v4l2_device.on_data = [&drm_device](V4l2Device::user_buffers_t& buf, v4l2_buffer& vbuf) {
        // For each buffer, import the DMABUF and display it
        drm_device.display(buf.index);

        static FreqMonitor freq_monitor("Main");
        freq_monitor.increment();

        TwoDimensionalBuffer buf2d (drm_device.dumb_buf_ptr, drm_device.dumb_buf_size, drm_device.width, drm_device.height);
        memset(drm_device.dumb_buf_ptr, 0x00, drm_device.dumb_buf_size); // make transparent
        // draw a pattern
        static int count = 0;
        count ++;
        count %= 640;
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                unsigned char* pixel = buf2d.get(x+count, y+count);
                if (pixel) {
                    pixel[0] = count; // R
                    pixel[1] = count * 2; // G
                    pixel[2] = count * 3; // B
                    pixel[3] = 0xFF;  // A
                }
            }
        }
    };

    while(!caught_interruption) {
        usleep(10000); // 10ms
    }

    v4l2_device.on_data = nullptr;
    v4l2_device.stream_off();

    drm_device.close();
    v4l2_device.close();

    sleep(1); // dirty: give some time for deamon threads to finish

    return 0;
}
