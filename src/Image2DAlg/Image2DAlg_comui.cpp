// ComUI 统一 C ABI 适配层（Image2DAlg）：
// 把本库的逐函数 C 接口包装成宿主 native 执行器要求的单一入口签名
// int32 method(const AlgoCInput*, AlgoCOutput*) + algo_free 释放约定（见 docs/THIRD_PARTY_OPS.md §5.5）。
// 输出缓冲一律 malloc（与 algo_free 的 free 配对）。
#include "Image2DAlg_export.h"
#include "include/Image2DAlg_api.h"
#include "../comui_native_abi.h"

#include <opencv2/core.hpp>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern "C" {

// ---------------- 统一释放约定 ----------------

IMAGE2DALG_EXPORT void algo_free(void* p) {
    free(p);
}

// ---------------- 参数读取辅助 ----------------

static double GetDoubleParam(const AlgoCInput* in, double def) {
    for (int i = 0; i < in->paramCount; i++)
        if (in->params[i].kind == 0) return in->params[i].d;
    return def;
}

static const char* GetTextParam(const AlgoCInput* in) {
    for (int i = 0; i < in->paramCount; i++)
        if (in->params[i].kind == 2) return in->params[i].s;
    return nullptr;
}

static AlgoCImageOut* AllocImageOut(int width, int height, uint8_t* bgra) {
    AlgoCImageOut* arr = (AlgoCImageOut*)malloc(sizeof(AlgoCImageOut));
    if (!arr) return nullptr;
    arr->width = width;
    arr->height = height;
    arr->bgra = bgra;
    return arr;
}

// ---------------- 算子入口：图像读取（node.json: alg.image2d.read） ----------------

IMAGE2DALG_EXPORT int Image2DAlg_ComuiRead(const AlgoCInput* in, AlgoCOutput* out) {
    memset(out, 0, sizeof(*out));
    const char* path = GetTextParam(in);
    if (!path || !path[0]) { out->error = "未设置图像路径"; return 1; }

    Image2DAlg_Image img = {};
    int rc = Image2DAlg_ReadImage(path, &img);   // 内部已统一转 BGRA 紧排（OpenCV 模式契约）
    if (rc != 0) { out->error = "读取失败：路径不存在或格式不支持（png/tiff/jpg/jpeg）"; return 2; }

    out->images = AllocImageOut(img.width, img.height, img.data);   // data 由 ReadImage malloc，algo_free 配对
    if (!out->images) { Image2DAlg_FreeImage(&img); out->error = "内存不足"; return 3; }
    out->imageCount = 1;
    return 0;
}

// ---------------- 算子入口：Gamma 校正（node.json: alg.image2d.gamma） ----------------

IMAGE2DALG_EXPORT int Image2DAlg_ComuiGamma(const AlgoCInput* in, AlgoCOutput* out) {
    memset(out, 0, sizeof(*out));
    if (in->imageCount < 1 || !in->images) { out->error = "输入未连线"; return 1; }

    double gamma = GetDoubleParam(in, 1.0);
    if (gamma < 0.05) gamma = 0.05;

    const AlgoCImage* src = &in->images[0];
    size_t n = (size_t)src->width * (size_t)src->height * 4;

    // LUT（OpenCV 风格：255 * (v/255)^(1/gamma)）
    uint8_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = (uint8_t)cv::saturate_cast<uchar>(255.0 * pow(i / 255.0, 1.0 / gamma));

    uint8_t* px = (uint8_t*)malloc(n);
    if (!px) { out->error = "内存不足"; return 2; }
    for (size_t i = 0; i < n; i += 4) {
        px[i]     = lut[src->bgra[i]];
        px[i + 1] = lut[src->bgra[i + 1]];
        px[i + 2] = lut[src->bgra[i + 2]];
        px[i + 3] = 255;
    }

    out->images = AllocImageOut(src->width, src->height, px);
    if (!out->images) { free(px); out->error = "内存不足"; return 2; }
    out->imageCount = 1;
    return 0;
}

} // extern "C"
