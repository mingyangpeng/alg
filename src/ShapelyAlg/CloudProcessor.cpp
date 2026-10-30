// CloudProcessor.cpp —— PCL 点云算子实现。
// 显式实例化白名单：pcl::PointXYZ / PointXYZI / PointXYZRGB（文末）。
// 注意（PCL 1.9 + MSVC 踩坑记录）：
//   * 算法重头文件（filters/segmentation/registration/...）只在本 .cpp include，
//     公共头零模板传染；
//   * LMEDS/MSAC 在部分 SAC 模型（球/圆柱）不支持，底层回退 RANSAC；
//   * Harris3D/ISS 需要 pcl::PointCloud<pcl::PointXYZ> 中转（只支持 XYZ 输入）；
//   * PCL 点型不能聚合初始化/互赋值（PointXYZRGB），用 makeQueryPoint 逐字段。

#include "CloudProcessor.h"

#include <pcl/filters/voxel_grid.h>
#include <pcl/filters/passthrough.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/filters/radius_outlier_removal.h>
#include <pcl/filters/conditional_removal.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/common/transforms.h>
#include <pcl/common/io.h>
#include <pcl/features/normal_3d.h>
#include <pcl/keypoints/harris_3d.h>
#include <pcl/keypoints/iss_3d.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/segmentation/extract_clusters.h>
#include <pcl/registration/icp.h>
#include <pcl/registration/icp_nl.h>
#include <pcl/registration/ndt.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/octree/octree_search.h>
#include <pcl/octree/octree_pointcloud_changedetector.h>
#include <pcl/surface/convex_hull.h>
#include <pcl/common/centroid.h>

#include <Eigen/Dense>

#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace shapely {

// ---------------- 内部工具 ----------------

namespace {

// shapely::FitMethod → pcl::SACMethods（LMEDS/MSAC 部分模型不支持，回退 RANSAC）
int toSacMethod(FitMethod m)
{
    switch (m) {
    case FitMethod::LMEDS: return pcl::SAC_LMEDS;
    case FitMethod::MSAC:  return pcl::SAC_MSAC;
    default:                return pcl::SAC_RANSAC;
    }
}

// Eigen 4x4 → 行主序 float[16]
template <typename M>
void matToArray(const M& m, float out[16])
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out[r * 4 + c] = m(r, c);
}

// 查询点构造：逐字段赋值（PointXYZRGB 等带额外字段的点型不能聚合初始化，
// 显式实例化白名单要求所有代码路径对全部白名单点型可编译）
template <typename PointT>
PointT makeQueryPoint(const float q[3])
{
    PointT p;
    p.x = q[0];
    p.y = q[1];
    p.z = q[2];
    return p;
}

} // namespace

// ---------------- filter ----------------

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::filterVoxelGrid(const CloudConstPtr& src, double leaf_size)
{
    if (!src || leaf_size <= 0)
        return nullptr;
    pcl::VoxelGrid<PointT> f;
    f.setInputCloud(src);
    f.setLeafSize((float)leaf_size, (float)leaf_size, (float)leaf_size);
    CloudPtr out(new pcl::PointCloud<PointT>);
    f.filter(*out);
    return out;
}

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::filterPassThrough(const CloudConstPtr& src, const std::string& field_name,
                                          double limit_min, double limit_max)
{
    if (!src)
        return nullptr;
    pcl::PassThrough<PointT> f;
    f.setInputCloud(src);
    f.setFilterFieldName(field_name);
    f.setFilterLimits((float)limit_min, (float)limit_max);
    CloudPtr out(new pcl::PointCloud<PointT>);
    f.filter(*out);
    return out;
}

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::filterStatisticalOutlier(const CloudConstPtr& src, int mean_k, double std_dev_mul)
{
    if (!src)
        return nullptr;
    pcl::StatisticalOutlierRemoval<PointT> f;
    f.setInputCloud(src);
    f.setMeanK(mean_k);
    f.setStddevMulThresh(std_dev_mul);
    CloudPtr out(new pcl::PointCloud<PointT>);
    f.filter(*out);
    return out;
}

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::filterRadiusOutlier(const CloudConstPtr& src, double radius, int min_neighbors)
{
    if (!src)
        return nullptr;
    pcl::RadiusOutlierRemoval<PointT> f;
    f.setInputCloud(src);
    f.setRadiusSearch(radius);
    f.setMinNeighborsInRadius(min_neighbors);
    CloudPtr out(new pcl::PointCloud<PointT>);
    f.filter(*out);
    return out;
}

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::filterConditionalXYZ(const CloudConstPtr& src,
                                             double x_min, double x_max,
                                             double y_min, double y_max,
                                             double z_min, double z_max)
{
    if (!src)
        return nullptr;
    // 条件：各轴范围（GT/LT 严格比较，即开区间）
    pcl::ConditionAnd<PointT>::Ptr cond(new pcl::ConditionAnd<PointT>);
    auto addRange = [&cond](const std::string& field, double lo, double hi) {
        pcl::FieldComparison<PointT>::Ptr c(new pcl::FieldComparison<PointT>(
            field, pcl::ComparisonOps::GT, (float)lo));
        pcl::FieldComparison<PointT>::Ptr c2(new pcl::FieldComparison<PointT>(
            field, pcl::ComparisonOps::LT, (float)hi));
        cond->addComparison(c);
        cond->addComparison(c2);
    };
    addRange("x", x_min, x_max);
    addRange("y", y_min, y_max);
    addRange("z", z_min, z_max);
    pcl::ConditionalRemoval<PointT> f;
    f.setInputCloud(src);
    f.setCondition(cond);
    CloudPtr out(new pcl::PointCloud<PointT>);
    f.filter(*out);
    return out;
}

// ---------------- extract ----------------

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::extractByIndices(const CloudConstPtr& src, const std::vector<int>& indices)
{
    if (!src || indices.empty())
        return nullptr;
    pcl::PointIndices::Ptr sel(new pcl::PointIndices);
    sel->indices = indices;
    pcl::ExtractIndices<PointT> f;
    f.setInputCloud(src);
    f.setIndices(sel);
    CloudPtr out(new pcl::PointCloud<PointT>);
    f.filter(*out);
    return out;
}

// ---------------- trans ----------------

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::transApplyMatrix(const CloudConstPtr& src, const float matrix_4x4[16])
{
    if (!src)
        return nullptr;
    Eigen::Matrix4f m;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            m(r, c) = matrix_4x4[r * 4 + c];
    CloudPtr out(new pcl::PointCloud<PointT>);
    pcl::transformPointCloud(*src, *out, m);
    return out;
}

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::transProject(const CloudConstPtr& src, const float plane_coeffs[4])
{
    // 投影：p' = p - (n·p + d)/|n|² · n
    if (!src)
        return nullptr;
    const double a = plane_coeffs[0], b = plane_coeffs[1], c = plane_coeffs[2], d = plane_coeffs[3];
    const double n2 = a * a + b * b + c * c;
    if (n2 <= 0)
        return nullptr;
    CloudPtr out(new pcl::PointCloud<PointT>);
    out->resize(src->size());
    for (size_t i = 0; i < src->size(); ++i) {
        const PointT& p = (*src)[i];
        const double t = (a * p.x + b * p.y + c * p.z + d) / n2;
        PointT& q = (*out)[i];
        q.x = (float)((double)p.x - t * a);
        q.y = (float)((double)p.y - t * b);
        q.z = (float)((double)p.z - t * c);
    }
    return out;
}

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::transTranslate(const CloudConstPtr& src, double dx, double dy, double dz)
{
    if (!src)
        return nullptr;
    CloudPtr out(new pcl::PointCloud<PointT>);
    out->resize(src->size());
    for (size_t i = 0; i < src->size(); ++i) {
        (*out)[i] = (*src)[i];
        (*out)[i].x += (float)dx;
        (*out)[i].y += (float)dy;
        (*out)[i].z += (float)dz;
    }
    return out;
}

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::transRotate(const CloudConstPtr& src, double axis_x, double axis_y,
                                    double axis_z, double angle)
{
    if (!src)
        return nullptr;
    const double len = std::sqrt(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
    if (len < 1e-12)
        return nullptr;
    Eigen::Affine3f t = Eigen::Affine3f::Identity();
    t.rotate(Eigen::AngleAxisf((float)angle,
                               Eigen::Vector3f((float)(axis_x / len),
                                               (float)(axis_y / len),
                                               (float)(axis_z / len))));
    CloudPtr out(new pcl::PointCloud<PointT>);
    pcl::transformPointCloud(*src, *out, t);
    return out;
}

// ---------------- feature ----------------

template <typename PointT>
pcl::PointCloud<pcl::Normal>::Ptr
CloudProcessor<PointT>::featureNormalEstimation(const CloudConstPtr& src, int k_search)
{
    if (!src)
        return nullptr;
    pcl::NormalEstimation<PointT, pcl::Normal> ne;
    ne.setInputCloud(src);
    typename pcl::search::KdTree<PointT>::Ptr tree(new pcl::search::KdTree<PointT>);
    ne.setSearchMethod(tree);
    ne.setKSearch(k_search);
    pcl::PointCloud<pcl::Normal>::Ptr out(new pcl::PointCloud<pcl::Normal>);
    ne.compute(*out);
    return out;
}

// ---------------- keypoint ----------------

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::keypointHarris3D(const CloudConstPtr& src, double radius, double threshold)
{
    // Harris3D 只支持 PointXYZ 输入（内部用强度），先转 XYZ
    if (!src || src->empty())
        return nullptr;
    pcl::PointCloud<pcl::PointXYZ>::Ptr xyz(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::copyPointCloud(*src, *xyz);
    pcl::HarrisKeypoint3D<pcl::PointXYZ, pcl::PointXYZI> harris;
    harris.setInputCloud(xyz);
    harris.setRadius((float)radius);
    harris.setThreshold((float)threshold);
    harris.setNonMaxSupression(true);
    pcl::PointCloud<pcl::PointXYZI>::Ptr kp(new pcl::PointCloud<pcl::PointXYZI>);
    harris.compute(*kp);
    CloudPtr out(new pcl::PointCloud<PointT>);
    out->resize(kp->size());
    for (size_t i = 0; i < kp->size(); ++i) {
        (*out)[i].x = (*kp)[i].x;
        (*out)[i].y = (*kp)[i].y;
        (*out)[i].z = (*kp)[i].z;
    }
    return out;
}

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::keypointISS3D(const CloudConstPtr& src, double salient_radius, double non_max_radius)
{
    if (!src || src->empty())
        return nullptr;
    pcl::PointCloud<pcl::PointXYZ>::Ptr xyz(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::copyPointCloud(*src, *xyz);
    typename pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>);
    pcl::ISSKeypoint3D<pcl::PointXYZ, pcl::PointXYZ> iss;
    iss.setInputCloud(xyz);
    iss.setSearchMethod(tree);
    iss.setSalientRadius((float)salient_radius);
    iss.setNonMaxRadius((float)non_max_radius);
    pcl::PointCloud<pcl::PointXYZ> kp;
    iss.compute(kp);
    CloudPtr out(new pcl::PointCloud<PointT>);
    out->resize(kp.size());
    for (size_t i = 0; i < kp.size(); ++i) {
        PointT p;
        p.x = kp[i].x; p.y = kp[i].y; p.z = kp[i].z;
        (*out)[i] = p;
    }
    return out;
}

// ---------------- fit ----------------

template <typename PointT>
bool CloudProcessor<PointT>::fitPlane(const CloudConstPtr& src, FitMethod method,
                                      double distance_threshold,
                                      std::vector<float>& out_coeffs, std::vector<int>& out_inliers)
{
    if (!src)
        return false;
    pcl::SACSegmentation<PointT> seg;
    seg.setInputCloud(src);
    seg.setModelType(pcl::SACMODEL_PLANE);
    seg.setMethodType(toSacMethod(method));
    seg.setDistanceThreshold(distance_threshold);
    seg.setOptimizeCoefficients(true);
    pcl::PointIndices inliers;
    pcl::ModelCoefficients coeffs;
    seg.segment(inliers, coeffs);
    if (inliers.indices.empty())
        return false;
    out_coeffs = coeffs.values;
    out_inliers = inliers.indices;
    return true;
}

template <typename PointT>
bool CloudProcessor<PointT>::fitSphere(const CloudConstPtr& src, FitMethod method,
                                       double distance_threshold,
                                       std::vector<float>& out_center, double& out_radius,
                                       std::vector<int>& out_inliers)
{
    if (!src)
        return false;
    pcl::SACSegmentation<PointT> seg;
    seg.setInputCloud(src);
    seg.setModelType(pcl::SACMODEL_SPHERE);
    // PCL 1.9：球模型仅支持 RANSAC/LMEDS；MSAC 回退 RANSAC
    seg.setMethodType(method == FitMethod::MSAC ? (int)pcl::SAC_RANSAC : toSacMethod(method));
    seg.setDistanceThreshold(distance_threshold);
    seg.setOptimizeCoefficients(true);
    pcl::PointIndices inliers;
    pcl::ModelCoefficients coeffs;
    seg.segment(inliers, coeffs);
    if (inliers.indices.empty() || coeffs.values.size() < 4)
        return false;
    out_center.assign(coeffs.values.begin(), coeffs.values.begin() + 3);
    out_radius = coeffs.values[3];
    out_inliers = inliers.indices;
    return true;
}

template <typename PointT>
bool CloudProcessor<PointT>::fitCylinder(const CloudConstPtr& src, FitMethod method,
                                         double distance_threshold,
                                         std::vector<float>& out_axis, std::vector<float>& out_point,
                                         double& out_radius, std::vector<int>& out_inliers)
{
    if (!src)
        return false;
    pcl::SACSegmentation<PointT> seg;
    seg.setInputCloud(src);
    seg.setModelType(pcl::SACMODEL_CYLINDER);
    // PCL 1.9：圆柱模型仅支持 RANSAC
    seg.setMethodType(pcl::SAC_RANSAC);
    (void)method;
    seg.setDistanceThreshold(distance_threshold);
    seg.setOptimizeCoefficients(true);
    pcl::PointIndices inliers;
    pcl::ModelCoefficients coeffs;
    seg.segment(inliers, coeffs);
    // coeffs = [point_on_axis(x,y,z), axis_direction(x,y,z), radius]
    if (inliers.indices.empty() || coeffs.values.size() < 7)
        return false;
    out_point.assign(coeffs.values.begin(), coeffs.values.begin() + 3);
    out_axis.assign(coeffs.values.begin() + 3, coeffs.values.begin() + 6);
    out_radius = coeffs.values[6];
    out_inliers = inliers.indices;
    return true;
}

// ---------------- segment ----------------

template <typename PointT>
bool CloudProcessor<PointT>::segmentPlaneRANSAC(const CloudConstPtr& src, double distance_threshold,
                                               CloudPtr& out_inliers, CloudPtr& out_outliers,
                                               std::vector<float>& out_coeffs)
{
    std::vector<int> inlierIdx;
    if (!fitPlane(src, FitMethod::RANSAC, distance_threshold, out_coeffs, inlierIdx))
        return false;
    out_inliers = extractByIndices(src, inlierIdx);
    // 补集（空时输出空点云而非 nullptr，调用方免判空）
    std::vector<int> outIdx;
    if (inlierIdx.size() < src->size()) {
        std::set<int> inset(inlierIdx.begin(), inlierIdx.end());
        for (int i = 0; i < (int)src->size(); ++i)
            if (!inset.count(i))
                outIdx.push_back(i);
    }
    if (outIdx.empty()) {
        out_outliers.reset(new pcl::PointCloud<PointT>);
    } else {
        out_outliers = extractByIndices(src, outIdx);
    }
    return true;
}

template <typename PointT>
std::vector<typename CloudProcessor<PointT>::CloudPtr>
CloudProcessor<PointT>::segmentEuclideanCluster(const CloudConstPtr& src, double cluster_tolerance,
                                                int min_size, int max_size)
{
    std::vector<CloudPtr> out;
    if (!src || src->empty())
        return out;
    pcl::EuclideanClusterExtraction<PointT> ec;
    ec.setInputCloud(src);
    ec.setClusterTolerance(cluster_tolerance);
    ec.setMinClusterSize(min_size);
    ec.setMaxClusterSize(max_size);
    typename pcl::search::KdTree<PointT>::Ptr tree(new pcl::search::KdTree<PointT>);
    ec.setSearchMethod(tree);
    std::vector<pcl::PointIndices> clusters;
    ec.extract(clusters);
    out.reserve(clusters.size());
    for (const pcl::PointIndices& c : clusters)
        out.push_back(extractByIndices(src, c.indices));
    // 按点数降序
    std::sort(out.begin(), out.end(),
              [](const CloudPtr& a, const CloudPtr& b) { return a->size() > b->size(); });
    return out;
}

// ---------------- regist ----------------

template <typename PointT>
bool CloudProcessor<PointT>::registICP_PointToPoint(const CloudConstPtr& source, const CloudConstPtr& target,
                                                    double max_dist, float out_transform[16])
{
    if (!source || !target)
        return false;
    pcl::IterativeClosestPoint<PointT, PointT> icp;
    icp.setInputSource(source);
    icp.setInputTarget(target);
    icp.setMaxCorrespondenceDistance((float)max_dist);
    pcl::PointCloud<PointT> aligned;
    icp.align(aligned);
    if (!icp.hasConverged())
        return false;
    matToArray(icp.getFinalTransformation(), out_transform);
    return true;
}

template <typename PointT>
bool CloudProcessor<PointT>::registICP_PointToPlane(const CloudConstPtr& source, const CloudConstPtr& target,
                                                    const pcl::PointCloud<pcl::Normal>::Ptr& target_normals,
                                                    double max_dist, float out_transform[16])
{
    if (!source || !target || !target_normals)
        return false;
    // 点到面 ICP 需要 target 带法线（Normal 拼到 XYZ 字段）
    pcl::PointCloud<pcl::PointNormal>::Ptr targetN(new pcl::PointCloud<pcl::PointNormal>);
    pcl::copyPointCloud(*target, *targetN);
    for (size_t i = 0; i < targetN->size() && i < target_normals->size(); ++i) {
        (*targetN)[i].normal_x = (*target_normals)[i].normal_x;
        (*targetN)[i].normal_y = (*target_normals)[i].normal_y;
        (*targetN)[i].normal_z = (*target_normals)[i].normal_z;
        (*targetN)[i].curvature = (*target_normals)[i].curvature;
    }
    pcl::PointCloud<pcl::PointNormal>::Ptr sourceN(new pcl::PointCloud<pcl::PointNormal>);
    pcl::copyPointCloud(*source, *sourceN);
    pcl::IterativeClosestPointWithNormals<pcl::PointNormal, pcl::PointNormal> icp;
    icp.setInputSource(sourceN);
    icp.setInputTarget(targetN);
    icp.setMaxCorrespondenceDistance((float)max_dist);
    pcl::PointCloud<pcl::PointNormal> aligned;
    icp.align(aligned);
    if (!icp.hasConverged())
        return false;
    matToArray(icp.getFinalTransformation(), out_transform);
    return true;
}

template <typename PointT>
bool CloudProcessor<PointT>::registNDT(const CloudConstPtr& source, const CloudConstPtr& target,
                                       double voxel_size, float out_transform[16])
{
    if (!source || !target)
        return false;
    pcl::NormalDistributionsTransform<PointT, PointT> ndt;
    ndt.setInputSource(source);
    ndt.setInputTarget(target);
    ndt.setResolution((float)voxel_size);
    pcl::PointCloud<PointT> aligned;
    ndt.align(aligned);
    if (!ndt.hasConverged())
        return false;
    matToArray(ndt.getFinalTransformation(), out_transform);
    return true;
}

template <typename PointT>
bool CloudProcessor<PointT>::registPCA(const CloudConstPtr& source, const CloudConstPtr& target,
                                       float out_transform[16])
{
    // PCA 粗配准：两片点云的主轴对齐（质心平移 + 主轴旋转）
    if (!source || !target || source->empty() || target->empty())
        return false;
    auto pcaAxes = [](const CloudConstPtr& c, Eigen::Vector4f& centroid) {
        pcl::compute3DCentroid(*c, centroid);
        Eigen::MatrixXf d(c->size(), 3);
        for (size_t i = 0; i < c->size(); ++i) {
            d((Eigen::Index)i, 0) = (*c)[i].x - centroid[0];
            d((Eigen::Index)i, 1) = (*c)[i].y - centroid[1];
            d((Eigen::Index)i, 2) = (*c)[i].z - centroid[2];
        }
        Eigen::Matrix3f cov = (d.adjoint() * d) / (float)std::max<size_t>(1, c->size() - 1);
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3f> es(cov);
        // 特征值升序 → 主轴降序
        Eigen::Matrix3f axes = es.eigenvectors();
        Eigen::Vector3f order = es.eigenvalues();
        for (int i = 0; i < 2; ++i)
            for (int j = i + 1; j < 3; ++j)
                if (order[j] > order[i]) {
                    std::swap(order[i], order[j]);
                    axes.col(i).swap(axes.col(j));
                }
        return axes;
    };
    Eigen::Vector4f cs, ct;
    const Eigen::Matrix3f As = pcaAxes(source, cs);
    const Eigen::Matrix3f At = pcaAxes(target, ct);
    // R = At · As⁻¹（把 source 主轴转到 target 主轴）
    const Eigen::Matrix3f R = At * As.transpose();
    const Eigen::Vector3f t = ct.head<3>() - R * cs.head<3>();
    Eigen::Matrix4f T = Eigen::Matrix4f::Identity();
    T.block<3, 3>(0, 0) = R;
    T.block<3, 1>(0, 3) = t;
    matToArray(T, out_transform);
    return true;
}

// ---------------- tree ----------------

template <typename PointT>
void CloudProcessor<PointT>::treeKdTreeKNN(const CloudConstPtr& src, const float query_point[3],
                                           int k, std::vector<int>& out_indices,
                                           std::vector<float>& out_distances)
{
    out_indices.clear();
    out_distances.clear();
    if (!src || src->empty())
        return;
    pcl::KdTreeFLANN<PointT> tree;
    tree.setInputCloud(src);
    std::vector<float> dist;
    tree.nearestKSearch(makeQueryPoint<PointT>(query_point), k, out_indices, dist);
    out_distances = std::move(dist);
}

template <typename PointT>
void CloudProcessor<PointT>::treeKdTreeRadiusSearch(const CloudConstPtr& src, const float query_point[3],
                                                    double radius, std::vector<int>& out_indices,
                                                    std::vector<float>& out_distances)
{
    out_indices.clear();
    out_distances.clear();
    if (!src || src->empty())
        return;
    pcl::KdTreeFLANN<PointT> tree;
    tree.setInputCloud(src);
    tree.radiusSearch(makeQueryPoint<PointT>(query_point), (float)radius, out_indices, out_distances);
}

template <typename PointT>
void CloudProcessor<PointT>::treeOctreeVoxelSearch(const CloudConstPtr& src, const float query_point[3],
                                                   std::vector<int>& out_indices,
                                                   double resolution)
{
    out_indices.clear();
    if (!src || src->empty())
        return;
    if (resolution <= 0)
        resolution = 1.0;
    pcl::octree::OctreePointCloudSearch<PointT> oct((float)resolution);
    oct.setInputCloud(src);
    oct.addPointsFromInputCloud();
    std::vector<int> vidx;
    if (oct.voxelSearch(makeQueryPoint<PointT>(query_point), vidx))
        out_indices = std::move(vidx);
}

template <typename PointT>
void CloudProcessor<PointT>::treeOctreeChangeDetection(const CloudConstPtr& src1, const CloudConstPtr& src2,
                                                       double min_resolution,
                                                       std::vector<int>& out_new_indices)
{
    out_new_indices.clear();
    if (!src1 || !src2 || src2->empty())
        return;
    pcl::octree::OctreePointCloudChangeDetector<PointT> oct((float)min_resolution);
    oct.setInputCloud(src1);
    oct.addPointsFromInputCloud();
    oct.switchBuffers();
    oct.setInputCloud(src2);
    oct.addPointsFromInputCloud();
    std::vector<int> newIdx;
    oct.getPointIndicesFromNewVoxels(newIdx);
    out_new_indices = std::move(newIdx);
}

// ---------------- surface ----------------

template <typename PointT>
typename CloudProcessor<PointT>::CloudPtr
CloudProcessor<PointT>::surfaceConvexHull(const CloudConstPtr& src)
{
    if (!src || src->empty())
        return nullptr;
    pcl::ConvexHull<PointT> hull;
    hull.setInputCloud(src);
    CloudPtr out(new pcl::PointCloud<PointT>);
    hull.reconstruct(*out);
    return out;
}

// =====================================================================
// 显式实例化白名单（业务层直接链接，无模板传染）
// =====================================================================

template class CloudProcessor<pcl::PointXYZ>;
template class CloudProcessor<pcl::PointXYZI>;
template class CloudProcessor<pcl::PointXYZRGB>;

} // namespace shapely
