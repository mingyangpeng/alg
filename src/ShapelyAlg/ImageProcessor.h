#ifndef SHAPELY_IMAGE_PROCESSOR_H
#define SHAPELY_IMAGE_PROCESSOR_H

// shapely::ImageProcessor —— 二维图像基础算子模块（纯静态工具类）。
// 设计约束（见技术交底书）：
//   * 公共头文件仅包含 <opencv2/core/mat.hpp>（cv::Mat 定义所需），严禁 imgproc；
//   * 枚举参数降级为原生 int（业务层传 OpenCV 枚举发生隐式转换）；
//   * 算法实现全部下沉 .cpp（imgproc 只在 .cpp 内 include）；
//   * 零拷贝：const cv::Mat& 输入，ROI/瓦片共享底层内存（引用计数）；
//   * 无业务语义：只输出矩阵/点集；
//   * 命名规范：分类前缀 + 动作（threshold/dilate/filter/canny/...）。

#include <opencv2/core/mat.hpp>

#include "ShapelyAlg_export.h"      // 自动生成的导出宏

#include <vector>

namespace shapely {

// 图像金字塔瓦片（OpenCV 原生结构，零第三方依赖）
//
// 语义契约（务必遵守，否则边缘/跨层场景会出错）：
//   1. image 为浅拷贝，共享源图内存；连续性不保证，裸指针遍历前必须
//      检查 image.isContinuous() 或 clone()。
//   2. roi = 设计网格矩形（当前层坐标系），width/height 恒为 tile_size，
//      位置 = (col*tile_size.width, row*tile_size.height)，仅用于索引。
//   3. covered_roi = roi 与当前层图像边界的交集，是"实际有像素"的区域。
//      命中测试、跨层映射一律用 covered_roi，不要用 roi。
//   4. roi_base = covered_roi 映射到 level 0 坐标系的结果（2 幂 scale）。
//   5. scale = 当前层相对 level 0 的缩放比（典型为 1/(1<<level)）。
//   6. 生命周期：源图由调用方持有；image 的 cv::Mat 引用计数保证像素存活。
struct Tile {
    cv::Mat image;        // 瓦片图像（浅拷贝，零拷贝）
    cv::Rect roi;         // 设计网格矩形（当前层坐标）
    cv::Rect covered_roi; // 实际覆盖区域（roi ∩ 图像边界）
    cv::Rect roi_base;    // covered_roi 映射到 level 0（2 幂 scale 用）
    double scale = 1.0;   // 相对 level 0 的缩放比
    int level = 0;        // 金字塔层级
};

class SHAPELYALG_EXPORT ImageProcessor
{
public:
    ImageProcessor() = delete;   // 纯静态工具类

    // ---------------- 1. 阈值与二值化 ----------------
    // type：cv::THRESH_*（int 降级，业务层直接传枚举）
    static void threshold(const cv::Mat& src, cv::Mat& dst,
                          double thresh, double maxval, int type);
    // method：cv::ADAPTIVE_THRESH_*；blockSize 奇数
    static void adaptiveThreshold(const cv::Mat& src, cv::Mat& dst, double maxval,
                                  int method, int block_size, double C);

    // ---------------- 2. 形态学 ----------------
    static void dilate(const cv::Mat& src, cv::Mat& dst, const cv::Mat& kernel);
    static void erode(const cv::Mat& src, cv::Mat& dst, const cv::Mat& kernel);
    // op：cv::MORPH_OPEN/CLOSE/GRADIENT/TOPHAT/BLACKHAT
    static void morphologyEx(const cv::Mat& src, cv::Mat& dst, int op, const cv::Mat& kernel);

    // ---------------- 3. 滤波与降噪 ----------------
    static void gaussianBlur(const cv::Mat& src, cv::Mat& dst,
                             cv::Size ksize, double sigmaX, double sigmaY = 0);
    static void medianBlur(const cv::Mat& src, cv::Mat& dst, int ksize);
    static void bilateralFilter(const cv::Mat& src, cv::Mat& dst, int d,
                                double sigmaColor, double sigmaSpace);

    // ---------------- 4. 边缘与特征检测 ----------------
    static void canny(const cv::Mat& src, cv::Mat& edges, double threshold1, double threshold2);
    static void sobel(const cv::Mat& src, cv::Mat& dst, int ddepth, int dx, int dy, int ksize = 3);

    // ---------------- 5. 几何变换 ----------------
    static void resize(const cv::Mat& src, cv::Mat& dst, cv::Size dsize,
                       double fx = 0, double fy = 0, int interpolation = 1 /*LINEAR*/);
    static void warpAffine(const cv::Mat& src, cv::Mat& dst, const cv::Mat& M, cv::Size dsize);
    static void warpPerspective(const cv::Mat& src, cv::Mat& dst, const cv::Mat& M, cv::Size dsize);

    // ---------------- 6. 模板匹配 ----------------
    // method：cv::TM_*
    static void matchTemplate(const cv::Mat& image, const cv::Mat& templ,
                              cv::Mat& result, int method);
    // 解析 matchTemplate 得分矩阵 → 最佳匹配位置与得分
    static void findBestMatch(const cv::Mat& result, int method,
                              cv::Point& out_loc, double& out_score);

    // ---------------- 7. 多尺度金字塔 ----------------
    static void pyrDown(const cv::Mat& src, cv::Mat& dst);
    static void pyrUp(const cv::Mat& src, cv::Mat& dst);
    static void buildGaussianPyramid(const cv::Mat& src,
                                     std::vector<cv::Mat>& out_pyramid, int maxlevel);

    // ---------------- 8. 空间切片与瓦片 ----------------
    // 大图 → 带 overlap 的零拷贝瓦片（covered_roi 处理边界）
    static void splitIntoTiles(const cv::Mat& src, cv::Size tile_size, int overlap,
                               std::vector<Tile>& out_tiles);
    // 金字塔瓦片集合（level 0..max_level，每层独立瓦片网格）
    static void buildTilePyramid(const cv::Mat& src, cv::Size tile_size, int max_level,
                                 std::vector<std::vector<Tile>>& out_tiles);

    // ---------------- 9. 轮廓提取与形状分析（与 Geom 联动） ----------------
    // mode：cv::RETR_*；method：cv::CHAIN_APPROX_*
    static void findContours(const cv::Mat& binary_src,
                             std::vector<std::vector<cv::Point>>& out_contours,
                             int mode, int method);
    static cv::RotatedRect getMinAreaRect(const std::vector<cv::Point>& points);
    static cv::Rect getBoundingRect(const std::vector<cv::Point>& points);
};

} // namespace shapely

#endif // SHAPELY_IMAGE_PROCESSOR_H
