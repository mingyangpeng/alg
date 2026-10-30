// Point3DAlg_SickIO.cpp
// SICK 一步式 C 接口：读扫描文件夹 → 点云（内部走 Point3DAlg_SickReader 类链路，
// 换算参数自动从两个 json 读取，见 Point3DAlg_SickReader.h）。

#include "include/Point3DAlg_api.h"
#include "include/Point3DAlg_SickReader.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int Point3DAlg_ReadSickCloud(const char* folder, int frame,
                             const char* recipe_path, const char* common_path,
                             float** out_xyz, int* out_count)
{
    if (!folder || !folder[0] || !out_xyz || !out_count)
        return -1;
    *out_xyz = nullptr;
    *out_count = 0;

    try {
        Point3DAlg_SickConfig cfg;
        char err[256] = { 0 };
        const int rc = Point3DAlg_LoadSickConfig(recipe_path, common_path, &cfg, err, sizeof(err));
        if (rc != 0)
            return -4;

        Point3DAlg_SickReader reader(cfg);
        reader.LoadRaw(std::vector<std::string>{ std::string(folder) }, frame);

        int count = 0;
        const float* cloud = reader.GetCloud(&count);
        float* xyz = (float*)malloc((size_t)count * 3 * sizeof(float));
        if (!xyz)
            return -4;
        memcpy(xyz, cloud, (size_t)count * 3 * sizeof(float));
        *out_xyz = xyz;
        *out_count = count;
        return 0;
    } catch (const std::exception&) {
        return -4;   // 文件夹无 .dat / 帧号越界 / 无有效点等，统一错误码
    }
}

void Point3DAlg_FreeBuffer(void* p)
{
    free(p);
}
