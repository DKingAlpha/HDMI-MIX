#include "v4l2.hpp"
#include "drm.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <memory.h>
#include <sys/mman.h>
#include <signal.h>

#include "egl_renderer.hpp"
#include "helper.hpp"
#include <EGL/egl.h>
#include <gbm.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>

#include "common.h"

#include <mutex>
#include <condition_variable>

class WaitSignal {
public:
    WaitSignal() : signaled_(false) {}
    
    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return signaled_; });
        signaled_ = false;
    }
    
    void signal() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            signaled_ = true;
        }
        cv_.notify_one();
    }
    
    void broadcast() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            signaled_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool signaled_;
};

void dump_config(EGLDisplay egl_display, EGLConfig config);
void test_draw();
void test_draw_dumb(DRMDevice& drm_device);

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

bool run_loop = true;
void signal_handler(int signum) {
    if (signum == SIGINT) {
        run_loop = false;
        std::cout << "Caught signal " << signum << ", exiting..." << std::endl;
    }
}

extern void imgui_main_pre(int width, int height);
extern void imgui_main_post();
extern void imgui_main_begin_frame();
extern void imgui_main_end_frame();

extern bool yolo_main_pre(const char *model_path, const char* label_list_file);
extern bool yolo_main_on_frame(int v2ld_dma_fd, int width, int height, image_format_t imgfmt);
extern void yolo_main_post();

int main(int argc, char** argv) {
    yolo_main_pre("./model/yolo11.rknn", "./model/coco_80_labels_list.txt");

    struct sigaction sigact;
    sigact.sa_handler = signal_handler;
    
    sigaction(SIGINT, &sigact, nullptr);
    // single buffer can cause screen tearing, because drm may be reading dirty buffer
    // so we use multiple buffers
    V4l2Device v4l2_device("/dev/video0", 4);
    if (!v4l2_device.is_open()) {
        std::cerr << "Failed to open video device" << std::endl;
        return 1;
    }

    DRMDevice drm_device("/dev/dri/card0", v4l2_device.width, v4l2_device.height, v4l2_device.pixfmt);
    for (int i=0; i<v4l2_device.buf_count; i++) {
        drm_device.import_dmabuf(v4l2_device.buffers[i].index, v4l2_device.buffers[i].mem[0].dma_fd);
    }

    // if (drm_device.create_canvas_buf_dumb() < 0) {
    //     std::cerr << "Failed to create cursor buffer" << std::endl;
    //     return 1;
    // }

    EGLBufRenderer renderer(drm_device.drm_fd, v4l2_device.width, v4l2_device.height);
    if(!renderer.initialize()) {
        return 1;
    }

    WaitSignal ws_release;
    uint32_t canvas_fb_id = 0;


    if (v4l2_device.pixfmt == V4L2_PIX_FMT_NV24) {
        printf("NV24 is not supported, manually transcode to NV12 first\n");
    }

    int last_dma_index = -1;
    std::thread render_th([&drm_device, &renderer, &v4l2_device, &ws_release, &canvas_fb_id, &last_dma_index]() {
        if (!renderer.bind_context_to_thread()) {
            std::cerr << "Failed to bind EGL context to thread" << std::endl;
            run_loop = false;
            return;
        }
        imgui_main_pre(v4l2_device.width, v4l2_device.height);

        while (run_loop) {
            static FreqMonitor freq_monitor("IMGUI");
            freq_monitor.increment();

            imgui_main_begin_frame();
            if (last_dma_index >= 0 && v4l2_device.pixfmt == V4L2_PIX_FMT_NV12) {
                yolo_main_on_frame(v4l2_device.buffers[last_dma_index].mem[0].dma_fd, v4l2_device.width, v4l2_device.height, IMAGE_FORMAT_YUV420SP_NV12);
            }
            imgui_main_end_frame();
            renderer.swap_buffer();
            gbm_bo* cur_bo = renderer.read_lock();

            // create framebuffer from the bo
            canvas_fb_id = drm_device.import_canvas_buf_bo(cur_bo);

            ws_release.wait();
            renderer.read_unlock(cur_bo); // unlock the bo for the next frame
        }
    });

    sleep(1); // dirty: wait for renderer to get ready

    v4l2_device.stream_on(run_loop, [&drm_device, &renderer, &ws_release, &canvas_fb_id, &last_dma_index]
        (V4l2Device::user_buffers_t& buf, v4l2_buffer& vbuf) {
        static FrameJitterMeasurer jitterMeasurer(60.0, 60);
        jitterMeasurer.markFrame();
        jitterMeasurer.print();

        last_dma_index = buf.index;
        drm_device.display(buf.index, canvas_fb_id);

        drmVBlank vbl = {};
        vbl.request.type = (drmVBlankSeqType)DRM_VBLANK_RELATIVE;
        vbl.request.sequence = 1; // wait for the next vblank
        if(int ret = drmWaitVBlank(drm_device.drm_fd, &vbl); ret) {
            std::cerr << "Failed to wait for vblank: " << strerror(-ret) << std::endl;
        }
        ws_release.signal();
    });

    render_th.join();

    imgui_main_post();

    v4l2_device.stream_off();
    usleep(100*1000); // 100ms to ensure all buffers are processed

    renderer.close();
    usleep(100*1000);

    drm_device.close();
    usleep(100*1000);

    v4l2_device.close();
    usleep(100*1000);
    
    yolo_main_post();

    return 0;
}


void dump_config(EGLDisplay egl_display, EGLConfig config) {
    // dump attributes of each config
    EGLint attribs[16]{};
    eglGetConfigAttrib(egl_display, config, EGL_RED_SIZE, &attribs[0]);
    eglGetConfigAttrib(egl_display, config, EGL_GREEN_SIZE, &attribs[1]);
    eglGetConfigAttrib(egl_display, config, EGL_BLUE_SIZE, &attribs[2]);
    eglGetConfigAttrib(egl_display, config, EGL_ALPHA_SIZE, &attribs[3]);

    eglGetConfigAttrib(egl_display, config, EGL_BUFFER_SIZE, &attribs[4]);
    eglGetConfigAttrib(egl_display, config, EGL_DEPTH_SIZE, &attribs[5]);


    eglGetConfigAttrib(egl_display, config, EGL_CONFIG_ID, &attribs[6]);
    eglGetConfigAttrib(egl_display, config, EGL_LEVEL, &attribs[7]);
    eglGetConfigAttrib(egl_display, config, EGL_RENDERABLE_TYPE, &attribs[8]);
    eglGetConfigAttrib(egl_display, config, EGL_SURFACE_TYPE, &attribs[9]);

    eglGetConfigAttrib(egl_display, config, EGL_NATIVE_RENDERABLE, &attribs[10]);
    eglGetConfigAttrib(egl_display, config, EGL_NATIVE_VISUAL_ID, &attribs[11]);
    eglGetConfigAttrib(egl_display, config, EGL_NATIVE_VISUAL_TYPE, &attribs[12]);

    char visual_id[5]{};
    memcpy(visual_id, &attribs[11], 4);
    visual_id[4] = '\0';

    std::cout
        << "Config ID: " << attribs[6] << ", "
        << "RGBA: " << attribs[0] << ":"
        << attribs[1] << ":"
        << attribs[2] << ":"
        << attribs[3] << ", "
        << "Buffer: " << attribs[4] << ", "
        << "Depth: " << attribs[5] << ", "
        << "Level: " << attribs[7] << ", "
        << "Renderable Type: " << std::hex << attribs[8] << ", "
        << "Surface Type: " << std::hex << attribs[9] << ", "
        // << "Native Renderable: " << attribs[10] << ", "
        << "Visual ID: " << visual_id << ", "
        << "Visual Type: " <<  std::hex << attribs[12]
        << std::endl;
}


void test_draw() {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float vertices[] = {
        -0.5f, -0.5f,
        0.5f, -0.5f,
        0.0f,  0.5f
    };
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glColor4f(0.0f, 1.0f, 0.0f, 1.0f); // 绿色
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableClientState(GL_VERTEX_ARRAY);
    glFlush();

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "OpenGL error: " << err << std::endl;
    }
}

void test_draw_dumb(DRMDevice& drm_device) {
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
}