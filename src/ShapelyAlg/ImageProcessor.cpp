// ImageProcessor.cpp —— 二维图像算子实现（imgproc 下沉在此，头文件零 imgproc）。
// 纯透传原则：直接包装 OpenCV 原生 API，严禁手写底层循环。

#include "ImageProcessor.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace shapely {

// ---------------- 1. 阈值与二值化 ----------------

void ImageProcessor::threshold(const cv::Mat& src, cv::Mat& dst,
                               double thresh, double maxval, int type)
{
    cv::threshold(src, dst, thresh, maxval, type);
}

void ImageProcessor::adaptiveThreshold(const cv::Mat& src, cv::Mat& dst, double maxval,
                                       int method, int block_size, double C)
{
    cv::adaptiveThreshold(src, dst, maxval, method, cv::THRESH_BINARY, block_size, C);
}

// ---------------- 2. 形态学 ----------------

void ImageProcessor::dilate(const cv::Mat& src, cv::Mat& dst, const cv::Mat& kernel)
{
    cv::dilate(src, dst, kernel);
}

void ImageProcessor::erode(const cv::Mat& src, cv::Mat& dst, const cv::Mat& kernel)
{
    cv::erode(src, dst, kernel);
}

void ImageProcessor::morphologyEx(const cv::Mat& src, cv::Mat& dst, int op, const cv::Mat& kernel)
{
    cv::morphologyEx(src, dst, op, kernel);
}

// ---------------- 3. 滤波与降噪 ----------------

void ImageProcessor::gaussianBlur(const cv::Mat& src, cv::Mat& dst,
                                  cv::Size ksize, double sigmaX, double sigmaY)
{
    cv::GaussianBlur(src, dst, ksize, sigmaX, sigmaY);
}

void ImageProcessor::medianBlur(const cv::Mat& src, cv::Mat& dst, int ksize)
{
    cv::medianBlur(src, dst, ksize);
}

void ImageProcessor::bilateralFilter(const cv::Mat& src, cv::Mat& dst, int d,
                                     double sigmaColor, double sigmaSpace)
{
    cv::bilateralFilter(src, dst, d, sigmaColor, sigmaSpace);
}

// ---------------- 4. 边缘与特征检测 ----------------

void ImageProcessor::canny(const cv::Mat& src, cv::Mat& edges, double threshold1, double threshold2)
{
    cv::Canny(src, edges, threshold1, threshold2);
}

void ImageProcessor::sobel(const cv::Mat& src, cv::Mat& dst, int ddepth, int dx, int dy, int ksize)
{
    cv::Sobel(src, dst, ddepth, dx, dy, ksize);
}

// ---------------- 5. 几何变换 ----------------

void ImageProcessor::resize(const cv::Mat& src, cv::Mat& dst, cv::Size dsize,
                            double fx, double fy, int interpolation)
{
    cv::resize(src, dst, dsize, fx, fy, interpolation);
}

void ImageProcessor::warpAffine(const cv::Mat& src, cv::Mat& dst, const cv::Mat& M, cv::Size dsize)
{
    cv::warpAffine(src, dst, M, dsize);
}

void ImageProcessor::warpPerspective(const cv::Mat& src, cv::Mat& dst, const cv::Mat& M, cv::Size dsize)
{
    cv::warpPerspective(src, dst, M, dsize);
}

// ---------------- 6. 模板匹配 ----------------

void ImageProcessor::matchTemplate(const cv::Mat& image, const cv::Mat& templ,
                                   cv::Mat& result, int method)
{
    cv::matchTemplate(image, templ, result, method);
}

void ImageProcessor::findBestMatch(const cv::Mat& result, int method,
                                   cv::Point& out_loc, double& out_score)
{
    // SQDIFF / SQDIFF_NORMED 越小越好，其余越大越好
    const bool isSqdiff = (method == cv::TM_SQDIFF || method == cv::TM_SQDIFF_NORMED);
    double minVal = 0, maxVal = 0;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
    if (isSqdiff) {
        out_loc = minLoc;
        out_score = minVal;
    } else {
        out_loc = maxLoc;
        out_score = maxVal;
    }
}

// ---------------- 7. 多尺度金字塔 ----------------

void ImageProcessor::pyrDown(const cv::Mat& src, cv::Mat& dst)
{
    cv::pyrDown(src, dst);
}

void ImageProcessor::pyrUp(const cv::Mat& src, cv::Mat& dst)
{
    cv::pyrUp(src, dst);
}

void ImageProcessor::buildGaussianPyramid(const cv::Mat& src,
                                          std::vector<cv::Mat>& out_pyramid, int maxlevel)
{
    out_pyramid.clear();
    out_pyramid.push_back(src);
    cv::Mat cur = src;
    for (int lv = 0; lv < maxlevel; ++lv) {
        cv::Mat next;
        cv::pyrDown(cur, next);
        out_pyramid.push_back(next);
        cur = next;
    }
}

// ---------------- 8. 空间切片与瓦片 ----------------

void ImageProcessor::splitIntoTiles(const cv::Mat& src, cv::Size tile_size, int overlap,
                                    std::vector<Tile>& out_tiles)
{
    out_tiles.clear();
    if (src.empty() || tile_size.width <= 0 || tile_size.height <= 0)
        return;
    const int step_w = tile_size.width - overlap;
    const int step_h = tile_size.height - overlap;
    if (step_w <= 0 || step_h <= 0)
        return;
    const int cols = (std::max)(1, (int)std::ceil((double)src.cols / step_w));
    const int rows = (std::max)(1, (int)std::ceil((double)src.rows / step_h));
    out_tiles.reserve((size_t)cols * rows);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            Tile t;
            t.roi = cv::Rect(c * step_w, r * step_h, tile_size.width, tile_size.height);
            t.covered_roi = t.roi & cv::Rect(0, 0, src.cols, src.rows);
            if (t.covered_roi.empty())
                continue;
            t.image = src(t.covered_roi);   // 浅拷贝（引用计数，零拷贝）
            t.roi_base = t.covered_roi;
            t.scale = 1.0;
            t.level = 0;
            out_tiles.push_back(std::move(t));
        }
    }
}

void ImageProcessor::buildTilePyramid(const cv::Mat& src, cv::Size tile_size, int max_level,
                                      std::vector<std::vector<Tile>>& out_tiles)
{
    out_tiles.clear();
    if (src.empty())
        return;
    // level 0 原图瓦片
    std::vector<Tile> level0;
    splitIntoTiles(src, tile_size, 0, level0);
    out_tiles.push_back(std::move(level0));
    // 逐层下采样（2 幂 scale）
    cv::Mat cur = src;
    for (int lv = 1; lv <= max_level; ++lv) {
        cv::Mat next;
        cv::pyrDown(cur, next);
        if (next.empty())
            break;
        std::vector<Tile> levelTiles;
        splitIntoTiles(next, tile_size, 0, levelTiles);
        const double scale = 1.0 / (double)(1 << lv);
        for (Tile& t : levelTiles) {
            t.level = lv;
            t.scale = scale;
            // covered_roi 映射回 level 0（2 幂 scale，四舍五入对齐）
            t.roi_base = cv::Rect(
                (int)std::lround(t.covered_roi.x / scale),
                (int)std::lround(t.covered_roi.y / scale),
                (int)std::lround(t.covered_roi.width / scale),
                (int)std::lround(t.covered_roi.height / scale));
        }
        out_tiles.push_back(std::move(levelTiles));
        cur = next;
    }
}

// ---------------- 9. 轮廓提取与形状分析 ----------------

void ImageProcessor::findContours(const cv::Mat& binary_src,
                                  std::vector<std::vector<cv::Point>>& out_contours,
                                  int mode, int method)
{
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binary_src, out_contours, hierarchy, mode, method);
}

cv::RotatedRect ImageProcessor::getMinAreaRect(const std::vector<cv::Point>& points)
{
    return cv::minAreaRect(points);
}

cv::Rect ImageProcessor::getBoundingRect(const std::vector<cv::Point>& points)
{
    return cv::boundingRect(points);
}

} // namespace shapely
