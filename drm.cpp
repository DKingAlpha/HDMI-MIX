#include "drm.hpp"

#include <iostream>
#include <unistd.h>
#include <string.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_fourcc.h>
#include <sys/mman.h>

bool DRMDevice::open_not_closing_on_failure() {
    // 1. Open the DRM device
    drm_fd = ::open(device.c_str(), O_RDWR);
    if (drm_fd < 0) {
        std::cerr << "Failed to open DRM device" << std::endl;
        return false;
    }

    // drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);

    drmGetCap(drm_fd, DRM_CAP_CURSOR_WIDTH, &cursor_width);
    drmGetCap(drm_fd, DRM_CAP_CURSOR_HEIGHT, &cursor_height);
    drmGetCap(drm_fd, DRM_CAP_DUMB_BUFFER, &support_dumb_buffer);

    resources = drmModeGetResources(drm_fd);
    if (!resources) {
        std::cerr << "Failed to get DRM resources" << std::endl;
        return false;
    }
    
    // Find a connected connector
    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(drm_fd, resources->connectors[i]);
        if (connector->connection == DRM_MODE_CONNECTED) {
            break;
        }
        connector = nullptr;
    }
    
    if (!connector) {
        std::cerr << "No connected connector found" << std::endl;
        return false;
    }
    
    conn_id = connector->connector_id;
    
    // Find encoder and CRTC
    crtc_id = 0;
    drmModeEncoder* encoder = drmModeGetEncoder(drm_fd, connector->encoder_id);
    if (encoder) {
        crtc_id = encoder->crtc_id;
        drmModeFreeEncoder(encoder);
    }
    

    drmModePlaneRes* plane_resources = drmModeGetPlaneResources(drm_fd);
    if (!plane_resources) {
        std::cerr << "Failed to get plane resources" << std::endl;
        return false;
    }

    plane_id_support_input_pixfmt = -1;
    for (int i = 0; i < plane_resources->count_planes; i++) {
        // printf("--------\n");
        
        drmModePlane* plane = drmModeGetPlane(drm_fd, plane_resources->planes[i]);
        // printf("Plane %d: possible CRTCs: 0x%x\n", plane->plane_id, plane->possible_crtcs);
        // printf("Formats: ");
        bool support_input_pixfmt = false;
        bool support_alpha = false;
        for (int j = 0; j < plane->count_formats; j++) {
            uint32_t c = plane->formats[j];
            // printf("%c%c%c%c ", c&0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, (c >> 24) & 0xFF);
            if (c == pixfmt) {
                support_input_pixfmt = true;
            }
            if (c == DRM_FORMAT_ARGB8888) {
                support_alpha = true;
            }
        }
        // printf("\n");
        drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
        // printf("Properties:\n");
        PlaneType plane_type = PlaneType::PLANE_TYPE_PRIMARY;

        std::map<std::string, uint32_t> cur_prop_ids;
        for (int j = 0; j < props->count_props; j++) {
            drmModePropertyPtr prop = drmModeGetProperty(drm_fd, props->props[j]);
            if (prop) {
                cur_prop_ids[prop->name] = prop->prop_id;
                // printf("  %s: ", prop->name);
                if (drm_property_type_is(prop, DRM_MODE_PROP_ENUM)) {
                    // printf("%s ", prop->enums[props->prop_values[j]].name);
                    if(strcmp(prop->name, "type") == 0) {
                        if (strcmp(prop->enums[props->prop_values[j]].name, "Overlay") == 0) {
                            plane_type = PlaneType::PLANE_TYPE_OVERLAY;
                        } else if (strcmp(prop->enums[props->prop_values[j]].name, "Cursor") == 0) {
                            plane_type = PlaneType::PLANE_TYPE_CURSOR;
                        } else if (strcmp(prop->enums[props->prop_values[j]].name, "Primary") == 0) {
                            plane_type = PlaneType::PLANE_TYPE_PRIMARY;
                        }
                    }
                } else if (drm_property_type_is(prop, DRM_MODE_PROP_BITMASK)) {
                    // printf("Bitmask");
                } else {
                    // printf("Value: %lu", props->prop_values[j]);
                }
                // printf("\n");
                drmModeFreeProperty(prop);
            }
        }
        panel_prop_ids[plane->plane_id] = cur_prop_ids;
        if (support_input_pixfmt && plane_id_support_input_pixfmt < 0 && plane_type != PlaneType::PLANE_TYPE_CURSOR) {
            plane_id_support_input_pixfmt = plane->plane_id;
            plane_type_support_input_pixfmt = plane_type;
        }
        if (support_alpha && plane_type == PlaneType::PLANE_TYPE_CURSOR && plane_id_cursor < 0) {
            plane_id_cursor = plane->plane_id;
        }
        
        drmModeFreePlane(plane);
    }
    drmModeFreePlaneResources(plane_resources);

    if (plane_id_support_input_pixfmt < 0) {
        std::cerr << "No suitable plane found for format " << pixfmt << std::endl;
        return false;
    }

    drmModeCrtcPtr crtc_info = drmModeGetCrtc(drm_fd, crtc_id);
    if (!crtc_info) {
        std::cerr << "Failed to get CRTC info" << std::endl;
        return false;
    }
    crtc_width = crtc_info->width;
    crtc_height = crtc_info->height;
    drmModeFreeCrtc(crtc_info);

    return true;

}

bool DRMDevice::close() {
    if (drm_fd < 0) {
        return false;
    }

    if (connector) {
        drmModeFreeConnector(connector);
        connector = nullptr;
    }
    
    if (resources) {
        drmModeFreeResources(resources);
        resources = nullptr;
    }

    while(!fb_ids.empty()) {
        drmModeRmFB(drm_fd, fb_ids.back());
        fb_ids.pop_back();
    }

    if (dumb_buf_handle) {
        drmModeDestroyDumbBuffer(drm_fd, dumb_buf_handle);
        dumb_buf_handle = 0;
    }

    if (dumb_buf_ptr) {
        munmap(dumb_buf_ptr, dumb_buf_size);
        dumb_buf_ptr = nullptr;
    }

    if (drm_fd >= 0) {
        drmClose(drm_fd);
    }
    drm_fd = -1;
    
    return true;
}

int DRMDevice::import_dmabuf(int dmabuf_fd) {
    if (pixfmt != DRM_FORMAT_NV12 && pixfmt != DRM_FORMAT_NV24) {
        std::cerr << "Unsupported pixel format: " << pixfmt << std::endl;
        return -1;
    }
    int ret;
    
    drmModeModeInfo* mode = nullptr;
	for (int i = 0; i < connector->count_modes; ++i) {
        mode = &connector->modes[i];
        if (mode->hdisplay == width && mode->vdisplay == height) {
            break;
        }
    }
    
    uint32_t bo_handle = 0;
    if (drmPrimeFDToHandle(drm_fd, dmabuf_fd, &bo_handle) < 0) {
        std::cerr << "Failed to import DMA buffer" << std::endl;
        return -1;
    }

    uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
    uint64_t modifiers[4] = {0};
    handles[0] = bo_handle;
    pitches[0] = width;  // Y stride
    offsets[0] = 0;
    modifiers[0] = DRM_FORMAT_MOD_LINEAR;
    // UV plane
    handles[1] = bo_handle;  // Same FD, different offset
    pitches[1] = width * (pixfmt == DRM_FORMAT_NV12 ? 1 : 2);       // UV stride
    offsets[1] = pitches[0] * height;  // UV data starts after Y plane
    modifiers[1] = DRM_FORMAT_MOD_LINEAR;

    uint32_t fb_id = 0;
    ret = drmModeAddFB2WithModifiers(drm_fd, width, height, 
                                    pixfmt, 
                                    handles, pitches, offsets, 
                                    modifiers, &fb_id, 
                                    DRM_MODE_FB_MODIFIERS);
    
    if (ret) {
        fprintf(stderr, "Failed to add FB: %s\n", strerror(-ret));
        return -1;
    }
    fb_ids.push_back(fb_id);

    printf("Successfully created FB with ID %u\n", fb_id);
    return fb_ids.size() - 1;
}

int DRMDevice::create_cursor_buf() {
    if (support_dumb_buffer == 0) {
        std::cerr << "Dumb buffer support not available" << std::endl;
        return -1;
    }

    if (cursor_width == 0 || cursor_height == 0) {
        std::cerr << "Cursor dimensions not available" << std::endl;
        return -1;
    }

    if (plane_id_cursor < 0) {
        std::cerr << "No cursor plane available" << std::endl;
        return -1;
    }

    uint32_t cursor_pitch = cursor_width * 4; // Assuming ARGB8888 format
    int ret = drmModeCreateDumbBuffer(drm_fd, cursor_width, cursor_height, 32, 0, &dumb_buf_handle, &cursor_pitch, &dumb_buf_size);
    if (ret) {
        std::cerr << "Failed to create dumb buffer: " << strerror(-ret) << std::endl;
        return -1;
    }

    uint64_t mmap_offset = 0;
    ret = drmModeMapDumbBuffer(drm_fd, dumb_buf_handle, &mmap_offset);
    if (ret) {
        std::cerr << "Failed to map dumb buffer: " << strerror(-ret) << std::endl;
        return -1;
    }

    dumb_buf_ptr = static_cast<unsigned char*>(mmap(nullptr, dumb_buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mmap_offset));
    if (dumb_buf_ptr == MAP_FAILED) {
        dumb_buf_ptr = nullptr;
        std::cerr << "Failed to mmap dumb buffer: " << strerror(errno) << std::endl;
        return -1;
    }
    memset(dumb_buf_ptr, 0xff, dumb_buf_size); // Clear the buffer

    uint32_t cursor_fb_id = 0;
    uint64_t cursor_modifiers = DRM_FORMAT_MOD_LINEAR;
    uint32_t fb2_offset = 0;


    uint32_t handles[4] = {dumb_buf_handle, 0, 0, 0};
    uint32_t pitches[4] = {cursor_pitch, 0, 0, 0};
    uint32_t offsets[4] = {fb2_offset, 0, 0, 0};
    uint64_t modifiers[4] = {cursor_modifiers, 0, 0, 0};
    ret = drmModeAddFB2WithModifiers(drm_fd, cursor_width, cursor_height,
                                         DRM_FORMAT_ARGB8888,
                                         handles, pitches, offsets,
                                         modifiers, &cursor_fb_id,
                                         DRM_MODE_FB_MODIFIERS);
    
    if (ret) {
        std::cerr << "Failed to create cursor framebuffer: " << strerror(-ret) << std::endl;
        return -1;
    }

    fb_ids.push_back(cursor_fb_id);

    return fb_ids.size() - 1;
}

bool DRMDevice::display(int index) {
    if (fb_ids.empty()) {
        std::cerr << "No framebuffer IDs available" << std::endl;
        return false;
    }

    /*
    // FIXME: 64x64 cursor plane still not working
    static int counter = 0;
    counter++;
    counter %= 1000;
    memset(dumb_buf_ptr, 0xff, dumb_buf_size); // Clear the buffer

    int ret_cursor = drmModeSetPlane(drm_fd, plane_id_cursor, crtc_id, fb_ids.back(), 0,
        counter, counter, cursor_width, cursor_height,
        0, 0, cursor_width << 16, cursor_height << 16);
    if (ret_cursor < 0 && ret_cursor != -EBUSY) {
        std::cerr << "Failed to set cursor plane: " << strerror(-ret_cursor) << std::endl;
        return false;
    }
    */

    /*
    int ret = drmModeSetPlane(drm_fd, plane_id_support_input_pixfmt, crtc_id, fb_ids[index], 0,
        0, 0, crtc_width, crtc_height,
        0, 0, width << 16, height << 16);
    */
    
    // legacy API above cause a few frame-drops a little bit, so use atomic API instead.
    
    drmModeAtomicReqPtr req = drmModeAtomicAlloc();
    if (!req) {
        std::cerr << "Failed to allocate atomic request" << std::endl;
        return false;
    }
    uint32_t plane_id = plane_id_support_input_pixfmt;
    drmModeAtomicAddProperty(req, plane_id, panel_prop_ids[plane_id]["CRTC_ID"], crtc_id);
    drmModeAtomicAddProperty(req, plane_id, panel_prop_ids[plane_id]["FB_ID"], fb_ids[index]);
    drmModeAtomicAddProperty(req, plane_id, panel_prop_ids[plane_id]["CRTC_X"], 0);
    drmModeAtomicAddProperty(req, plane_id, panel_prop_ids[plane_id]["CRTC_Y"], 0);
    drmModeAtomicAddProperty(req, plane_id, panel_prop_ids[plane_id]["CRTC_W"], crtc_width);
    drmModeAtomicAddProperty(req, plane_id, panel_prop_ids[plane_id]["CRTC_H"], crtc_height);
    drmModeAtomicAddProperty(req, plane_id, panel_prop_ids[plane_id]["SRC_X"], 0);
    drmModeAtomicAddProperty(req, plane_id, panel_prop_ids[plane_id]["SRC_Y"], 0);
    drmModeAtomicAddProperty(req, plane_id, panel_prop_ids[plane_id]["SRC_W"], width << 16);
    drmModeAtomicAddProperty(req, plane_id, panel_prop_ids[plane_id]["SRC_H"], height << 16);
    drmModeAtomicAddProperty(req, plane_id, panel_prop_ids[plane_id]["CRTC_VISIBLE"], 1);
    int ret = drmModeAtomicCommit(drm_fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_NONBLOCK, nullptr);
    drmModeAtomicFree(req);

    if (ret < 0 && ret != -EBUSY) {
        // a few EBUSY is normal.
        std::cerr << "Failed to commit atomic request: " << strerror(-ret) << std::endl;
        return false;
    }
    return true;
}