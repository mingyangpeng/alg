#ifndef SHAPELY_TYPES_H
#define SHAPELY_TYPES_H

// shapely 库内部原生轻量数据结构（公共头文件，零第三方依赖）。
// 所有对外输出统一使用这些结构与 double 精度，不绑定 GEOS/OpenCV/PCL/Eigen。

#include <cstddef>
#include <vector>

#include "ShapelyAlg_export.h"      // OBB3d::intersects/contains 实现在 .cpp，需导出

namespace shapely {

// 二维点（double）
struct Point2d {
    double x = 0.0;
    double y = 0.0;
};

// 三维点（double）
struct Point3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// 二维轴对齐包围盒
struct BoundingBox2d {
    double min_x = 0.0, min_y = 0.0;
    double max_x = 0.0, max_y = 0.0;
    bool valid = false;   // false = 空盒

    double width()  const { return max_x - min_x; }
    double height() const { return max_y - min_y; }
};

// 三维轴对齐包围盒
struct AABB3d {
    double min_x = 0.0, min_y = 0.0, min_z = 0.0;
    double max_x = 0.0, max_y = 0.0, max_z = 0.0;
    bool valid = false;   // false = 空盒

    double volume() const { return (max_x - min_x) * (max_y - min_y) * (max_z - min_z); }

    // 分离轴定理：三轴投影区间均重叠才相交
    bool intersects(const AABB3d& o) const
    {
        return valid && o.valid &&
               min_x <= o.max_x && o.min_x <= max_x &&
               min_y <= o.max_y && o.min_y <= max_y &&
               min_z <= o.max_z && o.min_z <= max_z;
    }

    bool contains(const AABB3d& o) const
    {
        return valid && o.valid &&
               min_x <= o.min_x && max_x >= o.max_x &&
               min_y <= o.min_y && max_y >= o.max_y &&
               min_z <= o.min_z && max_z >= o.max_z;
    }
};

// 三维有向包围盒（PCA 主轴）
struct SHAPELYALG_EXPORT OBB3d {
    Point3d center;                    // 中心
    double extents[3] = { 0, 0, 0 };   // 三根主轴的半长
    double rotation[9] = {             // 3x3 行主序旋转矩阵（第 d 行 = 第 d 主轴方向）
        1, 0, 0,
        0, 1, 0,
        0, 0, 1,
    };
    bool valid = false;

    double volume() const { return 8.0 * extents[0] * extents[1] * extents[2]; }

    // 分离轴定理：3 根自身轴 + 3 根对方轴 + 9 根叉积轴，共 15 轴
    bool intersects(const OBB3d& o) const;
    bool contains(const OBB3d& o) const;
};

} // namespace shapely

#endif // SHAPELY_TYPES_H
