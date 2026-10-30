#ifndef SHAPELYALG_H
#define SHAPELYALG_H

// ShapelyAlg.h —— shapely 库对外总入口（唯一需要 include 的头）。
//
// 库结构（单文件夹，公共头纯净 + 实现下沉 .cpp）：
//   Types.h           原生轻量结构（Point2d/3d、BoundingBox2d、AABB3d、OBB3d）
//   Geom.h            二维几何代数与拓扑（GEOS，Pimpl，公共头零 GEOS）
//   Stereo.h          三维空间数据管理与度量（纯数学，Pimpl）
//   ImageProcessor.h  二维图像算子（OpenCV，静态工具类，头仅 core/mat）
//   CloudProcessor.h  三维点云算子（PCL，显式实例化白名单）
//   CloudIO.h         点云文件读写（PCD/PLY/TXT）
//
// 快速上手：
//   #include "ShapelyAlg.h"
//
//   // 二维：鸭子类型输入（cv::Point 系 / 含 .x/.y 的结构 / 扁平数组），3D 自动丢 Z
//   shapely::Geom box = shapely::Geom::createPolygon(pts);
//   shapely::Geom cir = shapely::Geom::createCircle(0, 0, 10);
//   double a = cir.intersection(box).area();
//
//   // OpenCV 矩形/旋转矩形（适配层，随总入口一并引入）
//   shapely::Geom r = shapely::GeomCVAdapter::createRect(cv::Rect(0, 0, 30, 40));
//   shapely::Geom e = shapely::GeomCVAdapter::createEllipse(rotatedRect);
//
//   // 三维度量
//   shapely::Stereo st = shapely::Stereo::createFromPoints(pts3d);
//   shapely::AABB3d bb = st.getAABB();
//   shapely::Geom proj = st.projectTo2D();          // 2.5D 桥接
//
//   // 图像（枚举直接传 cv:: 常量）
//   shapely::ImageProcessor::threshold(src, dst, 100, 255, cv::THRESH_BINARY);
//   shapely::ImageProcessor::splitIntoTiles(src, {512, 512}, 0, tiles);
//
//   // 点云（模板白名单：pcl::PointXYZ / PointXYZI / PointXYZRGB）
//   auto cloud = shapely::CloudProcessor<pcl::PointXYZ>::filterVoxelGrid(src, 0.01);
//   auto loaded = shapely::CloudIO<pcl::PointXYZ>::ioLoadPCD("a.pcd");

#include "Types.h"
#include "Geom.h"
#include "Stereo.h"
#include "ImageProcessor.h"
#include "CloudProcessor.h"
#include "CloudIO.h"

// ---------------- OpenCV 适配层（cv::Rect/RotatedRect → Geom） ----------------

#include <opencv2/core/types.hpp>

namespace shapely {

class GeomCVAdapter
{
public:
    // cv::Rect 系列 → 矩形面（四角点自动闭合）
    template <typename R>
    static Geom createRect(const R& r)
    {
        const double x = (double)r.x, y = (double)r.y;
        const double w = (double)r.width, h = (double)r.height;
        const std::vector<Point2d> corners = {
            { x,     y },
            { x + w, y },
            { x + w, y + h },
            { x,     y + h },
        };
        return Geom::createPolygon(corners);
    }

    static Geom createRect(const cv::Rect2d& r)
    {
        return createRect<cv::Rect2d>(r);
    }

    // cv::RotatedRect → 椭圆面（center/size/angle 离散化）
    static Geom createEllipse(const cv::RotatedRect& rr,
                               double max_error = 1e-4, int segments = 0)
    {
        return Geom::createEllipse((double)rr.center.x, (double)rr.center.y,
                                   (double)rr.size.width * 0.5,
                                   (double)rr.size.height * 0.5,
                                   (double)rr.angle, max_error, segments);
    }
};

} // namespace shapely

// ---------------- PCL 适配层（pcl::PointCloud → Stereo） ----------------
// （PCL 头较重，按需单独 include "ShapelyAlg.h" 亦可用；
//   本总入口默认引入，与 CloudProcessor 一致。）

#include <pcl/point_types.h>
#include <pcl/point_cloud.h>

namespace shapely {

class StereoPCLAdapter
{
public:
    // PCL 点云 → Stereo（遍历提取三维坐标）
    template <typename PointT>
    static Stereo createFromCloud(const pcl::PointCloud<PointT>& cloud)
    {
        std::vector<Point3d> pts;
        pts.reserve(cloud.size());
        for (const PointT& p : cloud.points)
            pts.push_back(Point3d{ (double)p.x, (double)p.y, (double)p.z });
        return Stereo::makeFromPoints(std::move(pts));
    }

    // PCL 点云指针重载
    template <typename PointT>
    static Stereo createFromCloud(const typename pcl::PointCloud<PointT>::Ptr& cloud)
    {
        return cloud ? createFromCloud(*cloud) : Stereo();
    }
};

} // namespace shapely

#endif // SHAPELYALG_H
