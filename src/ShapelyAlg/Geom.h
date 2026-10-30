#ifndef SHAPELY_GEOM_H
#define SHAPELY_GEOM_H

// shapely::Geom —— 纯二维几何代数与拓扑引擎（基于 GEOS C API，Pimpl 隔离）。
// 设计约束（见技术交底书）：
//   * 公共头文件绝对纯净：仅 C++ 标准库 + shapely::Types.h，零第三方 include；
//   * Pimpl：GEOSGeometry* 完全隐藏在 .cpp；
//   * 静态工厂：构造函数私有，createPoint/createPolygon/... 强制声明几何意图；
//   * 算子原子化：点/线/面能力严格隔离，越界调用返回零值不抛异常；
//   * 不可变性：布尔/形变运算返回新对象，不改原对象；
//   * RAII + 移动语义：禁拷贝（防 Double Free），析构自动释放底层资源。
// 泛化输入（鸭子类型模板，编译期提取 .x/.y 转 double，3D 自动丢 Z）：
//   cv::Point/Point2f/Point2d/Point3f/Point3d、含 .x/.y 的自定义结构体、
//   扁平数组 [x1,y1,...]、二维嵌套数组（内层取 [0][1]）。
//   OpenCV Rect/RotatedRect 输入走适配层 GeomCVAdapter.hpp。

#include "Types.h"
#include "ShapelyAlg_export.h"      // 自动生成的导出宏

#include <string>
#include <utility>
#include <vector>

namespace shapely {

class SHAPELYALG_EXPORT Geom
{
public:
    // ---------------- 生命周期（RAII，只移动） ----------------
    ~Geom();
    Geom(Geom&& other) noexcept;
    Geom& operator=(Geom&& other) noexcept;
    Geom(const Geom&) = delete;
    Geom& operator=(const Geom&) = delete;

    // ---------------- 通用功能（点/线/面） ----------------
    bool isEmpty() const;                       // 构建是否成功
    std::string wkt() const;                    // WKT 输出（调试/序列化）
    std::string typeName() const;               // "point"/"linestring"/"polygon"
    BoundingBox2d bounds() const;               // 轴对齐包围盒
    std::vector<Point2d> getExteriorPoints() const;   // 顶点（渲染用）
    double distanceTo(const Geom& other) const; // 最短二维距离（空/失败返回 -1）
    bool intersects(const Geom& other) const;   // 接触或重叠

    // ---------------- 点专属 ----------------
    double getX() const;                        // 非点返回 0
    double getY() const;
    bool within(const Geom& lineOrPolygon) const;   // 点在线/面内

    // ---------------- 线专属 ----------------
    double length() const;                      // 折线总长（非线返回 0）
    bool crosses(const Geom& other) const;      // 线与线/面拓扑交叉

    // ---------------- 面专属 ----------------
    double area() const;                        // 面积（非面返回 0）
    Point2d centroid() const;                   // 真质心（面积加权，凹多边形可能在面外）
    bool contains(const Geom& other) const;     // 完全包含点/线/面
    Geom intersection(const Geom& other) const; // 交集（新对象）
    Geom unionWith(const Geom& other) const;    // 并集（新对象）
    Geom difference(const Geom& other) const;   // 差集（新对象）
    Geom buffer(double dist, int quadSegs = 8) const;   // 缓冲区（新对象）
    Geom convexHull() const;                    // 凸包（新对象）
    BoundingBox2d toRect2d() const;             // 最小包围盒（同 bounds）

    // =====================================================================
    // 静态工厂（模板，Header-Only；鸭子类型：只需 .x/.y 成员）
    // =====================================================================

    // ---- 点 ----
    static Geom createPoint(double x, double y);
    template <typename P> static Geom createPoint(const P& p)          // 含 .x/.y（3D 丢 Z）
        { return createPoint((double)p.x, (double)p.y); }
    template <typename T> static Geom createPoint(const std::vector<T>& flat)   // [x,y,...]
        { return flat.size() < 2 ? Geom() : createPoint((double)flat[0], (double)flat[1]); }

    // ---- 线（不强制闭合） ----
    template <typename P> static Geom createLine(const std::vector<P>& pts);
    template <typename T> static Geom createLineFromFlat(const std::vector<T>& flat);
    template <typename T> static Geom createLineFromNested(const std::vector<std::vector<T>>& nested);

    // ---- 多边形（自动闭合） ----
    template <typename P> static Geom createPolygon(const std::vector<P>& pts);
    template <typename T> static Geom createPolygonFromFlat(const std::vector<T>& flat);
    template <typename T> static Geom createPolygonFromNested(const std::vector<std::vector<T>>& nested);

    // ---- WKT ----
    static Geom fromWKT(const std::string& wkt);

    // ---- 曲线（离散化为普通多边形，语义丢失） ----
    // max_error：最大弦高误差（默认 1e-4，自适应分段 θ=2·acos(1-e/r)）；
    // segments > 0 时强制指定分段数。顶点数强制截断在 [8, 4096]。
    static Geom createCircle(double cx, double cy, double radius,
                             double max_error = 1e-4, int segments = 0);
    static Geom createEllipse(double xc, double yc, double a, double b, double theta_deg,
                              double max_error = 1e-4, int segments = 0);

private:
    friend class GeomCVAdapter;    // 适配层访问底层构建
    friend class Stereo;           // 2.5D 桥接（projectTo2D 构建空对象）
    Geom();                        // 空对象（构建失败兜底）
    explicit Geom(void* g);        // 接管底层句柄（内部/适配层）

    // 底层构建（.cpp，纯 double；点集已闭合与否由调用方保证语义）
    static Geom makePoint(double x, double y);
    static Geom makeLine(const std::vector<Point2d>& pts);
    static Geom makePolygon(const std::vector<Point2d>& pts);   // 自动闭合
    static Geom makeRingPolygon(const std::vector<Point2d>& pts);   // 已闭合
    static Geom parseWKT(const std::string& wkt);
    static Geom makeCircle(double cx, double cy, double r, double max_error, int segments);
    static Geom makeEllipse(double xc, double yc, double a, double b, double theta_deg,
                            double max_error, int segments);

    // Pimpl：底层句柄（实际类型在 .cpp，头文件零 GEOS 痕迹）
    void* geom_ = nullptr;
};

// =====================================================================
// 模板工厂实现（Header-Only，编译期特化）
// =====================================================================

namespace detail {

// 点集归一化：任意 .x/.y 结构 → vector<Point2d>（3D 丢 Z，统一 double）
template <typename P>
inline std::vector<Point2d> normalizePoints(const std::vector<P>& pts)
{
    std::vector<Point2d> out;
    out.reserve(pts.size());
    for (const P& p : pts)
        out.push_back(Point2d{ (double)p.x, (double)p.y });
    return out;
}

// 扁平数组 [x1,y1,x2,y2,...] → 点集
template <typename T>
inline std::vector<Point2d> flatToPoints(const std::vector<T>& flat)
{
    std::vector<Point2d> pts;
    const size_t n = flat.size() / 2;
    pts.reserve(n);
    for (size_t i = 0; i < n; ++i)
        pts.push_back(Point2d{ (double)flat[i * 2], (double)flat[i * 2 + 1] });
    return pts;
}

// 二维嵌套数组（内层取索引 0,1 作为 X,Y，忽略后续）→ 点集
template <typename T>
inline std::vector<Point2d> nestedToPoints(const std::vector<std::vector<T>>& nested)
{
    std::vector<Point2d> pts;
    pts.reserve(nested.size());
    for (const std::vector<T>& p : nested)
        if (p.size() >= 2)
            pts.push_back(Point2d{ (double)p[0], (double)p[1] });
    return pts;
}

} // namespace detail

template <typename P>
inline Geom Geom::createLine(const std::vector<P>& pts)
{
    return makeLine(detail::normalizePoints(pts));
}

template <typename T>
inline Geom Geom::createLineFromFlat(const std::vector<T>& flat)
{
    return makeLine(detail::flatToPoints(flat));
}

template <typename T>
inline Geom Geom::createLineFromNested(const std::vector<std::vector<T>>& nested)
{
    return makeLine(detail::nestedToPoints(nested));
}

template <typename P>
inline Geom Geom::createPolygon(const std::vector<P>& pts)
{
    return makePolygon(detail::normalizePoints(pts));   // 内部自动闭合
}

template <typename T>
inline Geom Geom::createPolygonFromFlat(const std::vector<T>& flat)
{
    return makePolygon(detail::flatToPoints(flat));
}

template <typename T>
inline Geom Geom::createPolygonFromNested(const std::vector<std::vector<T>>& nested)
{
    return makePolygon(detail::nestedToPoints(nested));
}

} // namespace shapely

#endif // SHAPELY_GEOM_H
