#pragma once

#include <iostream>
#include <EGL/egl.h>
#include <gbm.h>

class EGLBufRenderer {
public:
    EGLBufRenderer(int drm_fd, int width, int height) 
        : drm_fd(drm_fd), width(width), height(height), initialized(false){}
    
    ~EGLBufRenderer() {
        if (egl_display != EGL_NO_DISPLAY && egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(egl_display, egl_context);
            egl_context = EGL_NO_CONTEXT;
        }
        if (egl_display != EGL_NO_DISPLAY && egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(egl_display, egl_surface);
            egl_surface = EGL_NO_SURFACE;
        }
        if (egl_display != EGL_NO_DISPLAY) {
            eglTerminate(egl_display);
            egl_display = EGL_NO_DISPLAY;
        }
        if (gbm_surface != nullptr) {
            gbm_surface_destroy(gbm_surface);
            gbm_surface = nullptr;
        }
        if (gbm_device != nullptr) {
            gbm_device_destroy(gbm_device);
            gbm_device = nullptr;
        }
        initialized = false;
    }
    
    bool initialize() {
        gbm_device = gbm_create_device(drm_fd);
        if (!gbm_device) {
            std::cerr << "Failed to create GBM device" << std::endl;
            return false;
        }
        
        gbm_surface = gbm_surface_create(gbm_device, width, height, GBM_BO_FORMAT_ARGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
        if (!gbm_surface) {
            std::cerr << "Failed to create GBM surface" << std::endl;
            return false;
        }

        egl_display = eglGetDisplay(gbm_device);
        if (egl_display == EGL_NO_DISPLAY) {
            std::cerr << "Failed to get EGL display" << std::endl;
            return false;
        }
        EGLint major, minor;
        if (!eglInitialize(egl_display, &major, &minor)) {
            std::cerr << "Failed to initialize EGL" << std::endl;
            return false;
        }
        printf("major: %d, minor: %d\n", major, minor);

        EGLint num_configs;
        /*
        std::vector<EGLConfig> configs(256, nullptr);
        eglGetConfigs(egl_display, configs.data(), configs.size(), &num_configs);
        for (int i = 0; i < num_configs; ++i) {
            dump_config(egl_display, configs[i]);
        }
        */

        EGLint attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_BUFFER_SIZE, 32,
            EGL_NATIVE_RENDERABLE, EGL_TRUE,
            EGL_NATIVE_VISUAL_ID, GBM_FORMAT_ARGB8888,
            EGL_NONE
        };
        EGLConfig config = nullptr;
        if (!eglChooseConfig(egl_display, attribs, &config, 1, &num_configs) || num_configs < 1) {
            std::cerr << "Failed to choose EGL config" << std::endl;
            return false;
        }
        printf("find %d configs\n", num_configs);

        egl_surface = eglCreateWindowSurface(egl_display, config, (EGLNativeWindowType)gbm_surface, nullptr);
        if (egl_surface == EGL_NO_SURFACE) {
            std::cerr << "Failed to create EGL surface, err:" << std::hex << eglGetError() << std::endl;
            return false;
        }
        egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, nullptr);
        if (egl_context == EGL_NO_CONTEXT) {
            std::cerr << "Failed to create EGL context, err:" << std::hex << eglGetError() << std::endl;
            return false;
        }
        initialized = true;
        return true;
    }

    bool bind_context_to_thread() {
        if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
            std::cerr << "Failed to make EGL context current" << std::endl;
            return false;
        }
        return true;
    }

    bool prepare_read() {
		if (!eglSwapBuffers(egl_display, egl_surface)) {
			std::cerr << "Failed to swap buffers, err: " << std::hex << eglGetError() << std::endl;
			return false;
		}
		return true;
    }

    struct gbm_bo* read_lock() {
        return gbm_surface_lock_front_buffer(gbm_surface);
    }

    void read_unlock(gbm_bo* bo) {
        gbm_surface_release_buffer(gbm_surface, bo);
    }

    bool initialized;
    
    struct gbm_surface* gbm_surface = nullptr;

private:
    int width;
    int height;
    int drm_fd;

    struct gbm_device* gbm_device = nullptr;
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLContext egl_context = EGL_NO_CONTEXT;
};
