// Image2DAlg_Calib.cpp —— 相机标定实现（手写 Mei 模型 + 张正友 + LM）。
// 数学库 Eigen（thrid/PCL-1.9.1/3rdParty/Eigen），图像容器 OpenCV（仅 Mat/Point）。
// 本文件不使用 cv::calibrateCamera —— 标定求解全部手写。

#include "include/Image2DAlg_Calib.h"
#include "CommonAlg/include/CommonAlg_FileUtil.h"

#include <Eigen/Dense>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace image2d {

namespace {

constexpr int kUnknown = -1;

// ---------------- 小工具 ----------------

cv::Mat IntrinsicsToK(const CameraIntrinsics& K)
{
    cv::Mat m = cv::Mat::zeros(3, 3, CV_64F);
    m.at<double>(0, 0) = K.fx;
    m.at<double>(1, 1) = K.fy;
    m.at<double>(0, 2) = K.cx;
    m.at<double>(1, 2) = K.cy;
    m.at<double>(2, 2) = 1.0;
    return m;
}

// Rodrigues 旋转向量 ↔ 3x3（手写，不依赖 calib3d）
cv::Mat RodriguesToMat(const cv::Mat& rvec)
{
    const double th = cv::norm(rvec);
    cv::Mat R = cv::Mat::eye(3, 3, CV_64F);
    if (th < 1e-12)
        return R;
    cv::Mat k = rvec / th;
    const double kx = k.at<double>(0), ky = k.at<double>(1), kz = k.at<double>(2);
    const double c = std::cos(th), s = std::sin(th), v = 1.0 - c;
    R.at<double>(0, 0) = c + kx * kx * v;
    R.at<double>(0, 1) = kx * ky * v - kz * s;
    R.at<double>(0, 2) = kx * kz * v + ky * s;
    R.at<double>(1, 0) = ky * kx * v + kz * s;
    R.at<double>(1, 1) = c + ky * ky * v;
    R.at<double>(1, 2) = ky * kz * v - kx * s;
    R.at<double>(2, 0) = kz * kx * v - ky * s;
    R.at<double>(2, 1) = kz * ky * v + kx * s;
    R.at<double>(2, 2) = c + kz * kz * v;
    return R;
}

cv::Mat MatToRodrigues(const cv::Mat& R)
{
    const double trace = R.at<double>(0, 0) + R.at<double>(1, 1) + R.at<double>(2, 2);
    double th = std::acos(std::max(-1.0, std::min(1.0, (trace - 1.0) * 0.5)));
    if (th < 1e-12)
        return cv::Mat::zeros(3, 1, CV_64F);
    // 轴 = (R - R^T) 的反对称部分
    cv::Mat axis(3, 1, CV_64F);
    axis.at<double>(0) = R.at<double>(2, 1) - R.at<double>(1, 2);
    axis.at<double>(1) = R.at<double>(0, 2) - R.at<double>(2, 0);
    axis.at<double>(2) = R.at<double>(1, 0) - R.at<double>(0, 1);
    axis /= (2.0 * std::sin(th));
    return axis * th;
}

// ---------------- 针孔投影（含畸变） ----------------

cv::Point2f ProjectPinhole(const cv::Point3f& pw, const CameraIntrinsics& K,
                           const cv::Mat& rvec, const cv::Mat& tvec)
{
    cv::Mat R = RodriguesToMat(rvec);
    const double X = (double)pw.x, Y = (double)pw.y, Z = (double)pw.z;
    // 世界 → 相机
    const double xc = R.at<double>(0, 0) * X + R.at<double>(0, 1) * Y + R.at<double>(0, 2) * Z + tvec.at<double>(0);
    const double yc = R.at<double>(1, 0) * X + R.at<double>(1, 1) * Y + R.at<double>(1, 2) * Z + tvec.at<double>(1);
    const double zc = R.at<double>(2, 0) * X + R.at<double>(2, 1) * Y + R.at<double>(2, 2) * Z + tvec.at<double>(2);
    if (zc <= 1e-9)
        return cv::Point2f(-1, -1);

    // 归一化 + 畸变
    const double xn = xc / zc, yn = yc / zc;
    const double r2 = xn * xn + yn * yn;
    const double radial = 1.0 + K.k1 * r2 + K.k2 * r2 * r2 + K.k3 * r2 * r2 * r2;
    const double xd = xn * radial + 2.0 * K.p1 * xn * yn + K.p2 * (r2 + 2.0 * xn * xn);
    const double yd = yn * radial + K.p1 * (r2 + 2.0 * yn * yn) + 2.0 * K.p2 * xn * yn;

    return cv::Point2f((float)(K.fx * xd + K.cx), (float)(K.fy * yd + K.cy));
}

// ---------------- Mei 等距模型 ----------------

/// 归一化像平面点 (xd, yd)（含畸变前的针孔坐标）→ Mei 反投影方向。
/// Mei: θ = atan2(ρ_un, 1 - ξ ... ) 实际用迭代：给定像素 m，求球面点。
/// 此处用简化迭代：像素 → 针孔归一化 (xd,yd) → 求满足 Mei 投影方程的 θ。
/// Mei 投影：ρ = (ξ + cosθ... ) 正向公式：r = f·sinθ/(ξ+cosθ)（视 ξ 而定）
/// 反投影用一维牛顿：解 θ 使 f·sinθ/(ξ+cosθ) = ρ0（ρ0=||xd,yd||）
bool MeiUnprojectTheta(double rho0, double fx, double xi, double& theta)
{
    // 退化：ξ≈0 时 Mei 退化为针孔，θ = atan(rho0/f)
    if (std::fabs(xi) < 1e-9) {
        theta = std::atan(rho0 / fx);
        return true;
    }
    // f(θ) = sinθ/(ξ + cosθ) - rho0/fx = 0
    // 目标值
    const double target = rho0 / fx;
    theta = std::atan(target);   // 初值（ξ=0 解）
    for (int it = 0; it < 30; ++it) {
        const double st = std::sin(theta), ct = std::cos(theta);
        const double denom = xi + ct;
        if (std::fabs(denom) < 1e-12)
            return false;
        const double f = st / denom - target;
        const double df = (ct * denom + st * st) / (denom * denom);   // d/dθ
        const double dtheta = f / df;
        theta -= dtheta;
        if (std::fabs(dtheta) < 1e-12)
            return true;
    }
    return std::fabs(std::sin(theta) / (xi + std::cos(theta)) - target) < 1e-6;
}

} // namespace

// =====================================================================
// Mei 投影 / 反投影 / 去畸变
// =====================================================================

bool MeiProjectPoint(const double X[3], const CameraIntrinsics& K, double out_uv[2])
{
    // 世界点 → 镜面坐标系（Mei 假定相机系原点即镜面中心，此处直接用）
    const double x = X[0], y = X[1], z = X[2];
    const double norm = std::sqrt(x * x + y * y + z * z);
    if (norm < 1e-12)
        return false;

    // 球面投影
    const double sx = x / norm, sy = y / norm, sz = z / norm;

    // 等距/中心投影到平面：r = f * sinθ / (ξ + cosθ)
    // sinθ = √(sx²+sy²), cosθ = sz（要求 z>0 侧）
    const double rho = std::sqrt(sx * sx + sy * sy);
    const double denom = K.xi + sz;
    if (std::fabs(denom) < 1e-12)
        return false;
    const double md = K.fx * rho / denom;   // 广义焦距 f 作用于等距半径

    // 方向
    double xd = 0.0, yd = 0.0;
    if (rho > 1e-12) {
        xd = md * sx / rho;
        yd = md * sy / rho;
    }

    // Mei 广义畸变（通常很小，默认 0）
    const double r2 = xd * xd + yd * yd;
    const double dist = 1.0 + K.meiC * r2 + K.meiD * r2 * r2 + K.meiE * r2 * r2 * r2;
    xd *= dist;
    yd *= dist;

    out_uv[0] = K.fx * 0.0 + K.cx + xd * (K.fx > 0 ? 1.0 : 1.0);   // xd 已含 f 缩放
    out_uv[0] = K.cx + xd;
    out_uv[1] = K.cy + yd;
    return true;
}

bool MeiUnprojectPixel(double u, double v, const CameraIntrinsics& K, double out_dir[3])
{
    // 像素 → md（Mei 广义焦距域半径：md = f·sinθ/(ξ+cosθ)）
    const double xd = u - K.cx;
    const double yd = v - K.cy;
    const double rho_md = std::sqrt(xd * xd + yd * yd);

    // 解 θ：sinθ/(ξ+cosθ) = rho_md/f
    double theta = 0.0;
    if (!MeiUnprojectTheta(rho_md, K.fx, K.xi, theta))
        return false;
    const double st = std::sin(theta), ct = std::cos(theta);
    const double denom = K.xi + ct;
    if (std::fabs(denom) < 1e-12 || std::fabs(st) < 1e-12)
        return false;

    // 由正向投影关系重建球面方向：
    //   xd = md·sx/rho_s = f·st·sx/(denom·rho_s)，rho_s = st → sx = xd·denom/f
    double sx = xd * denom / K.fx;
    double sy = yd * denom / K.fx;
    const double sz = ct;
    const double n = std::sqrt(sx * sx + sy * sy + sz * sz);
    if (n < 1e-12)
        return false;
    out_dir[0] = sx / n;
    out_dir[1] = sy / n;
    out_dir[2] = sz / n;
    return true;
}

bool MeiUndistortPixel(double u, double v, const CameraIntrinsics& K, double out_uv[2])
{
    // 鱼眼像素 → 方向 → 理想针孔（xi=0 的等价针孔投影）
    double dir[3];
    if (!MeiUnprojectPixel(u, v, K, dir))
        return false;
    if (std::fabs(dir[2]) < 1e-12)
        return false;
    // Mei 的广义 f 直接当针孔焦距（ξ=0 时 Mei=针孔，f 相同）
    out_uv[0] = K.cx + K.fx * dir[0] / dir[2];
    out_uv[1] = K.cy + K.fy * dir[1] / dir[2];
    return true;
}

bool MeiDistortPixel(double u_undist, double v_undist, const CameraIntrinsics& K, double out_uv[2])
{
    // 理想针孔像素 → 方向 → Mei 像素
    if (std::fabs(v_undist - K.cy) < 1e-12 && std::fabs(u_undist - K.cx) < 1e-12) {
        out_uv[0] = K.cx; out_uv[1] = K.cy;
        return true;
    }
    const double xn = (u_undist - K.cx) / K.fx;
    const double yn = (v_undist - K.cy) / K.fy;
    const double norm = std::sqrt(xn * xn + yn * yn);
    if (norm < 1e-12)
        return false;
    // 针孔方向 → 球面 → Mei 投影
    const double X = xn, Y = yn, Z = 1.0;
    const double nrm = std::sqrt(X * X + Y * Y + Z * Z);
    const double sx = X / nrm, sy = Y / nrm, sz = Z / nrm;
    const double rho = std::sqrt(sx * sx + sy * sy);
    const double denom = K.xi + sz;
    if (std::fabs(denom) < 1e-12)
        return false;
    const double md = K.fx * rho / denom;
    double xd = md * sx / rho, yd = md * sy / rho;
    const double r2 = xd * xd + yd * yd;
    const double dist = 1.0 + K.meiC * r2 + K.meiD * r2 * r2 + K.meiE * r2 * r2 * r2;
    xd *= dist; yd *= dist;
    out_uv[0] = K.cx + xd;
    out_uv[1] = K.cy + yd;
    return true;
}

// =====================================================================
// 标定板
// =====================================================================

std::vector<cv::Point3f> CalibBoardWorldPoints(cv::Size corners, double squareSize)
{
    std::vector<cv::Point3f> out;
    out.reserve((size_t)corners.width * corners.height);
    for (int y = 0; y < corners.height; ++y)
        for (int x = 0; x < corners.width; ++x)
            out.push_back(cv::Point3f((float)(x * squareSize), (float)(y * squareSize), 0.0f));
    return out;
}

// =====================================================================
// 内部：LM 优化框架（通用残差最小二乘）
// =====================================================================

namespace {

// LM 优化框架：residualFn(params) → 残差向量（未平方），
// cost = ||r||²，H = J^T J（含参数耦合），标准 LM 步长。
using ResidualFn = std::function<Eigen::VectorXd(const Eigen::VectorXd&)>;

bool SolveLM(ResidualFn fn, Eigen::VectorXd& params, int maxIter = 200, double lambda = 1e-3)
{
    if (params.size() == 0)
        return false;
    Eigen::VectorXd r0 = fn(params);
    double cost = r0.squaredNorm();
    for (int it = 0; it < maxIter; ++it) {
        const int n = (int)params.size();
        const int m = (int)r0.size();
        // 数值雅可比（残差向量，中心差分）
        Eigen::MatrixXd J(m, n);
        for (int j = 0; j < n; ++j) {
            const double h = std::max(1e-8, std::fabs(params[j]) * 1e-5);
            Eigen::VectorXd pp = params, pm = params;
            pp[j] += h; pm[j] -= h;
            J.col(j) = (fn(pp) - fn(pm)) / (2.0 * h);
        }
        // LM 步：δ = -(J^T J + λ·diag(J^T J))^-1 · J^T r
        Eigen::MatrixXd JTJ = J.transpose() * J;
        Eigen::VectorXd g = J.transpose() * r0;
        for (int j = 0; j < n; ++j)
            JTJ(j, j) += lambda * std::max(JTJ(j, j), 1e-9);
        const Eigen::VectorXd step = JTJ.ldlt().solve(-g);
        if (!step.allFinite())
            break;

        Eigen::VectorXd trial = params + step;
        Eigen::VectorXd rt = fn(trial);
        const double trialCost = rt.squaredNorm();
        if (!std::isfinite(trialCost))
            break;
        if (trialCost < cost) {
            const double improve = cost - trialCost;
            params = trial;
            r0 = rt;
            cost = trialCost;
            lambda = std::max(lambda * 0.3, 1e-10);
            if (it < 8)
                std::fprintf(stderr, "  [LM it%d] cost=%.3e lambda=%.2e\n", it, cost, lambda);
            if (improve < 1e-10 || step.norm() < 1e-12)
                break;
        } else {
            lambda *= 5.0;
            if (lambda > 1e14)
                break;
        }
    }
    std::fprintf(stderr, "  [LM end] cost=%.3e\n", cost);
    return true;
}

bool HomographyDLT(const std::vector<cv::Point3f>& world,
                   const std::vector<cv::Point2f>& image, cv::Mat& H)
{
    const size_t n = world.size();
    if (n < 4 || image.size() != n)
        return false;

    Eigen::MatrixXd A(2 * n, 9);
    for (size_t i = 0; i < n; ++i) {
        const double X = world[i].x, Y = world[i].y, u = image[i].x, v = image[i].y;   // Z=0 平面，X/Y 即归一化坐标
        A(2 * i, 0) = X; A(2 * i, 1) = Y; A(2 * i, 2) = 1;
        A(2 * i, 3) = 0; A(2 * i, 4) = 0; A(2 * i, 5) = 0;
        A(2 * i, 6) = -u * X; A(2 * i, 7) = -u * Y; A(2 * i, 8) = -u;
        A(2 * i + 1, 0) = 0; A(2 * i + 1, 1) = 0; A(2 * i + 1, 2) = 0;
        A(2 * i + 1, 3) = X; A(2 * i + 1, 4) = Y; A(2 * i + 1, 5) = 1;
        A(2 * i + 1, 6) = -v * X; A(2 * i + 1, 7) = -v * Y; A(2 * i + 1, 8) = -v;
    }
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(A, Eigen::ComputeFullV);
    const Eigen::VectorXd h = svd.matrixV().col(8);
    if (h.size() != 9)
        return false;
    H = cv::Mat::zeros(3, 3, CV_64F);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            H.at<double>(r, c) = h[r * 3 + c];
    // 归一化
    const double scale = H.at<double>(2, 2);
    if (std::fabs(scale) < 1e-12)
        return false;
    H /= scale;
    return true;
}

} // namespace

// =====================================================================
// 单目标定
// =====================================================================

bool CalibrateMono(
    const std::vector<std::vector<cv::Point2f>>& imagePoints,
    const std::vector<cv::Point3f>& worldPoints,
    cv::Size imageSize,
    CameraIntrinsics& K,
    std::vector<cv::Mat>& rvecs, std::vector<cv::Mat>& tvecs,
    double* outRms)
{
    rvecs.clear(); tvecs.clear();
    if (outRms) *outRms = -1.0;
    if (imagePoints.size() < 3 || worldPoints.empty())
        return false;
    const size_t nPer = worldPoints.size();
    for (const auto& ip : imagePoints)
        if (ip.size() != nPer)
            return false;

    // ---- 1. 内参初值 ----
    CameraIntrinsics work = K;
    work.imageSize = imageSize;
    if (work.fx <= 0) work.fx = imageSize.width;
    if (work.fy <= 0) work.fy = imageSize.width;
    if (work.cx <= 0) work.cx = imageSize.width * 0.5;
    if (work.cy <= 0) work.cy = imageSize.height * 0.5;
    if (work.k1 != 0 || work.k2 != 0) { work.k1 = 0; work.k2 = 0; work.p1 = 0; work.p2 = 0; }
    // 畸变初值清零：让 LM 从无畸变出发逐步学出（避免与内参耦合规范不确定性）

    // ---- 2. 逐图外参初值：DLT（世界 Z=0 平面 → 单应分解） ----
    std::vector<cv::Mat> Rl, tl;
    for (size_t i = 0; i < imagePoints.size(); ++i) {
        cv::Mat H;
        if (!HomographyDLT(worldPoints, imagePoints[i], H))
            return false;
        // H = K [r1 r2 t] → r1 = K^-1 h1, r2 = K^-1 h2, t = K^-1 h3
        const cv::Mat Km = IntrinsicsToK(work);
        const cv::Mat Kinv = Km.inv();
        const cv::Mat r1 = Kinv * H.col(0);
        const cv::Mat r2 = Kinv * H.col(1);
        const cv::Mat t  = Kinv * H.col(2);

        // Gram-Schmidt 正交化（r1/r2 由 K^-1 缩放，幅值非 1）
        cv::Mat c0 = r1.clone(), c1 = r2.clone();
        const double n0 = cv::norm(c0);
        if (n0 < 1e-12) return false;
        c0 /= n0;
        c1 = c1 - (c1.dot(c0)) * c0;
        const double n1 = cv::norm(c1);
        if (n1 < 1e-12) return false;
        c1 /= n1;
        cv::Mat c2 = c0.cross(c1);
        cv::Mat Rfull = cv::Mat::zeros(3, 3, CV_64F);
        c0.copyTo(Rfull.col(0));
        c1.copyTo(Rfull.col(1));
        c2.copyTo(Rfull.col(2));
        if (cv::determinant(Rfull) < 0) {
            Rfull.col(0) = -Rfull.col(0);
            Rfull.col(1) = -Rfull.col(1);
        }
        // DLT 的 H 归一化到 H[2,2]=1 后 t 只剩方向，量纲丢失。
        // 恢复量纲：t 方向保留，模长缩放到板对角线 × 5（LM 可微调）
        cv::Mat tvec = t.clone();
        const double tnorm = cv::norm(tvec);
        if (tnorm > 1e-12) {
            const double boardDiag = 10.0 * std::sqrt(
                (double)(worldPoints.back().x * worldPoints.back().x +
                         worldPoints.back().y * worldPoints.back().y));
            tvec *= (boardDiag * 5.0 / tnorm);
        }
        Rl.push_back(Rfull);
        tl.push_back(tvec);
    }

    // ---- 3. 两阶段 LM ----
    //   阶段 A：固定内参，只优化外参（外参初值来自 DLT，误差大；先收好外参）
    //   阶段 B：全参数精化
    const int nViews = (int)imagePoints.size();
    const int nIntr = 8;
    const int nParams = nIntr + nViews * 6;
    Eigen::VectorXd params(nParams);
    params[0] = work.fx; params[1] = work.fy;
    params[2] = work.cx; params[3] = work.cy;
    params[4] = work.k1; params[5] = work.k2;
    params[6] = work.p1; params[7] = work.p2;
    for (int i = 0; i < nViews; ++i) {
        const cv::Mat rv = MatToRodrigues(Rl[i]);
        for (int j = 0; j < 3; ++j) params[nIntr + i * 6 + j] = rv.at<double>(j);
        for (int j = 0; j < 3; ++j) params[nIntr + i * 6 + 3 + j] = tl[i].at<double>(j);
    }

    auto makeResidual = [&](const CameraIntrinsics* fixedIntr) {
        return [&, fixedIntr](const Eigen::VectorXd& p) -> Eigen::VectorXd {
            CameraIntrinsics kk = fixedIntr ? *fixedIntr : work;
            if (!fixedIntr) {
                kk.fx = p[0]; kk.fy = p[1]; kk.cx = p[2]; kk.cy = p[3];
                kk.k1 = p[4]; kk.k2 = p[5]; kk.p1 = p[6]; kk.p2 = p[7];
            }
            const int extOff = fixedIntr ? 0 : nIntr;
            Eigen::VectorXd r(nViews * (int)nPer * 2);
            int k = 0;
            for (int i = 0; i < nViews; ++i) {
                cv::Mat rvec = cv::Mat::zeros(3, 1, CV_64F);
                cv::Mat tvec = cv::Mat::zeros(3, 1, CV_64F);
                for (int j = 0; j < 3; ++j) {
                    rvec.at<double>(j) = p[extOff + i * 6 + j];
                    tvec.at<double>(j) = p[extOff + i * 6 + 3 + j];
                }
                for (size_t j = 0; j < nPer; ++j) {
                    const cv::Point2f proj = ProjectPinhole(worldPoints[j], kk, rvec, tvec);
                    r[k++] = proj.x - imagePoints[i][j].x;
                    r[k++] = proj.y - imagePoints[i][j].y;
                }
            }
            return r;
        };
    };

    {
        Eigen::VectorXd extOnly(nViews * 6);
        for (int i = 0; i < nViews; ++i)
            for (int j = 0; j < 6; ++j)
                extOnly[i * 6 + j] = params[nIntr + i * 6 + j];
        {   // 阶段 A 起始 RMS
            auto rr0 = makeResidual(&work)(extOnly);
            std::fprintf(stderr, "[阶段A起始] rms=%.4f\n",
                         std::sqrt(rr0.squaredNorm() / (rr0.size() * 0.5)));
        }
        std::fprintf(stderr, "[阶段A] 优化外参...\n");
        SolveLM(makeResidual(&work), extOnly, 10);
        std::fprintf(stderr, "[阶段A] 完成\n");
        for (int i = 0; i < nViews; ++i)
            for (int j = 0; j < 6; ++j)
                params[nIntr + i * 6 + j] = extOnly[i * 6 + j];
        {   // 阶段 A 结束后的 RMS（用固定内参评估）
            auto rr = makeResidual(&work)(params);
            std::fprintf(stderr, "[阶段A结束] rms=%.4f\n",
                         std::sqrt(rr.squaredNorm() / (rr.size() * 0.5)));
        }
    }
    SolveLM(makeResidual(nullptr), params, 3000);

    // ---- 4. 回写 ----
    K = work;
    K.imageSize = imageSize;
    K.fx = params[0]; K.fy = params[1];
    K.cx = params[2]; K.cy = params[3];
    K.k1 = params[4]; K.k2 = params[5];
    K.p1 = params[6]; K.p2 = params[7];

    rvecs.clear(); tvecs.clear();
    double sum2 = 0.0;
    int cnt = 0;
    for (int i = 0; i < nViews; ++i) {
        cv::Mat rvec = cv::Mat::zeros(3, 1, CV_64F);
        cv::Mat tvec = cv::Mat::zeros(3, 1, CV_64F);
        for (int j = 0; j < 3; ++j) {
            rvec.at<double>(j) = params[nIntr + i * 6 + j];
            tvec.at<double>(j) = params[nIntr + i * 6 + 3 + j];
        }
        rvecs.push_back(rvec);
        tvecs.push_back(tvec);
        for (size_t j = 0; j < nPer; ++j) {
            const cv::Point2f proj = ProjectPinhole(worldPoints[j], K, rvec, tvec);
            const double dx = proj.x - imagePoints[i][j].x;
            const double dy = proj.y - imagePoints[i][j].y;
            sum2 += dx * dx + dy * dy;
            ++cnt;
        }
    }
    if (outRms)
        *outRms = cnt ? std::sqrt(sum2 / cnt) : -1.0;
    return true;
}


bool CalibrateFisheyeMei(
    const std::vector<std::vector<cv::Point2f>>& imagePoints,
    const std::vector<cv::Point3f>& worldPoints,
    cv::Size imageSize,
    CameraIntrinsics& K,
    double* outRms)
{
    if (imagePoints.size() < 3 || worldPoints.empty())
        return false;
    const size_t nPer = worldPoints.size();

    // 1. 针孔初值（Mei 的 ξ 初值取 0.5，鱼眼常用）
    CameraIntrinsics work;
    work.imageSize = imageSize;
    work.fx = imageSize.width; work.fy = imageSize.width;
    work.cx = imageSize.width * 0.5;
    work.cy = imageSize.height * 0.5;
    work.xi = 0.5;
    if (K.fx > 0) work = K;   // 允许外部带初值

    // 2. LM：fx fy cx cy xi + 每图外参；残差用 MeiProjectPoint
    const int nViews = (int)imagePoints.size();
    const int nIntr = 5;   // fx fy cx cy xi
    const int nParams = nIntr + nViews * 6;
    Eigen::VectorXd params(nParams);
    params[0] = work.fx; params[1] = work.fy;
    params[2] = work.cx; params[3] = work.cy;
    params[4] = work.xi;
    for (int i = 0; i < nViews; ++i) {
        // 外参初值：用针孔 DLT（鱼眼图先粗去畸变再 DLT 也行；此处直接以图像点近似）
        cv::Mat H;
        if (!HomographyDLT(worldPoints, imagePoints[i], H))
            return false;
        const cv::Mat Km = IntrinsicsToK(work);
        const cv::Mat Kinv = Km.inv();
        const cv::Mat r1 = Kinv * H.col(0);
        const cv::Mat r2 = Kinv * H.col(1);
        const cv::Mat t  = Kinv * H.col(2);
        cv::Mat R = cv::Mat::zeros(3, 3, CV_64F);
        r1.copyTo(R.col(0)); r2.copyTo(R.col(1)); t.copyTo(R.col(2));
        cv::Mat c0 = R.col(0).clone(), c1 = R.col(1).clone();
        const double n0 = cv::norm(c0);
        if (n0 < 1e-12) return false;
        c0 /= n0;
        c1 = c1 - (c1.dot(c0)) * c0;
        const double n1 = cv::norm(c1);
        if (n1 < 1e-12) return false;
        c1 /= n1;
        cv::Mat c2 = c0.cross(c1);
        cv::Mat Rfull = cv::Mat::zeros(3, 3, CV_64F);
        c0.copyTo(Rfull.col(0)); c1.copyTo(Rfull.col(1)); c2.copyTo(Rfull.col(2));
        if (cv::determinant(Rfull) < 0) {
            Rfull.col(0) = -Rfull.col(0);
            Rfull.col(1) = -Rfull.col(1);
        }
        cv::Mat rv = MatToRodrigues(Rfull);
        cv::Mat tv = cv::Mat::zeros(3, 1, CV_64F);
        t.copyTo(tv);
        // t 量纲恢复（同 CalibrateMono）
        const double tnorm2 = cv::norm(tv);
        if (tnorm2 > 1e-12) {
            const double diag2 = 10.0 *
                std::sqrt((double)(worldPoints.back().x * worldPoints.back().x +
                                   worldPoints.back().y * worldPoints.back().y));
            tv *= (diag2 * 5.0 / tnorm2);
        }
        for (int j = 0; j < 3; ++j) {
            params[nIntr + i * 6 + j] = rv.at<double>(j);
            params[nIntr + i * 6 + 3 + j] = tv.at<double>(j);
        }
    }

    auto residualFn = [&](const Eigen::VectorXd& p) -> Eigen::VectorXd {
        CameraIntrinsics kk = work;
        kk.fx = p[0]; kk.fy = p[1];
        kk.cx = p[2]; kk.cy = p[3];
        kk.xi = p[4];
        Eigen::VectorXd r(nViews * (int)nPer * 2);
        int k = 0;
        for (int i = 0; i < nViews; ++i) {
            cv::Mat rvec = cv::Mat::zeros(3, 1, CV_64F);
            cv::Mat tvec = cv::Mat::zeros(3, 1, CV_64F);
            for (int j = 0; j < 3; ++j) {
                rvec.at<double>(j) = p[nIntr + i * 6 + j];
                tvec.at<double>(j) = p[nIntr + i * 6 + 3 + j];
            }
            cv::Mat R = RodriguesToMat(rvec);
            for (size_t j = 0; j < nPer; ++j) {
                const double X = worldPoints[j].x, Y = worldPoints[j].y, Z = 0.0;
                const double xc = R.at<double>(0, 0) * X + R.at<double>(0, 1) * Y + R.at<double>(0, 2) * Z + tvec.at<double>(0);
                const double yc = R.at<double>(1, 0) * X + R.at<double>(1, 1) * Y + R.at<double>(1, 2) * Z + tvec.at<double>(1);
                const double zc = R.at<double>(2, 0) * X + R.at<double>(2, 1) * Y + R.at<double>(2, 2) * Z + tvec.at<double>(2);
                const double w[3] = { xc, yc, zc };
                double uv[2];
                if (MeiProjectPoint(w, kk, uv)) {
                    r[k++] = uv[0] - imagePoints[i][j].x;
                    r[k++] = uv[1] - imagePoints[i][j].y;
                } else {
                    r[k++] = 1e4;   // 视场外罚项（大残差而非 1e6 平方爆炸）
                    r[k++] = 1e4;
                }
            }
        }
        return r;
    };

    SolveLM(residualFn, params, 3000);

    K = work;
    K.imageSize = imageSize;
    K.fx = params[0]; K.fy = params[1];
    K.cx = params[2]; K.cy = params[3];
    K.xi = params[4];

    // 重投影 RMS
    if (outRms) {
        double sum2 = 0.0;
        int cnt = 0;
        bool ok = true;
        for (int i = 0; i < nViews && ok; ++i) {
            cv::Mat rvec = cv::Mat::zeros(3, 1, CV_64F);
            cv::Mat tvec = cv::Mat::zeros(3, 1, CV_64F);
            for (int j = 0; j < 3; ++j) {
                rvec.at<double>(j) = params[nIntr + i * 6 + j];
                tvec.at<double>(j) = params[nIntr + i * 6 + 3 + j];
            }
            cv::Mat R = RodriguesToMat(rvec);
            for (size_t j = 0; j < nPer; ++j) {
                const double X = worldPoints[j].x, Y = worldPoints[j].y;
                const double xc = R.at<double>(0, 0) * X + R.at<double>(0, 1) * Y + tvec.at<double>(0);
                const double yc = R.at<double>(1, 0) * X + R.at<double>(1, 1) * Y + tvec.at<double>(1);
                const double zc = R.at<double>(2, 0) * X + R.at<double>(2, 1) * Y + tvec.at<double>(2);
                const double w[3] = { xc, yc, zc };
                double uv[2];
                if (!MeiProjectPoint(w, K, uv)) { ok = false; break; }
                const double dx = uv[0] - imagePoints[i][j].x;
                const double dy = uv[1] - imagePoints[i][j].y;
                sum2 += dx * dx + dy * dy;
                ++cnt;
            }
        }
        *outRms = (ok && cnt) ? std::sqrt(sum2 / cnt) : -1.0;
    }
    return true;
}

// =====================================================================
// 双目标定 + 立体校正
// =====================================================================

bool CalibrateStereo(
    const std::vector<std::vector<cv::Point2f>>& leftPoints,
    const std::vector<std::vector<cv::Point2f>>& rightPoints,
    const std::vector<cv::Point3f>& worldPoints,
    cv::Size imageSize,
    CameraIntrinsics& KL, CameraIntrinsics& KR,
    StereoExtrinsics& ext, StereoRectify& rect,
    double* outRms)
{
    if (leftPoints.size() < 3 || leftPoints.size() != rightPoints.size())
        return false;

    // 1. 左右各自单目标定
    std::vector<cv::Mat> rl, tl, rr, tr;
    double rmsL = -1, rmsR = -1;
    CameraIntrinsics kl0 = KL, kr0 = KR;
    if (!CalibrateMono(leftPoints, worldPoints, imageSize, kl0, rl, tl, &rmsL))
        return false;
    if (!CalibrateMono(rightPoints, worldPoints, imageSize, kr0, rr, tr, &rmsR))
        return false;
    KL = kl0; KR = kr0;

    // 2. 立体对齐：逐图 T_l→板、T_r→板 → 右系在左系下的位姿 = T_r^-1 * T_l（板→左 = 板→右 · 右→左）
    //    R = Rr * Rl^T, t = tr - R * tl（板在左右各自相机系下的位姿组合）
    //    多帧平均（用第一帧也够，这里取平均更稳）
    cv::Mat Racc = cv::Mat::zeros(3, 3, CV_64F);
    cv::Mat tacc = cv::Mat::zeros(3, 1, CV_64F);
    for (size_t i = 0; i < leftPoints.size(); ++i) {
        const cv::Mat Rl = RodriguesToMat(rl[i]);
        const cv::Mat Rr = RodriguesToMat(rr[i]);
        // 右→左：R_rel = Rl * Rr^T ; t_rel = tl - R_rel * tr
        const cv::Mat Rrel = Rl * Rr.t();
        const cv::Mat trel = tl[i] - Rrel * tr[i];
        Racc += Rrel;
        tacc += trel;
    }
    Racc /= (double)leftPoints.size();
    tacc /= (double)leftPoints.size();

    ext.R = Racc.clone();
    ext.t = tacc.clone();

    if (outRms)
        *outRms = (rmsL + rmsR) * 0.5;

    // 3. Bouguet 立体校正
    const cv::Mat r1 = MatToRodrigues(rl[0]);
    (void)r1;
    // 分解 R 为两个半旋转 R1, R2（Bouguet）：r = R1·R2，各转一半
    cv::Mat R = ext.R;
    cv::Mat rvec = MatToRodrigues(R);
    const double theta = cv::norm(rvec);
    cv::Mat raxis = rvec / theta;
    const cv::Mat R1 = MatToRodrigues(raxis * (theta * 0.5));
    const cv::Mat R2 = MatToRodrigues(raxis * (-theta * 0.5));
    rect.R1 = R1;
    rect.R2 = R2;
    rect.rectSize = imageSize;

    // P1/P2：校正后投影矩阵（基线放在 P2[0][3] = -fx*tx）
    const cv::Mat P1 = IntrinsicsToK(KL) * cv::Mat::eye(3, 4, CV_64F) * 1.0;
    cv::Mat P1m = cv::Mat::zeros(3, 4, CV_64F);
    IntrinsicsToK(KL).copyTo(P1m.colRange(0, 3));
    cv::Mat P2m = cv::Mat::zeros(3, 4, CV_64F);
    IntrinsicsToK(KR).copyTo(P2m.colRange(0, 3));
    P2m.at<double>(0, 3) = -KR.fx * ext.t.at<double>(0);   // 基线项
    rect.P1 = P1m;
    rect.P2 = P2m;

    // Q：视差 → 深度
    cv::Mat Q = cv::Mat::zeros(4, 4, CV_64F);
    Q.at<double>(0, 0) = 1.0;
    Q.at<double>(1, 1) = 1.0;
    Q.at<double>(3, 2) = -1.0 / KL.fx;                  // 1/fx
    Q.at<double>(2, 3) = KL.cx;                          // 占位
    Q.at<double>(3, 3) = KL.cx;                          // 标准形式含 cx/cx' 差
    Q.at<double>(2, 3) = 0.0;
    Q.at<double>(3, 3) = 0.0;
    // 标准 Q：[1 0 0 -cx; 0 1 0 -cy; 0 0 0 f; 0 0 -1/Tx (cx-cx')/Tx]
    Q = cv::Mat::zeros(4, 4, CV_64F);
    Q.at<double>(0, 0) = 1.0;
    Q.at<double>(1, 1) = 1.0;
    Q.at<double>(0, 3) = -KL.cx;
    Q.at<double>(1, 3) = -KL.cy;
    Q.at<double>(2, 3) = KL.fx;
    Q.at<double>(3, 2) = -1.0 / (KR.fx * std::fabs(ext.t.at<double>(0)) / KR.fx);   // = -1/Tx
    Q.at<double>(3, 2) = -1.0 / std::max(1e-9, std::fabs(ext.t.at<double>(0)));
    Q.at<double>(3, 3) = 0.0;
    rect.Q = Q;
    return true;
}

// =====================================================================
// 持久化
// =====================================================================

bool CalibSave(const char* path, const CameraIntrinsics& K,
               const StereoExtrinsics* ext, const StereoRectify* rect)
{
    if (!path || !path[0])
        return false;
    char buf[2048];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - (size_t)n,
        "# CameraCalib v1 (key=value)\n"
        "model=pinhole+dist\n"
        "image_width=%d\nimage_height=%d\n"
        "fx=%.10f\nfy=%.10f\ncx=%.10f\ncy=%.10f\n"
        "k1=%.12f\nk2=%.12f\np1=%.12f\np2=%.12f\nk3=%.12f\n"
        "xi=%.10f\nmei_c=%.10f\nmei_d=%.10f\nmei_e=%.10f\n",
        K.imageSize.width, K.imageSize.height,
        K.fx, K.fy, K.cx, K.cy,
        K.k1, K.k2, K.p1, K.p2, K.k3,
        K.xi, K.meiC, K.meiD, K.meiE);
    if (ext) {
        for (int r = 0; r < 3; ++r)
            n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                          "R%d=%.10f %.10f %.10f\n", r,
                          ext->R.at<double>(r, 0), ext->R.at<double>(r, 1), ext->R.at<double>(r, 2));
        n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                      "t=%.10f %.10f %.10f\n",
                      ext->t.at<double>(0), ext->t.at<double>(1), ext->t.at<double>(2));
    }
    if (rect) {
        for (int r = 0; r < 3; ++r)
            n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                          "P1_%d=%.10f %.10f %.10f %.10f\n", r,
                          rect->P1.at<double>(r, 0), rect->P1.at<double>(r, 1),
                          rect->P1.at<double>(r, 2), rect->P1.at<double>(r, 3));
        for (int r = 0; r < 3; ++r)
            n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                          "P2_%d=%.10f %.10f %.10f %.10f\n", r,
                          rect->P2.at<double>(r, 0), rect->P2.at<double>(r, 1),
                          rect->P2.at<double>(r, 2), rect->P2.at<double>(r, 3));
        for (int r = 0; r < 4; ++r)
            n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                          "Q_%d=%.10f %.10f %.10f %.10f\n", r,
                          rect->Q.at<double>(r, 0), rect->Q.at<double>(r, 1),
                          rect->Q.at<double>(r, 2), rect->Q.at<double>(r, 3));
        n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                      "rect_width=%d\nrect_height=%d\n",
                      rect->rectSize.width, rect->rectSize.height);
    }
    return CommonAlg_WriteFile(path, std::string(buf, (size_t)n));
}

bool CalibLoad(const char* path, CameraIntrinsics& K,
               StereoExtrinsics* ext, StereoRectify* rect)
{
    if (!path || !path[0])
        return false;
    std::string text;
    if (!CommonAlg_ReadFile(path, text))
        return false;

    auto parseVec = [](const std::string& v, cv::Mat& m, int r) {
        double a[4] = { 0, 0, 0, 0 };
        if (sscanf(v.c_str(), "%lf %lf %lf %lf", &a[0], &a[1], &a[2], &a[3]) >= 1)
            for (int c = 0; c < 4 && c < m.cols; ++c)
                m.at<double>(r, c) = a[c];
    };

    K = CameraIntrinsics();
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, (eol == std::string::npos ? text.size() : eol) - pos);
        pos = (eol == std::string::npos) ? text.size() : eol + 1;
        if (line.empty() || line[0] == '#')
            continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if      (key == "image_width")  K.imageSize.width = atoi(val.c_str());
        else if (key == "image_height") K.imageSize.height = atoi(val.c_str());
        else if (key == "fx") K.fx = atof(val.c_str());
        else if (key == "fy") K.fy = atof(val.c_str());
        else if (key == "cx") K.cx = atof(val.c_str());
        else if (key == "cy") K.cy = atof(val.c_str());
        else if (key == "k1") K.k1 = atof(val.c_str());
        else if (key == "k2") K.k2 = atof(val.c_str());
        else if (key == "p1") K.p1 = atof(val.c_str());
        else if (key == "p2") K.p2 = atof(val.c_str());
        else if (key == "k3") K.k3 = atof(val.c_str());
        else if (key == "xi") K.xi = atof(val.c_str());
        else if (key == "mei_c") K.meiC = atof(val.c_str());
        else if (key == "mei_d") K.meiD = atof(val.c_str());
        else if (key == "mei_e") K.meiE = atof(val.c_str());
        else if (ext) {
            if (key == "R0" || key == "R1" || key == "R2") {
                if (ext->R.empty()) ext->R = cv::Mat::zeros(3, 3, CV_64F);
                parseVec(val, ext->R, key[1] - '0');
            } else if (key == "t") {
                if (ext->t.empty()) ext->t = cv::Mat::zeros(3, 1, CV_64F);
                double a[3] = { 0, 0, 0 };
                if (sscanf(val.c_str(), "%lf %lf %lf", &a[0], &a[1], &a[2]) == 3)
                    for (int r = 0; r < 3; ++r)
                        ext->t.at<double>(r) = a[r];
            }
        }
        if (rect) {
            if (key.rfind("P1_", 0) == 0) {
                if (rect->P1.empty()) rect->P1 = cv::Mat::zeros(3, 4, CV_64F);
                parseVec(val, rect->P1, atoi(key.c_str() + 3));
            } else if (key.rfind("P2_", 0) == 0) {
                if (rect->P2.empty()) rect->P2 = cv::Mat::zeros(3, 4, CV_64F);
                parseVec(val, rect->P2, atoi(key.c_str() + 3));
            } else if (key.rfind("Q_", 0) == 0) {
                if (rect->Q.empty()) rect->Q = cv::Mat::zeros(4, 4, CV_64F);
                parseVec(val, rect->Q, atoi(key.c_str() + 2));
            } else if (key == "rect_width") rect->rectSize.width = atoi(val.c_str());
            else if (key == "rect_height") rect->rectSize.height = atoi(val.c_str());
        }
    }
    return K.fx > 0 && K.fy > 0;
}

} // namespace image2d
