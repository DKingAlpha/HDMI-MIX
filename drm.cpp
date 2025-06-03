#include "drm.hpp"

#include <iostream>
#include <unistd.h>
#include <string.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_fourcc.h>

bool DRMDevice::open_not_closing_on_failure() {
    // 1. Open the DRM device
    drm_fd = ::open(device.c_str(), O_RDWR);
    if (drm_fd < 0) {
        std::cerr << "Failed to open DRM device" << std::endl;
        return false;
    }

    // drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

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
    crtc_id = connector->encoder_id;
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

    plane_id_support_fmt = -1;
    for (int i = 0; i < plane_resources->count_planes; i++) {
        // printf("--------\n");
        
        drmModePlane* plane = drmModeGetPlane(drm_fd, plane_resources->planes[i]);
        // printf("Plane %d: possible CRTCs: 0x%x\n", plane->plane_id, plane->possible_crtcs);
        // printf("Formats: ");
        bool supported_nv24 = false;
        for (int j = 0; j < plane->count_formats; j++) {
            /*
            printf("%c%c%c%c ", 
                   (plane->formats[j] & 0xFF), 
                   (plane->formats[j] >> 8) & 0xFF, 
                   (plane->formats[j] >> 16) & 0xFF, 
                   (plane->formats[j] >> 24) & 0xFF);
            */
            if (plane->formats[j] == pixfmt) {
                supported_nv24 = true;
            }
        }
        // printf("\n");
        drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(drm_fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
        // printf("Properties:\n");
        bool is_overlay = false;

        std::map<std::string, uint32_t> cur_prop_ids;
        for (int j = 0; j < props->count_props; j++) {
            drmModePropertyPtr prop = drmModeGetProperty(drm_fd, props->props[j]);
            if (prop) {
                cur_prop_ids[prop->name] = prop->prop_id;
                // printf("  %s: ", prop->name);
                if (drm_property_type_is(prop, DRM_MODE_PROP_ENUM)) {
                    // printf("%s ", prop->enums[props->prop_values[j]].name);
                    if(strcmp(prop->name, "type") == 0 && strcmp(prop->enums[props->prop_values[j]].name, "Overlay") == 0) {
                        is_overlay = true;
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
        if (supported_nv24 && is_overlay) {
            plane_id_support_fmt = plane->plane_id;
        }
        drmModeFreePlane(plane);
    }
    drmModeFreePlaneResources(plane_resources);

    if (plane_id_support_fmt < 0) {
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

bool DRMDevice::display(int index) {
    if (fb_ids.empty()) {
        std::cerr << "No framebuffer IDs available" << std::endl;
        return false;
    }

    int ret = drmModeSetPlane(drm_fd, plane_id_support_fmt, crtc_id, fb_ids[index], 0,
        0, 0, crtc_width, crtc_height,
        0, 0, width << 16, height << 16);
    if (ret < 0) {
        std::cerr << "Failed to set plane, ret:" << ret << std::endl;
        return false;
    }
    return true;
}