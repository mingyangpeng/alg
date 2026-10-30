#ifndef IMAGE2DALG_API_H
#define IMAGE2DALG_API_H

#include "Image2DAlg_export.h"

#ifdef __cplusplus
extern "C" {
#endif

// 版本信息
IMAGE2DALG_EXPORT const char* Image2DAlg_GetVersion(void);

// 初始化/清理
IMAGE2DALG_EXPORT int Image2DAlg_Init(void);
IMAGE2DALG_EXPORT void Image2DAlg_Cleanup(void);

// 图像处理接口
typedef struct Image2DAlg_Image {
    int width;
    int height;
    int channels;
    unsigned char* data;
} Image2DAlg_Image;

IMAGE2DALG_EXPORT int Image2DAlg_LoadImage(const char* path, Image2DAlg_Image* out_image);
IMAGE2DALG_EXPORT void Image2DAlg_FreeImage(Image2DAlg_Image* image);
IMAGE2DALG_EXPORT int Image2DAlg_SaveImage(const char* path, const Image2DAlg_Image* image);

// 滤波处理
typedef enum {
    IMAGE2DALG_FILTER_GAUSSIAN = 0,
    IMAGE2DALG_FILTER_MEDIAN = 1,
    IMAGE2DALG_FILTER_BILATERAL = 2
} Image2DAlg_FilterType;

IMAGE2DALG_EXPORT int Image2DAlg_ApplyFilter(const Image2DAlg_Image* src, Image2DAlg_Image* dst,
                                             Image2DAlg_FilterType type, double param1, double param2);

// 边缘检测
IMAGE2DALG_EXPORT int Image2DAlg_DetectEdges(const Image2DAlg_Image* src, Image2DAlg_Image* dst,
                                             double threshold1, double threshold2);

// 特征点检测
typedef struct {
    float x, y;
    float response;
    int octave;
} Image2DAlg_KeyPoint;

IMAGE2DALG_EXPORT int Image2DAlg_DetectKeypoints(const Image2DAlg_Image* src,
                                                  Image2DAlg_KeyPoint** keypoints, int* count);

// 图像读取（支持 png/tiff/jpg/jpeg）
IMAGE2DALG_EXPORT int Image2DAlg_ReadImage(const char* img_path, Image2DAlg_Image* out_image);

#ifdef __cplusplus
}
#endif

#endif // IMAGE2DALG_API_H