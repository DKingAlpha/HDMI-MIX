#pragma once

#include <stdint.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <map>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

enum class PlaneType {
    PLANE_TYPE_PRIMARY,
    PLANE_TYPE_OVERLAY,
    PLANE_TYPE_CURSOR,
};

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
    int import_dmabuf(int index, int dmabuf_fd);

    uint32_t import_canvas_buf_bo(gbm_bo* bo);
    uint32_t create_canvas_buf_dumb();

    enum class PlaneType {
        PLANE_TYPE_OVERLAY,
        PLANE_TYPE_CURSOR,
        PLANE_TYPE_PRIMARY
    };

    bool display(int index, uint32_t canvas_fb_id);

    int width = 0;
    int height = 0;
    int pixfmt = 0;

    std::string device;

    unsigned char* dumb_buf_ptr = nullptr;
    uint64_t dumb_buf_size = 0;

    int drm_fd = -1;
private:
    bool open_not_closing_on_failure();

    uint32_t conn_id = 0;
    uint32_t crtc_id = 0;

    int plane_id_support_input_pixfmt = -1;
    PlaneType plane_type_support_input_pixfmt = PlaneType::PLANE_TYPE_PRIMARY;

    int plane_id_canvas = -1;
    int pixfmt_cursor = 0;
    uint32_t dumb_buf_handle = 0;

    drmModeRes* resources = nullptr;
    drmModeConnector* connector = nullptr;

    std::vector<uint32_t> passthrough_fd_ids;
    std::map<gbm_bo*, uint32_t> canvas_fb_ids;
    std::map<uint32_t, std::map<std::string, uint32_t>> panel_prop_ids;

    // int crtc_width = 0;
    // int crtc_height = 0;

    uint64_t support_dumb_buffer = 0;

    int cur_passthrough_fd_index = -1;
    uint32_t cur_canvas_fb_id = 0;
};
