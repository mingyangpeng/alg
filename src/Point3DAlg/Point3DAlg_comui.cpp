// ComUI 统一 C ABI 适配层（Point3DAlg）：
// 单一入口签名 + algo_free 释放约定（见 docs/THIRD_PARTY_OPS.md §5.5）。
// 体素下采样：哈希格网 → 每格质心 + 颜色均值（PCL VoxelGrid 的等价简化实现，SoA 直进直出）。
#include "Point3DAlg_export.h"
#include "include/Point3DAlg_api.h"
#include "../comui_native_abi.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unordered_map>

extern "C" {

// ---------------- 统一释放约定 ----------------

POINT3DALG_EXPORT void algo_free(void* p) {
    free(p);
}

// ---------------- 算子入口：体素下采样（node.json: alg.point3d.voxel_downsample） ----------------

POINT3DALG_EXPORT int Point3DAlg_ComuiVoxel(const AlgoCInput* in, AlgoCOutput* out) {
    memset(out, 0, sizeof(*out));
    if (in->cloudCount < 1 || !in->clouds) { out->error = "输入未连线"; return 1; }

    double leaf = 0.01;
    for (int i = 0; i < in->paramCount; i++)
        if (in->params[i].kind == 0) { leaf = in->params[i].d; break; }
    if (leaf < 1e-6) leaf = 1e-6;

    const AlgoCCloud* src = &in->clouds[0];
    int n = src->count;
    const float* xyz = src->xyz;
    const uint8_t* rgb = src->rgb;
    if (n <= 0 || !xyz) { out->error = "输入点云为空"; return 2; }

    // 体素格网：格键 = (ix,iy,iz) 打包；格值 = 点索引（首点代表格）
    // 输出取每格质心与颜色均值（与 PCL VoxelGrid 语义一致）
    struct Acc { double x = 0, y = 0, z = 0; uint64_t r = 0, g = 0, b = 0; int cnt = 0; };
    std::unordered_map<uint64_t, Acc> grid;
    grid.reserve((size_t)n * 2 + 1);
    for (int i = 0; i < n; i++) {
        int64_t ix = (int64_t)floor(xyz[i * 3]     / leaf);
        int64_t iy = (int64_t)floor(xyz[i * 3 + 1] / leaf);
        int64_t iz = (int64_t)floor(xyz[i * 3 + 2] / leaf);
        uint64_t key = (uint64_t)(ix & 0x1FFFFF) << 42 | (uint64_t)(iy & 0x1FFFFF) << 21 | (uint64_t)(iz & 0x1FFFFF);
        auto& acc = grid[key];
        acc.x += xyz[i * 3]; acc.y += xyz[i * 3 + 1]; acc.z += xyz[i * 3 + 2];
        if (rgb) { acc.r += rgb[i * 3]; acc.g += rgb[i * 3 + 1]; acc.b += rgb[i * 3 + 2]; }
        acc.cnt++;
    }

    int m = (int)grid.size();
    float* outXyz = (float*)malloc((size_t)m * 3 * sizeof(float));
    uint8_t* outRgb = rgb ? (uint8_t*)malloc((size_t)m * 3) : nullptr;
    if (!outXyz || (rgb && !outRgb)) { free(outXyz); free(outRgb); out->error = "内存不足"; return 3; }

    int k = 0;
    for (const auto& kv : grid) {
        const auto& a = kv.second;
        outXyz[k * 3]     = (float)(a.x / a.cnt);
        outXyz[k * 3 + 1] = (float)(a.y / a.cnt);
        outXyz[k * 3 + 2] = (float)(a.z / a.cnt);
        if (outRgb) {
            outRgb[k * 3]     = (uint8_t)(a.r / a.cnt);
            outRgb[k * 3 + 1] = (uint8_t)(a.g / a.cnt);
            outRgb[k * 3 + 2] = (uint8_t)(a.b / a.cnt);
        }
        k++;
    }

    AlgoCCloudOut* arr = (AlgoCCloudOut*)malloc(sizeof(AlgoCCloudOut));
    if (!arr) { free(outXyz); free(outRgb); out->error = "内存不足"; return 3; }
    arr->count = m;
    arr->xyz = outXyz;
    arr->rgb = outRgb;

    out->clouds = arr;
    out->cloudCount = 1;
    return 0;
}

// ---------------- 参数读取辅助（SICK 读取用） ----------------

static const char* GetTextParam(const AlgoCInput* in) {
    for (int i = 0; i < in->paramCount; i++)
        if (in->params[i].kind == 2) return in->params[i].s;
    return nullptr;
}

static int GetIntParam(const AlgoCInput* in, int def) {
    for (int i = 0; i < in->paramCount; i++)
        if (in->params[i].kind == 1) return in->params[i].i;
    return def;
}

// ---------------- 算子入口：SICK 扫描读取（node.json: alg.point3d.read_sick） ----------------
// 参数：0=folder(path) 1=frame(int, <0=全部帧)；
// 换算参数自动读取（recipe/common json，默认路径，见 Point3DAlg_api.h 注释）。

POINT3DALG_EXPORT int Point3DAlg_ComuiReadSick(const AlgoCInput* in, AlgoCOutput* out) {
    memset(out, 0, sizeof(*out));
    const char* folder = GetTextParam(in);
    if (!folder || !folder[0]) { out->error = "未设置扫描数据文件夹"; return 1; }
    const int frame = GetIntParam(in, -1);

    float* xyz = nullptr;
    int count = 0;
    const int rc = Point3DAlg_ReadSickCloud(folder, frame, nullptr, nullptr, &xyz, &count);
    if (rc != 0) {
        static char err[512];
        if (rc == -2)      snprintf(err, sizeof(err), "文件夹无 .dat 数据：%s", folder);
        else if (rc == -3) snprintf(err, sizeof(err), "帧号越界（超出 .dat 文件数）：%s", folder);
        else if (rc == -4) snprintf(err, sizeof(err), "读取失败（配置或数据错误）：%s", folder);
        else               snprintf(err, sizeof(err), "读取失败（错误码 %d）：%s", rc, folder);
        out->error = err;
        return 2;
    }

    AlgoCCloudOut* arr = (AlgoCCloudOut*)malloc(sizeof(AlgoCCloudOut));
    if (!arr) { Point3DAlg_FreeBuffer(xyz); out->error = "内存不足"; return 3; }
    arr->count = count;
    arr->xyz = xyz;
    arr->rgb = nullptr;   // 无颜色：查看器按高度伪彩上色
    out->clouds = arr;
    out->cloudCount = 1;
    return 0;
}

} // extern "C"
