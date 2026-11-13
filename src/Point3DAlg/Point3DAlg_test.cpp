// Point3DAlg_test.cpp —— SICK 读取测试入口（控制台 exe，数据目录可经命令行传参）
// 流程：读 json 配置 → 创建 SickReader 对象 → 生成点云 → 保存点云(PLY)/深度图(16位+可视8位)。
// 用法：Point3DAlg_test.exe [相机1目录] [相机2目录 ...]（每个目录含 *.dat；缺省 = 内置双相机路径）
// 退出码：0=通过  1=配置读取失败  2=保存失败  3=处理异常  77=SKIP（数据目录不存在或无 .dat）

#include "include/Point3DAlg_SickReader.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <windows.h>   // FindFirstFileA（数据目录 .dat 探测）

// ---------------- 测试参数（缺省值，可用命令行参数覆盖） ----------------
// 用法：Point3DAlg_test.exe [相机1目录] [相机2目录 ...]
//   缺省相机目录 = Z:/ScanImage/3D、3D-2；输出文件写到**当前工作目录**
//   （2026-11-11 去掉硬编码 D:/pmy/alg/output——换机器/换构建目录不再写失败）
static const char* kFolders[] = { "Z:/ScanImage/3D", "Z:/ScanImage/3D-2" };   // 各相机扫描文件夹
static const int   kDeviceCount = 2;                                          // 相机数（1=单相机）
static const int   kFrame  = -1;                   // 指定帧（>=0），<0 = 全部帧
static const char* kOutCloud     = "sick_cloud.ply";        // 点云输出（PLY 二进制，cwd）
static const char* kOutDepth     = "sick_depth.png";        // 深度图输出（16 位 PNG，cwd）
static const char* kOutDepthView = "sick_depth_view.png";   // 深度图可视输出（8 位 PNG，cwd）
static const char* kOutDepthFov  = "sick_depth_fov.png";    // FOV 标注图（框+序号，cwd）

int main(int argc, char* argv[])
{
    // ---- 0. 数据目录参数化（argv[1..] = 各相机扫描目录，缺省用内置路径） ----
    std::vector<std::string> folders;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i)
            folders.push_back(argv[i]);
    } else {
        folders.assign(kFolders, kFolders + kDeviceCount);
    }

    // 数据目录不存在或没有任何 .dat → 打印 SKIP 返回 77（ctest 惯例的跳过码），
    // 不进入后续流程（LoadRaw 会因读不到数据抛异常/挂死）。
    for (const std::string& f : folders) {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((f + "/*.dat").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) {
            std::printf("SKIP: 扫描数据目录不存在或无 .dat 文件: %s\n", f.c_str());
            std::printf("      （用法: Point3DAlg_test.exe <相机1目录> [相机2目录 ...]，缺省 %s）\n",
                        kFolders[0]);
            return 77;
        }
        FindClose(h);
    }

    // ---- 1. 读 json 配置（提取不到必需值报错退出） ----
    Point3DAlg_SickConfig cfg;
    char err[512] = { 0 };
    const int rc = Point3DAlg_LoadSickConfig(nullptr, nullptr, &cfg, err, sizeof(err));
    if (rc != 0) {
        std::printf("配置读取失败(错误码 %d): %s\n", rc, err);
        return 1;
    }
    std::printf("配置读取成功: mm_pulse=%g x_scale=%g y_scale=%g z_scale=%g z_offset=%g comp(%g,%g)+%d阶\n",
                cfg.mm_pulse, cfg.x_scale, cfg.y_scale, cfg.z_scale, cfg.z_offset,
                cfg.comp_fit0, cfg.comp_fit1, cfg.comp_count);

    // ---- 2. 创建对象（构造时接收 SICK 相关参数）→ 加载原始图像（多相机） ----
    try {
        Point3DAlg_SickReader reader(cfg);
        std::printf("加载: %d 相机, frame=%d\n", (int)folders.size(), kFrame);
        for (const std::string& f : folders)
            std::printf("  %s\n", f.c_str());
        auto t0 = std::chrono::steady_clock::now();
        std::fflush(stdout);
        reader.LoadRaw(folders, kFrame);
        std::printf("  LoadRaw 完成\n"); std::fflush(stdout);
        auto t1 = std::chrono::steady_clock::now();
        std::printf("  LoadRaw: %.0f ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count());
        std::printf("  原始图(相机0): %lld 行 × %d 列, 编码器首行 A=%d, 扫描行(pass)数=%d\n",
                    reader.Rows(), reader.Width(), reader.Encoder()[0],
                    (int)reader.RowY().size());
        std::fflush(stdout);

        // ---- 3. 矫正后的拼接深度图 ----
        auto t2 = std::chrono::steady_clock::now();
        std::printf("  GetDepth 开始\n"); std::fflush(stdout);
        const float* depth = reader.GetDepth();
        std::printf("  GetDepth 返回\n"); std::fflush(stdout);
        auto t3 = std::chrono::steady_clock::now();
        std::printf("  GetDepth: %.0f ms\n", std::chrono::duration<double, std::milli>(t3 - t2).count());
        const long long sRows = reader.StitchRows();
        const long long sCols = reader.StitchCols();
        std::printf("  拼接深度图: %lld 行 × %lld 列\n", sRows, sCols);
        float zmin = 1e30f, zmax = -1e30f;
        for (long long i = 0; i < sRows * sCols; ++i) {
            if (depth[i] == 0.0f) continue;
            if (depth[i] < zmin) zmin = depth[i];
            if (depth[i] > zmax) zmax = depth[i];
        }
        std::printf("  深度图: z [%.5f, %.5f] mm\n", zmin, zmax);

        // ---- 4. 基于拼接深度图生成点云 ----
        auto t4 = std::chrono::steady_clock::now();
        int count = 0;
        const float* cloud = reader.GetCloud(&count);
        auto t5 = std::chrono::steady_clock::now();
        std::printf("  GetCloud: %.0f ms\n", std::chrono::duration<double, std::milli>(t5 - t4).count());
        float xmin = cloud[0], xmax = cloud[0];
        float ymin = cloud[1], ymax = cloud[1];
        for (int i = 1; i < count; ++i) {
            const float x = cloud[i * 3], y = cloud[i * 3 + 1];
            if (x < xmin) xmin = x; if (x > xmax) xmax = x;
            if (y < ymin) ymin = y; if (y > ymax) ymax = y;
        }
        std::printf("  点云: %d 点, x [%.4f, %.4f]  y [%.4f, %.4f]  z [%.5f, %.5f] (mm)\n",
                    count, xmin, xmax, ymin, ymax, zmin, zmax);

        // ---- 5. 保存点云(PLY) / 深度图(16位 + 可视8位 + FOV标注) ----
        const int rc1 = Point3DAlg_SaveCloud(kOutCloud, cloud, count, 1);
        std::printf("  保存点云 %s: %s\n", kOutCloud, rc1 == 0 ? "成功" : "失败");
        const int rc2 = Point3DAlg_SaveDepth(kOutDepth, depth,
                                            (int)sRows, (int)sCols, 0, 0);
        std::printf("  保存深度图 %s: %s\n", kOutDepth, rc2 == 0 ? "成功" : "失败");
        const int rc3 = Point3DAlg_SaveDepthView(kOutDepthView, depth,
                                                (int)sRows, (int)sCols, 0, 0);
        std::printf("  保存可视深度图 %s: %s\n", kOutDepthView, rc3 == 0 ? "成功" : "失败");
        // FOV 标注图：框出每个 FOV 的覆盖范围 + 序号（相机:FOV）
        std::vector<int> fovRects, fovIds;
        const int nFov = reader.FovBoxes(fovRects, fovIds);
        std::printf("  FOV 数量: %d\n", nFov);
        const int rc4 = Point3DAlg_DrawFovBoxes(kOutDepthFov, depth,
                                               (int)sRows, (int)sCols, 0, 0,
                                               fovRects.data(), fovIds.data(), nFov);
        std::printf("  保存FOV标注图 %s: %s\n", kOutDepthFov, rc4 == 0 ? "成功" : "失败");
        if (rc1 != 0 || rc2 != 0 || rc3 != 0 || rc4 != 0)
            return 2;
    } catch (const std::exception& e) {
        std::printf("失败: %s\n", e.what());
        return 3;
    }
    std::printf("全部测试通过\n");
    return 0;
}
