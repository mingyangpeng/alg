#ifndef SHAPELY_STEREO_H
#define SHAPELY_STEREO_H

// shapely::Stereo —— 三维空间基础数据管理与度量模块（Pimpl，公共头纯净）。
// 设计约束（见技术交底书）：
//   * 纯三维：只处理 X/Y/Z 坐标与空间度量，不做 2D 布尔运算；
//   * 无业务语义：只回答纯数学事实（距离/包围盒），不做业务判断；
//   * Pimpl：内部点云容器隐藏在 .cpp；
//   * RAII + 移动语义：禁拷贝；
//   * 2.5D 桥接：projectTo2D() 剥离 Z 生成 Geom。
// 泛化输入（鸭子类型模板，编译期提取 .x/.y/.z 转 double）：
//   cv::Point3f/Point3d、含 .x/.y/.z 的自定义结构体、
//   扁平数组 [x,y,z,...]、二维嵌套数组（内层取 [0][1][2]）。
//   PCL 点云输入走适配层 StereoPCLAdapter.hpp。

#include "Types.h"
#include "Geom.h"
#include "ShapelyAlg_export.h"      // 自动生成的导出宏

#include <vector>

namespace shapely {

class SHAPELYALG_EXPORT Stereo
{
public:
    // ---------------- 生命周期（RAII，只移动） ----------------
    ~Stereo();
    Stereo(Stereo&& other) noexcept;
    Stereo& operator=(Stereo&& other) noexcept;
    Stereo(const Stereo&) = delete;
    Stereo& operator=(const Stereo&) = delete;

    // ---------------- 基础数据管理 ----------------
    bool isEmpty() const;                       // 无点且无包围盒
    size_t size() const;                        // 点数量
    std::vector<Point3d> getPoints() const;     // 点集输出
    void getZBounds(double& min_z, double& max_z) const;   // Z 轴范围（空时置 0）
    Stereo filterByZRange(double min_z, double max_z) const;   // 高度区间过滤（新对象）

    // ---------------- 包围盒与体积 ----------------
    AABB3d getAABB() const;                     // 轴对齐包围盒
    OBB3d getOBB() const;                       // PCA 有向包围盒
    double getAABBVolume() const;
    double getOBBVolume() const;

    // ---------------- 点集间距离与位置（纯数学事实） ----------------
    double minDistanceTo(const Stereo& other) const;   // 最近欧氏距离（空返回 -1）
    void findClosestPair(const Stereo& other,
                         Point3d& pt_self, Point3d& pt_other) const;   // 最近点对
    bool hasCoincidentPointWith(const Stereo& other, double tolerance) const;

    // ---------------- 2.5D 降维桥接 ----------------
    // 剥离 Z，XY 点集 → Geom 多边形（凸包轮廓，自动闭合）；
    // 点数 < 3 时退化为 Geom 空对象。
    Geom projectTo2D() const;

    // =====================================================================
    // 静态工厂（模板，Header-Only）
    // =====================================================================

    // 从 3D 点集（含 .x/.y/.z 的任意结构）
    template <typename P> static Stereo createFromPoints(const std::vector<P>& pts);
    // 从一维扁平数组 [x,y,z, x,y,z, ...]
    template <typename T> static Stereo createFromFlatArray(const std::vector<T>& flat);
    // 从二维嵌套数组（内层取 0,1,2 索引）
    template <typename T> static Stereo createFromNestedArray(const std::vector<std::vector<T>>& nested);
    // 从包围盒（8 个角点）
    static Stereo createFromBox(const AABB3d& box);
    static Stereo createFromBox(const OBB3d& box);
    // 从边界值
    static Stereo createFromBox(double min_x, double min_y, double min_z,
                                double max_x, double max_y, double max_z);

private:
    friend class StereoPCLAdapter;
    struct Impl;                     // Pimpl（定义在 .cpp）
    Stereo();                        // 空对象
    static Stereo makeFromPoints(std::vector<Point3d> pts);   // 内部构建（.cpp）

    // Pimpl：内部点云容器（实际类型在 .cpp）
    Impl* impl_ = nullptr;
};

// =====================================================================
// 模板工厂实现（Header-Only）
// =====================================================================

namespace detail {

template <typename P>
inline std::vector<Point3d> normalize3D(const std::vector<P>& pts)
{
    std::vector<Point3d> out;
    out.reserve(pts.size());
    for (const P& p : pts)
        out.push_back(Point3d{ (double)p.x, (double)p.y, (double)p.z });
    return out;
}

} // namespace detail

template <typename P>
inline Stereo Stereo::createFromPoints(const std::vector<P>& pts)
{
    return makeFromPoints(detail::normalize3D(pts));
}

template <typename T>
inline Stereo Stereo::createFromFlatArray(const std::vector<T>& flat)
{
    std::vector<Point3d> pts;
    const size_t n = flat.size() / 3;
    pts.reserve(n);
    for (size_t i = 0; i < n; ++i)
        pts.push_back(Point3d{ (double)flat[i * 3], (double)flat[i * 3 + 1], (double)flat[i * 3 + 2] });
    return makeFromPoints(std::move(pts));
}

template <typename T>
inline Stereo Stereo::createFromNestedArray(const std::vector<std::vector<T>>& nested)
{
    std::vector<Point3d> pts;
    pts.reserve(nested.size());
    for (const std::vector<T>& p : nested)
        if (p.size() >= 3)
            pts.push_back(Point3d{ (double)p[0], (double)p[1], (double)p[2] });
    return makeFromPoints(std::move(pts));
}

} // namespace shapely

#endif // SHAPELY_STEREO_H
