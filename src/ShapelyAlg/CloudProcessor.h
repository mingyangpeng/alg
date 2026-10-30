#ifndef SHAPELY_CLOUD_PROCESSOR_H
#define SHAPELY_CLOUD_PROCESSOR_H

// shapely::CloudProcessor —— 三维点云基础算子模块（PCL 封装，显式实例化）。
// 设计约束（见技术交底书）：
//   * 公共头文件仅允许 <pcl/point_types.h> + <pcl/point_cloud.h>（容器定义），
//     严禁算法级重头文件（registration/keypoints/octree/...）；
//   * 复杂算法类（KdTree/Octree）前置声明，输出矩阵统一 float[16]（不暴露 Eigen）；
//   * 库原生枚举 shapely::FitMethod（RANSAC/LMEDS/MSAC），底层映射 pcl::SAC_*
//     （注意：PCL 的 LMEDS/MSAC 在 SACMODEL 平面/球拟合中支持有限，底层做能力回退）；
//   * 显式实例化白名单：pcl::PointXYZ / PointXYZI / PointXYZRGB（.cpp 内），
//     业务层直接链接已编译机器码，无模板传染；
//   * 零拷贝：pcl::PointCloud<PointT>::Ptr 流转；
//   * 命名规范：分类前缀 + 动作（filter/extract/trans/feature/keypoint/fit/
//     segment/regist/tree/surface），camelCase。

#include <pcl/point_types.h>
#include <pcl/point_cloud.h>

#include "ShapelyAlg_export.h"      // 自动生成的导出宏

#include <string>
#include <vector>

namespace shapely {

// 拟合方法（库原生枚举，底层映射 PCL SAC 花样）
enum class FitMethod {
    RANSAC = 0,
    LMEDS  = 1,
    MSAC   = 2,
};

template <typename PointT>
class SHAPELYALG_EXPORT CloudProcessor
{
public:
    CloudProcessor() = delete;   // 纯静态工具类

    using CloudPtr = typename pcl::PointCloud<PointT>::Ptr;
    using CloudConstPtr = typename pcl::PointCloud<PointT>::ConstPtr;

    // ---------------- filter：滤波与降采样 ----------------
    static CloudPtr filterVoxelGrid(const CloudConstPtr& src, double leaf_size);
    static CloudPtr filterPassThrough(const CloudConstPtr& src, const std::string& field_name,
                                      double limit_min, double limit_max);
    static CloudPtr filterStatisticalOutlier(const CloudConstPtr& src, int mean_k, double std_dev_mul);
    static CloudPtr filterRadiusOutlier(const CloudConstPtr& src, double radius, int min_neighbors);
    // 条件滤波：XYZ 范围（GT/LT 严格比较，即开区间 (min, max)）
    static CloudPtr filterConditionalXYZ(const CloudConstPtr& src,
                                         double x_min, double x_max,
                                         double y_min, double y_max,
                                         double z_min, double z_max);

    // ---------------- extract：点云提取 ----------------
    static CloudPtr extractByIndices(const CloudConstPtr& src, const std::vector<int>& indices);

    // ---------------- trans：空间变换与投影 ----------------
    // matrix_4x4：行主序刚体变换矩阵（Eigen::Matrix4f 兼容布局）
    static CloudPtr transApplyMatrix(const CloudConstPtr& src, const float matrix_4x4[16]);
    // plane_coeffs：[a,b,c,d]（ax+by+cz+d=0）
    static CloudPtr transProject(const CloudConstPtr& src, const float plane_coeffs[4]);
    static CloudPtr transTranslate(const CloudConstPtr& src, double dx, double dy, double dz);
    // 绕轴（axis 过原点方向向量）旋转 angle 弧度
    static CloudPtr transRotate(const CloudConstPtr& src, double axis_x, double axis_y,
                                double axis_z, double angle);

    // ---------------- feature：特征提取 ----------------
    // 返回法线点云（xyz = 法向，w = 曲率），与输入点一一对应
    static pcl::PointCloud<pcl::Normal>::Ptr featureNormalEstimation(const CloudConstPtr& src, int k_search);

    // ---------------- keypoint：关键点检测 ----------------
    static CloudPtr keypointHarris3D(const CloudConstPtr& src, double radius, double threshold);
    static CloudPtr keypointISS3D(const CloudConstPtr& src, double salient_radius, double non_max_radius);

    // ---------------- fit：稳健模型拟合 ----------------
    // fitPlane：out_coeffs = [a,b,c,d]；out_inliers = 局内点索引
    static bool fitPlane(const CloudConstPtr& src, FitMethod method, double distance_threshold,
                         std::vector<float>& out_coeffs, std::vector<int>& out_inliers);
    // fitSphere：out_center = [x,y,z]
    static bool fitSphere(const CloudConstPtr& src, FitMethod method, double distance_threshold,
                          std::vector<float>& out_center, double& out_radius,
                          std::vector<int>& out_inliers);
    // fitCylinder：out_axis = [dx,dy,dz]（单位向量），out_point = [x,y,z]（轴上一点）
    static bool fitCylinder(const CloudConstPtr& src, FitMethod method, double distance_threshold,
                            std::vector<float>& out_axis, std::vector<float>& out_point,
                            double& out_radius, std::vector<int>& out_inliers);

    // ---------------- segment：分割与聚类 ----------------
    static bool segmentPlaneRANSAC(const CloudConstPtr& src, double distance_threshold,
                                   CloudPtr& out_inliers, CloudPtr& out_outliers,
                                   std::vector<float>& out_coeffs);
    // 欧式聚类：输出多个独立点云簇（按点数降序）
    static std::vector<CloudPtr> segmentEuclideanCluster(const CloudConstPtr& src,
                                                         double cluster_tolerance,
                                                         int min_size, int max_size);

    // ---------------- regist：配准与位姿估计 ----------------
    // out_transform：行主序 4x4
    static bool registICP_PointToPoint(const CloudConstPtr& source, const CloudConstPtr& target,
                                       double max_dist, float out_transform[16]);
    static bool registICP_PointToPlane(const CloudConstPtr& source, const CloudConstPtr& target,
                                       const pcl::PointCloud<pcl::Normal>::Ptr& target_normals,
                                       double max_dist, float out_transform[16]);
    static bool registNDT(const CloudConstPtr& source, const CloudConstPtr& target,
                          double voxel_size, float out_transform[16]);
    static bool registPCA(const CloudConstPtr& source, const CloudConstPtr& target,
                          float out_transform[16]);

    // ---------------- tree：空间索引与近邻搜索 ----------------
    // query_point = [x,y,z]
    static void treeKdTreeKNN(const CloudConstPtr& src, const float query_point[3], int k,
                              std::vector<int>& out_indices, std::vector<float>& out_distances);
    static void treeKdTreeRadiusSearch(const CloudConstPtr& src, const float query_point[3],
                                       double radius, std::vector<int>& out_indices,
                                       std::vector<float>& out_distances);
    // Octree 体素搜索：返回查询点所在体素内的全部点索引。
    // resolution：体素边长（与点云同单位；默认 1.0 适合米制，mm/像素单位请显式传）
    static void treeOctreeVoxelSearch(const CloudConstPtr& src, const float query_point[3],
                                      std::vector<int>& out_indices,
                                      double resolution = 1.0);
    // Octree 变化检测：返回 src2 相对 src1 新增（变化）的点索引
    static void treeOctreeChangeDetection(const CloudConstPtr& src1, const CloudConstPtr& src2,
                                          double min_resolution, std::vector<int>& out_new_indices);

    // ---------------- surface：表面重建 ----------------
    static CloudPtr surfaceConvexHull(const CloudConstPtr& src);
};

} // namespace shapely

#endif // SHAPELY_CLOUD_PROCESSOR_H
