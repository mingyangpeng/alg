#include "include/Image2DAlg_api.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

using namespace cv;

static int g_initialized = 0;

const char* Image2DAlg_GetVersion(void) {
    return "1.0.0";
}

int Image2DAlg_Init(void) {
    if (g_initialized) return 0;
    g_initialized = 1;
    return 0;
}

void Image2DAlg_Cleanup(void) {
    g_initialized = 0;
}

int Image2DAlg_LoadImage(const char* path, Image2DAlg_Image* out_image) {
    (void)path; (void)out_image;
    return -1;
}

void Image2DAlg_FreeImage(Image2DAlg_Image* image) {
    if (image && image->data) {
        free(image->data);
        image->data = NULL;
    }
}

int Image2DAlg_SaveImage(const char* path, const Image2DAlg_Image* image) {
    (void)path; (void)image;
    return -1;
}

int Image2DAlg_ApplyFilter(const Image2DAlg_Image* src, Image2DAlg_Image* dst,
                           Image2DAlg_FilterType type, double param1, double param2) {
    (void)src; (void)dst; (void)type; (void)param1; (void)param2;
    return -1;
}

int Image2DAlg_DetectEdges(const Image2DAlg_Image* src, Image2DAlg_Image* dst,
                           double threshold1, double threshold2) {
    (void)src; (void)dst; (void)threshold1; (void)threshold2;
    return -1;
}

int Image2DAlg_DetectKeypoints(const Image2DAlg_Image* src,
                               Image2DAlg_KeyPoint** keypoints, int* count) {
    (void)src; (void)keypoints; (void)count;
    return -1;
}
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
