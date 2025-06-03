#pragma once

#include <stdint.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <map>
#include <xf86drmMode.h>

class DRMDevice {
public:
    DRMDevice(std::string device, int width, int height, int pixfmt) : device(device), width(width), height(height), pixfmt(pixfmt) {
        open();
    }

    ~DRMDevice() {
        close();
    }

    bool open() {
        if (!open_not_closing_on_failure()) {
            close();
            return false;
        }
        return true;
    }
    bool close();
    
    // Return the index of the framebuffer
    int import_dmabuf(int dmabuf_fd);

    bool display(int index);


    int width = 0;
    int height = 0;
    int pixfmt = 0;

    std::string device;

private:
    bool open_not_closing_on_failure();

    int drm_fd = -1;
    uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
    uint64_t modifiers[4] = {0};
    uint32_t conn_id = 0;
    uint32_t crtc_id = 0;

    int plane_id_support_fmt = -1;

    drmModeRes* resources = nullptr;
    drmModeConnector* connector = nullptr;

    std::vector<uint32_t> fb_ids;
    std::map<uint32_t, std::map<std::string, uint32_t>> panel_prop_ids;

    int crtc_width = 0;
    int crtc_height = 0;
};
