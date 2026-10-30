// ShapelyAlg_test.cpp —— shapely 库四模块验证（控制台，参数写死）。
// 覆盖：Geom（工厂/布尔/度量/离散化）、Stereo（AABB/OBB/距离/桥接）、
//       ImageProcessor（阈值/轮廓/瓦片/匹配）、CloudProcessor（滤波/拟合/聚类/树）。

#include "ShapelyAlg.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using shapely::Geom;
using shapely::Stereo;
using shapely::Point2d;
using shapely::Point3d;

// 自定义点结构（验证鸭子类型）
struct MyPt { float x; float y; };
struct MyPt3 { double x; double y; double z; };

static int g_fail = 0;

static void check(bool ok, const char* name)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++g_fail;
}

static bool near(double a, double b, double eps = 1e-6)
{
    return std::fabs(a - b) < eps;
}

// ---------------- Geom ----------------

static void testGeom()
{
    std::printf("== Geom 工厂 ==\n");
    {
        const Geom p = Geom::createPoint(3.0, 4.0);
        check(p.getX() == 3.0 && p.getY() == 4.0, "createPoint(x,y)");
    }
    check(Geom::createPoint(cv::Point(10, 20)).getX() == 10.0, "createPoint(cv::Point)");
    check(Geom::createPoint(cv::Point3f(1.5f, 2.5f, 99.0f)).getY() == 2.5, "createPoint(Point3f 丢Z)");
    check(Geom::createPoint(std::vector<double>{ 7, 8, 999 }).getX() == 7.0, "createPoint(扁平数组)");
    check(Geom::createPoint(MyPt{ 6.0f, 9.0f }).getY() == 9.0, "createPoint(自定义结构)");

    check(near(Geom::createLine(std::vector<Point2d>{
        { 0, 0 }, { 3, 0 }, { 3, 4 } }).length(), 7.0), "createLine 长度 3+4=7");
    check(near(Geom::createLineFromFlat(std::vector<float>{ 0, 0, 0, 5 }).length(), 5.0),
          "createLineFromFlat 长度 5");
    check(near(Geom::createLineFromNested(std::vector<std::vector<double>>{
        { 0, 0 }, { 0, 6 } }).length(), 6.0), "createLineFromNested 长度 6");

    check(near(Geom::createPolygon(std::vector<Point2d>{
        { 0, 0 }, { 4, 0 }, { 4, 4 }, { 0, 4 } }).area(), 16.0), "createPolygon 自动闭合 面积 16");
    check(near(Geom::createPolygonFromFlat(std::vector<double>{ 0, 0, 2, 0, 2, 2, 0, 2 }).area(), 4.0),
          "createPolygonFromFlat 面积 4");
    check(near(Geom::createPolygonFromNested(std::vector<std::vector<int>>{
        { 0, 0 }, { 3, 0 }, { 3, 3 }, { 0, 3 } }).area(), 9.0), "createPolygonFromNested 面积 9");

    check(near(shapely::GeomCVAdapter::createRect(cv::Rect(10, 20, 30, 40)).area(), 1200.0),
          "createRect(cv::Rect) 面积 30x40");
    check(near(Geom::createCircle(0, 0, 10.0).area(), M_PI * 100.0, 1e-2), "createCircle 面积 πr²");
    check(near(Geom::createCircle(0, 0, 10.0, 1e-4, 36).area(), M_PI * 100.0, 1.7),
          "createCircle 36段（理论差 1.59）");
    check(near(Geom::createEllipse(0, 0, 5.0, 2.0, 30.0).area(), M_PI * 10.0, 1e-3),
          "createEllipse(5参数) 面积 πab");
    check(near(shapely::GeomCVAdapter::createEllipse(
        cv::RotatedRect(cv::Point2f(0, 0), cv::Size2f(10, 4), 30.0f)).area(), M_PI * 10.0, 1e-3),
        "createEllipse(RotatedRect) 面积");
    check(near(Geom::fromWKT("POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))").area(), 1.0), "fromWKT 面积 1");

    std::printf("== Geom 运算 ==\n");
    const Geom a = shapely::GeomCVAdapter::createRect(cv::Rect2d(0, 0, 4, 4));
    const Geom b = shapely::GeomCVAdapter::createRect(cv::Rect2d(2, 2, 4, 4));
    check(near(a.intersection(b).area(), 4.0), "交集 2x2=4");
    check(near(a.unionWith(b).area(), 28.0), "并集 16+16-4=28");
    check(near(a.difference(b).area(), 12.0), "差集 16-4=12");

    const Geom c1 = Geom::createCircle(0, 0, 1.0);
    const Geom c2 = Geom::createCircle(5, 0, 1.0);
    check(near(c1.distanceTo(c2), 3.0, 1e-2), "距离 5-2=3");
    check(!c1.intersects(c2) && c1.intersects(Geom::createCircle(1.5, 0, 1.0)), "相交判定");

    check(a.contains(Geom::createPoint(1, 1)) && !a.contains(Geom::createPoint(5, 5)), "面包含点");
    check(Geom::createPoint(1, 1).within(a), "点 within 面");
    check(Geom::createLine(std::vector<Point2d>{ { 0, 2 }, { 10, 2 } })
              .crosses(Geom::createLine(std::vector<Point2d>{ { 5, 0 }, { 5, 10 } })), "线交叉");

    check(near(Geom::createPoint(0, 0).buffer(2.0, 16).area(), M_PI * 4.0, 0.05), "点 buffer(2)");
    check(near(Geom::createLine(std::vector<Point2d>{
        { 0, 0 }, { 4, 0 }, { 2, 2 }, { 4, 4 }, { 0, 4 } }).convexHull().area(), 16.0), "折线凸包 16");

    const shapely::BoundingBox2d bb = Geom::createCircle(0, 0, 10.0).bounds();
    check(near(bb.width(), 20.0, 1e-3) && near(bb.height(), 20.0, 1e-3), "圆包围盒 20x20");
    check(Geom::createCircle(0, 0, 10.0).getExteriorPoints().size() >= 8, "圆顶点数 ≥ 8");

    const Point2d ct = Geom::createPolygon(
        std::vector<Point2d>{ { 0, 0 }, { 4, 0 }, { 4, 4 }, { 0, 4 } }).centroid();
    check(near(ct.x, 2.0) && near(ct.y, 2.0), "质心 (2,2)");

    Geom moved = Geom::createCircle(0, 0, 1.0);
    const Geom target = std::move(moved);
    check(moved.isEmpty() && near(target.area(), M_PI, 1e-3), "移动语义");

    check(Geom::fromWKT("POINT EMPTY").isEmpty() && Geom::fromWKT("BOGUS").isEmpty(), "空对象安全");
}

// ---------------- Stereo ----------------

static void testStereo()
{
    std::printf("== Stereo ==\n");
    // 立方体角点（8 点）
    const std::vector<Point3d> cube = {
        { 0, 0, 0 }, { 4, 0, 0 }, { 4, 4, 0 }, { 0, 4, 0 },
        { 0, 0, 4 }, { 4, 0, 4 }, { 4, 4, 4 }, { 0, 4, 4 },
    };
    const Stereo s = Stereo::createFromPoints(cube);
    check(s.size() == 8 && !s.isEmpty(), "createFromPoints 8 点");

    const Stereo sFlat = Stereo::createFromFlatArray(
        std::vector<double>{ 0, 0, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0,
                             0, 0, 4, 4, 0, 4, 4, 4, 4, 0, 4, 4 });
    check(sFlat.size() == 8, "createFromFlatArray 8 点");
    const Stereo sNest = Stereo::createFromNestedArray(
        std::vector<std::vector<double>>{ { 0, 0, 0 }, { 1, 1, 1 }, { 2, 2, 2 } });
    check(sNest.size() == 3, "createFromNestedArray 3 点");
    const Stereo sMy = Stereo::createFromPoints(
        std::vector<MyPt3>{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } });
    check(sMy.size() == 3, "createFromPoints(自定义结构)");

    // AABB
    const shapely::AABB3d aabb = s.getAABB();
    check(aabb.valid && near(aabb.volume(), 64.0), "AABB 体积 64");
    check(near(s.getAABBVolume(), 64.0), "getAABBVolume");
    // OBB（立方体 PCA：体积 = 64，半长 2）
    const shapely::OBB3d obb = s.getOBB();
    check(obb.valid && near(obb.volume(), 64.0, 1e-6), "OBB 体积 64");
    check(near(obb.extents[0], 2.0, 1e-6) && near(obb.extents[1], 2.0, 1e-6) &&
          near(obb.extents[2], 2.0, 1e-6), "OBB 半长 2");

    // AABB/OBB 关系
    const shapely::AABB3d inner{ 1, 1, 1, 3, 3, 3, true };
    check(aabb.contains(inner) && inner.intersects(aabb) && !inner.contains(aabb),
          "AABB contains/intersects");
    const shapely::AABB3d far{ 10, 10, 10, 12, 12, 12, true };
    check(!aabb.intersects(far), "AABB 不相交");
    check(obb.intersects(shapely::OBB3d{ Point3d{ 3.9, 0, 0 }, { 2, 2, 2 },
                                         { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, true }), "OBB 相交（平移小盒）");
    check(!obb.intersects(shapely::OBB3d{ Point3d{ 10, 0, 0 }, { 2, 2, 2 },
                                          { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, true }), "OBB 不相交（远盒）");

    // createFromBox
    const Stereo fromBox = Stereo::createFromBox(shapely::AABB3d{ 0, 0, 0, 2, 2, 2, true });
    check(fromBox.size() == 8 && near(fromBox.getAABB().volume(), 8.0), "createFromBox(AABB)");
    const Stereo fromBox2 = Stereo::createFromBox(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
    check(fromBox2.size() == 8 && near(fromBox2.getAABB().volume(), 1.0), "createFromBox(边界值)");
    const Stereo fromOBB = Stereo::createFromBox(obb);
    check(fromOBB.size() == 8 && near(fromOBB.getAABB().volume(), 64.0, 1e-6), "createFromBox(OBB)");

    // Z 过滤
    double zmin = 0, zmax = 0;
    s.getZBounds(zmin, zmax);
    check(zmin == 0.0 && zmax == 4.0, "getZBounds [0,4]");
    const Stereo low = s.filterByZRange(0.0, 0.0);
    check(low.size() == 4, "filterByZRange z=0 → 4 点");

    // 距离/最近点对/重合
    const Stereo near2 = Stereo::createFromPoints(std::vector<Point3d>{ { 5, 0, 0 }, { 6, 0, 0 } });
    check(near(s.minDistanceTo(near2), 1.0), "minDistanceTo = 1");
    Point3d pa, pb;
    s.findClosestPair(near2, pa, pb);
    check(pa.x == 4.0 && pb.x == 5.0, "findClosestPair (4,0,0)-(5,0,0)");
    check(s.hasCoincidentPointWith(Stereo::createFromPoints(
        std::vector<Point3d>{ { 99, 99, 99 }, { 4, 0, 0 } }), 1e-9), "hasCoincidentPointWith");
    check(!s.hasCoincidentPointWith(Stereo::createFromPoints(
        std::vector<Point3d>{ { 99, 99, 99 } }), 1e-9), "无重合点");

    // 2.5D 桥接：立方体 XY 投影 = 4x4 正方形
    const Geom proj = s.projectTo2D();
    check(near(proj.area(), 16.0, 1e-6), "projectTo2D 凸包面积 16");

    // 移动语义
    Stereo m = Stereo::createFromPoints(cube);
    const Stereo t = std::move(m);
    check(m.isEmpty() && t.size() == 8, "移动语义");
    check(Stereo::createFromPoints(std::vector<Point3d>()).isEmpty() &&
          Stereo::createFromPoints(std::vector<Point3d>()).minDistanceTo(s) == -1.0, "空对象安全");
}

// ---------------- ImageProcessor ----------------

static void testImageProcessor()
{
    std::printf("== ImageProcessor ==\n");
    // 100x100 灰度图，左半 200 右半 50
    cv::Mat img(100, 100, CV_8UC1, cv::Scalar(50));
    img(cv::Rect(0, 0, 50, 100)).setTo(200);

    cv::Mat bin;
    shapely::ImageProcessor::threshold(img, bin, 100, 255, cv::THRESH_BINARY);
    check((int)cv::sum(bin)[0] == 50 * 100 * 255, "threshold 左半二值化");

    cv::Mat blur;
    shapely::ImageProcessor::gaussianBlur(img, blur, cv::Size(5, 5), 1.0);
    check(!blur.empty(), "gaussianBlur");

    cv::Mat edges;
    shapely::ImageProcessor::canny(img, edges, 50, 150);
    check(cv::countNonZero(edges) > 0, "canny 边缘非空");

    // 轮廓 → Geom 联动（左半矩形 → 面积 50x100）
    std::vector<std::vector<cv::Point>> contours;
    shapely::ImageProcessor::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    check(!contours.empty(), "findContours 非空");
    if (!contours.empty()) {
        // 全局坐标直接进 Geom（鸭子类型，无需转换）
        const Geom poly = Geom::createPolygon(contours[0]);
        check(near(poly.area(), 49.0 * 99.0, 1.0), "轮廓→Geom 面积 49x99（像素中心）");
        const cv::Rect br = shapely::ImageProcessor::getBoundingRect(contours[0]);
        check(br.width == 50 && br.height == 100, "getBoundingRect 50x100");
    }

    // 模板匹配：在右半找 10x10 的 50 值模板
    cv::Mat templ(10, 10, CV_8UC1, cv::Scalar(50));
    cv::Mat result;
    shapely::ImageProcessor::matchTemplate(img, templ, result, cv::TM_SQDIFF);
    cv::Point loc;
    double score = 0;
    shapely::ImageProcessor::findBestMatch(result, cv::TM_SQDIFF, loc, score);
    check(loc.x >= 50 && loc.x <= 90, "findBestMatch 定位右半");

    // 瓦片：100x100 切 40x40 无重叠 → 3x3=9 块（covered_roi 处理边界）
    std::vector<shapely::Tile> tiles;
    shapely::ImageProcessor::splitIntoTiles(img, cv::Size(40, 40), 0, tiles);
    check(tiles.size() == 9, "splitIntoTiles 3x3=9");
    if (tiles.size() == 9) {
        const shapely::Tile& last = tiles.back();
        check(last.covered_roi.width == 20 && last.covered_roi.height == 20,
              "边界瓦片 covered_roi 20x20");
        check(last.image.cols == 20 && last.image.rows == 20, "瓦片图像尺寸 = covered_roi");
    }

    // 金字塔瓦片：2 层
    std::vector<std::vector<shapely::Tile>> pyramid;
    shapely::ImageProcessor::buildTilePyramid(img, cv::Size(40, 40), 1, pyramid);
    check(pyramid.size() == 2, "buildTilePyramid 2 层");
    if (pyramid.size() == 2) {
        check(near(pyramid[1][0].scale, 0.5), "level1 scale 0.5");
        check(pyramid[1][0].roi_base.width == 80, "level1 roi_base 映射回 level0");
    }

    // 金字塔
    std::vector<cv::Mat> pyr;
    shapely::ImageProcessor::buildGaussianPyramid(img, pyr, 2);
    check(pyr.size() == 3 && pyr[2].cols == 25, "buildGaussianPyramid 3 层 25 宽");

    // 零拷贝验证：瓦片改值会影响源图（共享内存）
    if (!tiles.empty()) {
        tiles[0].image.at<uchar>(0, 0) = 255;
        check(img.at<uchar>(0, 0) == 255, "瓦片零拷贝（共享内存）");
    }
}

// ---------------- CloudProcessor ----------------

static void testCloudProcessor()
{
    std::printf("== CloudProcessor ==\n");
    using CP = shapely::CloudProcessor<pcl::PointXYZ>;
    using Cloud = pcl::PointCloud<pcl::PointXYZ>;
    using CloudPtr = Cloud::Ptr;

    // 平面点云（z=0 的 10x10 网格）+ 离群点
    CloudPtr plane(new Cloud);
    for (int x = 0; x < 10; ++x)
        for (int y = 0; y < 10; ++y)
            plane->push_back(pcl::PointXYZ((float)x, (float)y, 0.0f));
    plane->push_back(pcl::PointXYZ(50.0f, 50.0f, 50.0f));   // 离群点
    plane->width = (uint32_t)plane->size();
    plane->height = 1;
    plane->is_dense = true;

    // 体素滤波（leaf 0.5 → 网格不变但离群点保留；用 1.0 验证降采样）
    CloudPtr voxel = CP::filterVoxelGrid(plane, 1.0);
    check(voxel && voxel->size() == 101, "filterVoxelGrid 1.0 → 101（网格合并+离群）");

    // 直通滤波：z ∈ [-0.1, 0.1] → 100 点（去离群）
    CloudPtr pt = CP::filterPassThrough(plane, "z", -0.1, 0.1);
    check(pt && pt->size() == 100, "filterPassThrough z → 100");

    // 统计离群去除
    CloudPtr sor = CP::filterStatisticalOutlier(plane, 20, 1.0);
    check(sor && sor->size() == 100, "filterStatisticalOutlier → 100（去离群）");

    // 半径离群
    CloudPtr ror = CP::filterRadiusOutlier(plane, 1.5, 3);
    check(ror && ror->size() == 100, "filterRadiusOutlier → 100");

    // 条件滤波 XYZ
    CloudPtr cond = CP::filterConditionalXYZ(plane, 0, 5, 0, 5, -1, 1);
    check(cond && cond->size() == 16, "filterConditionalXYZ (0,5) 开区间 → 16");

    // 索引提取
    CloudPtr ext = CP::extractByIndices(plane, { 0, 1, 2 });
    check(ext && ext->size() == 3, "extractByIndices 3");

    // 变换：平移
    CloudPtr moved = CP::transTranslate(plane, 1.0, 0, 0);
    check(moved && near((*moved)[0].x, 1.0, 1e-6), "transTranslate x+1");
    // 变换：矩阵（单位阵）
    const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    CloudPtr ident = CP::transApplyMatrix(plane, I);
    check(ident && near((*ident)[0].x, (*plane)[0].x, 1e-6), "transApplyMatrix 单位阵");
    // 变换：投影到 z=0 平面
    const float planeEq[4] = { 0, 0, 1, 0 };
    CloudPtr proj = CP::transProject(plane, planeEq);
    check(proj && near((*proj)[100].z, 0.0, 1e-6), "transProject 离群点投回 z=0");
    // 变换：绕 z 轴 90°
    CloudPtr rot = CP::transRotate(plane, 0, 0, 1, M_PI / 2);
    // plane[1] = (0,1,0)：绕 +z 90° → (-1,0,0)
    check(rot && near((*rot)[1].x, -1.0, 1e-5) && near((*rot)[1].y, 0.0, 1e-5),
          "transRotate 绕z 90°");

    // 法线估计（平面 → 法向 z）
    auto normals = CP::featureNormalEstimation(pt, 10);
    check(normals && !normals->empty() &&
          near((*normals)[0].normal_z, 1.0, 0.1), "featureNormalEstimation 法向 z");

    // 平面拟合
    std::vector<float> coeffs;
    std::vector<int> inliers;
    check(CP::fitPlane(plane, shapely::FitMethod::RANSAC, 0.01, coeffs, inliers) &&
          inliers.size() == 100, "fitPlane 100 局内点");
    check(coeffs.size() >= 4 && near(std::fabs(coeffs[2]), 1.0, 0.01), "fitPlane 法向 z");

    // 平面分割
    CloudPtr inCloud, outCloud;
    check(CP::segmentPlaneRANSAC(plane, 0.01, inCloud, outCloud, coeffs) &&
          inCloud->size() == 100 && outCloud->size() == 1, "segmentPlaneRANSAC 100+1");

    // 欧式聚类：平面网格（间距 1，容差 1.5 → 全连通 1 簇）+ 远点单独 1 簇
    std::vector<CloudPtr> clusters = CP::segmentEuclideanCluster(plane, 1.5, 1, 10000);
    check(clusters.size() == 2 && clusters[0]->size() == 100, "segmentEuclideanCluster 2 簇");

    // KdTree KNN
    std::vector<int> knnIdx;
    std::vector<float> knnDist;
    const float q[3] = { 0.0f, 0.0f, 0.0f };
    CP::treeKdTreeKNN(plane, q, 3, knnIdx, knnDist);
    check(knnIdx.size() == 3 && knnIdx[0] >= 0 && near(knnDist[0], 0.0, 1e-6),
          "treeKdTreeKNN 最近点距离 0");

    // KdTree 半径搜索
    std::vector<int> radIdx;
    std::vector<float> radDist;
    CP::treeKdTreeRadiusSearch(plane, q, 1.5, radIdx, radDist);
    check(radIdx.size() == 4, "treeKdTreeRadiusSearch r=1.5 → 4 点（含自身+对角）");

    // Octree 体素搜索
    std::vector<int> voxIdx;
    CP::treeOctreeVoxelSearch(plane, q, voxIdx, 1.0);
    check(!voxIdx.empty(), "treeOctreeVoxelSearch 非空");

    // Octree 变化检测：新点云多一个点
    CloudPtr plane2(new Cloud(*plane));
    plane2->push_back(pcl::PointXYZ(20.0f, 20.0f, 0.0f));
    plane2->width = (uint32_t)plane2->size();
    std::vector<int> newIdx;
    CP::treeOctreeChangeDetection(plane, plane2, 1.0, newIdx);
    check(!newIdx.empty(), "treeOctreeChangeDetection 检出新增");

    // 凸包（平面网格 → 2D 凸包 4 角）
    CloudPtr hull = CP::surfaceConvexHull(pt);
    check(hull && hull->size() == 4, "surfaceConvexHull 平面 → 4 角点");

    // ICP：平移后的平面配回原位
    CloudPtr shifted = CP::transTranslate(pt, 0.5, 0, 0);
    float T[16] = { 0 };
    check(CP::registICP_PointToPoint(shifted, pt, 2.0, T) &&
          near(T[3], -0.5, 0.05), "registICP_PointToPoint 平移恢复");
    // PCA 配准
    check(CP::registPCA(shifted, pt, T) && near(T[3], -0.5, 0.1), "registPCA 平移恢复");

    // Harris / ISS 关键点（平面无角点，仅验证不崩）
    CloudPtr harris = CP::keypointHarris3D(pt, 1.0, 1e-4);
    check(harris != nullptr, "keypointHarris3D 运行");
    CloudPtr iss = CP::keypointISS3D(pt, 1.5, 1.0);
    check(iss != nullptr, "keypointISS3D 运行");

    // CloudIO：TXT 读写回环
    const std::string txtPath = "D:/pmy/alg/output/_shapely_cloud_test.txt";
    check(shapely::CloudIO<pcl::PointXYZ>::ioSaveTXT(txtPath, pt, " "), "ioSaveTXT");
    CloudPtr loaded = shapely::CloudIO<pcl::PointXYZ>::ioLoadTXT(txtPath, " ", 0, 1, 2);
    check(loaded && loaded->size() == pt->size(), "ioLoadTXT 回环 100 点");

    // PCD 回环
    const std::string pcdPath = "D:/pmy/alg/output/_shapely_cloud_test.pcd";
    check(shapely::CloudIO<pcl::PointXYZ>::ioSavePCD(pcdPath, pt), "ioSavePCD");
    CloudPtr pcd = shapely::CloudIO<pcl::PointXYZ>::ioLoadPCD(pcdPath);
    check(pcd && pcd->size() == pt->size(), "ioLoadPCD 回环 100 点");

    // PLY 回环
    const std::string plyPath = "D:/pmy/alg/output/_shapely_cloud_test.ply";
    check(shapely::CloudIO<pcl::PointXYZ>::ioSavePLY(plyPath, pt, true), "ioSavePLY");
    CloudPtr ply = shapely::CloudIO<pcl::PointXYZ>::ioLoadPLY(plyPath);
    check(ply && ply->size() == pt->size(), "ioLoadPLY 回环 100 点");

    std::remove(txtPath.c_str());
    std::remove(pcdPath.c_str());
    std::remove(plyPath.c_str());
}

int main()
{
    testGeom();
    testStereo();
    testImageProcessor();
    testCloudProcessor();
    std::printf("\n%s（失败 %d 项）\n", g_fail == 0 ? "全部测试通过" : "存在失败", g_fail);
    return g_fail == 0 ? 0 : 1;
}
