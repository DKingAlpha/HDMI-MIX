#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yolo11.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"

#include "imgui.h"

rknn_app_context_t rknn_app_ctx;

bool yolo_main_pre(const char *model_path, const char* label_list_file) {
    int ret;
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));

    init_post_process(label_list_file);

    ret = init_yolo11_model(model_path, &rknn_app_ctx);
    if (ret != 0)
    {
        printf("init_yolo11_model fail! ret=%d model_path=%s\n", ret, model_path);
        return false;
    }
    return true;
}

// imgfmt does not support NV24. so only NV12 works here.
bool yolo_main_on_frame(int v2ld_dma_fd, int width, int height, image_format_t imgfmt) {
    image_buffer_t src_image {
        .width = width,
        .height = height,
        .format = imgfmt,
        .fd = v2ld_dma_fd
        // other fields are automatically set by the framework
    };

    object_detect_result_list od_results;

    int ret = inference_yolo11_model(&rknn_app_ctx, &src_image, &od_results);
    if (ret != 0)
    {
        printf("init_yolo11_model fail! ret=%d\n", ret);
        return false;
    }

    // 画框和概率
    char text[256]{};
    ImDrawList* drawlist = ImGui::GetForegroundDrawList();
    for (int i = 0; i < od_results.count; i++)
    {
        object_detect_result *det_result = &(od_results.results[i]);

        const char* cls_name = coco_cls_to_name(det_result->cls_id);
        if (strcmp(cls_name, "person") != 0) {
            // skip is not person
            continue;
        }

        if (strcmp(cls_name, "tv") == 0 || strcmp(cls_name, "laptop") == 0
            || strcmp(cls_name, "refrigerator") == 0 || strcmp(cls_name, "teddy bear") == 0 ) {
            continue;
        }
        /*
        printf("%s @ (%d %d %d %d) %.3f\n", cls_name,
               det_result->box.left, det_result->box.top,
               det_result->box.right, det_result->box.bottom,
               det_result->prop);
        */

        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        int x2 = det_result->box.right;
        int y2 = det_result->box.bottom;

        sprintf(text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
        drawlist->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0, 255, 0, 255), 0.0f, ImDrawFlags_RoundCornersAll, 3.0f);
        drawlist->AddText(nullptr, 128, ImVec2(x1, y1 - 128), IM_COL32(255, 0, 0, 255), text);
    }

    return true;
}

void yolo_main_post() {
    deinit_post_process();

    int ret = release_yolo11_model(&rknn_app_ctx);
    if (ret != 0)
    {
        printf("release_yolo11_model fail! ret=%d\n", ret);
    }
}
