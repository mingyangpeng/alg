#include "include/Image2DAlg_api.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <stdlib.h>
#include <string.h>

using namespace cv;

static int g_initialized = 0;

// 返回库版本号（静态字符串，与版本历史/check_version_sync 五源门禁锁定）
const char* Image2DAlg_GetVersion(void) {
    return "2.8.0";
}

// 初始化占位：仅维护无功能影响的标志（幂等，恒 0）
int Image2DAlg_Init(void) {
    if (g_initialized) return 0;
    g_initialized = 1;
    return 0;
}

// 反初始化占位：清标志（与 Init 配对，无实际副作用）
void Image2DAlg_Cleanup(void) {
    g_initialized = 0;
}

// 释放图像缓冲（free + data 置 NULL；NULL/空 data 安全）
void Image2DAlg_FreeImage(Image2DAlg_Image* image) {
    if (image && image->data) {
        free(image->data);
        image->data = NULL;
    }
}

// 图像文件读取：固定输出 BGRA 4 通道（宿主契约）；-3 = 源图通道数不支持
int Image2DAlg_ReadImage(const char* img_path, Image2DAlg_Image* out_image) {
    if (!img_path || !out_image) return -1;
    Mat img = imread(img_path, IMREAD_UNCHANGED);
    if (img.empty()) return -2;
    Mat bgra;
    if (img.channels() == 4) {
        bgra = img;
        if (!bgra.isContinuous()) {
            bgra = bgra.clone();
        }
    } else if (img.channels() == 3) {
        cvtColor(img, bgra, COLOR_BGR2BGRA);
    } else if (img.channels() == 1) {
        cvtColor(img, bgra, COLOR_GRAY2BGRA);
    } else {
        return -3;
    }
    if (!bgra.isContinuous()) {
        bgra = bgra.clone();
    }
    out_image->width = bgra.cols;
    out_image->height = bgra.rows;
    out_image->channels = 4;
    size_t sz = (size_t)bgra.cols * (size_t)bgra.rows * 4;
    out_image->data = (unsigned char*)malloc(sz);
    if (!out_image->data) return -4;
    memcpy(out_image->data, bgra.data, sz);
    return 0;
}
