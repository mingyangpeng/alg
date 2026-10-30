// Point3DAlg_SickReader.cpp
// SICK Ranger3 扫描数据读取类 + 独立 json 配置读取/保存接口。
// 数据布局与换算链路见 include/Point3DAlg_SickReader.h 头部注释。
// 纯 C++ 实现（无 OpenCV/PCL 依赖）；PNG 保存为自写最小实现（zlib 无压缩存储块）。

#include "include/Point3DAlg_SickReader.h"
#include "include/Point3DAlg_Json.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#endif

#if defined(_MSC_VER)
#define FSEEK64 _fseeki64
#define FTELL64 _ftelli64
#else
#define FSEEK64 fseeko
#define FTELL64 ftello
#endif

namespace {

constexpr int    kSickWidth   = 2560;   // 每行轮廓像素数（Ranger3）
constexpr int    kEncRecBytes = 20;     // 每行编码器记录字节数（5 × int32）
constexpr double kMmPulseDef  = 0.0002; // 编码器脉冲→mm 兜底值

const char* kDefaultRecipePath = "Z:/Calibrate/3D/MtxJuxin60003D.dll.user.json";
const char* kDefaultCommonPath = "Z:/Juxin60003D_CommonParams.json";

// ---------------- 文件工具 ----------------

std::vector<std::string> ListFiles(const std::string& folder, const std::string& ext)
{
    std::vector<std::string> out;
    auto endsWith = [&ext](const std::string& name) {
        return name.size() >= ext.size() &&
               name.compare(name.size() - ext.size(), ext.size(), ext) == 0;
    };
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    const std::string pattern = folder + "/*" + ext;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && endsWith(fd.cFileName))
                out.push_back(folder + "/" + fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR* d = opendir(folder.c_str());
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) != nullptr)
            if (endsWith(e->d_name))
                out.push_back(folder + "/" + e->d_name);
        closedir(d);
    }
#endif
    std::sort(out.begin(), out.end());
    return out;
}

// 解析 Scan3DPart*.json 的 "Coordinate 3D"，取每个子数组 [x1, x2, y, w] 的第 3 个数（行中心 Y）
bool ReadCoordinateJsonY(const std::string& jsonPath, std::vector<double>& rowY)
{
    JsonDoc doc;
    if (!doc.load(jsonPath.c_str()))
        return false;
    const JsonValue coords = doc["Coordinate 3D"];
    const size_t n = coords.size();
    for (size_t i = 0; i < n; ++i) {
        const std::vector<double> v = coords.at(i).array();
        if (v.size() < 3)
            break;
        rowY.push_back(v[2]);
    }
    return !rowY.empty();
}

} // namespace

// =====================================================================
// json 配置读取（独立接口）：提取不到必需值 → 报错退出（返回非 0 + err_msg）
// =====================================================================

int Point3DAlg_LoadSickConfig(const char* recipe_path, const char* common_path,
                              Point3DAlg_SickConfig* cfg,
                              char* err_msg, int err_len)
{
    auto fail = [&](int code, const char* msg) -> int {
        if (err_msg && err_len > 0)
            snprintf(err_msg, (size_t)err_len, "%s", msg);
        return code;
    };
    if (!cfg)
        return fail(-1, "cfg 参数为空");
    memset(cfg, 0, sizeof(*cfg));
    cfg->mm_pulse = kMmPulseDef;

    // ---- 1. 用户配方（recipe） ----
    const char* rp = (recipe_path && recipe_path[0]) ? recipe_path : kDefaultRecipePath;
    JsonDoc recipe;
    if (!recipe.load(rp))
        return fail(1, (std::string("配方文件不存在: ") + rp).c_str());
    if (!recipe["scanpulses"].has())
        return fail(2, "配方缺 scanpulses");
    const std::vector<double> xyz = recipe["scaleXYZ"].array();
    if (xyz.size() < 4)
        return fail(2, "配方缺 scaleXYZ（或不足 4 个分量）");
    const std::string lens = recipe["LensRatio"].string_();
    if (lens.empty())
        return fail(3, "配方缺 LensRatio");

    cfg->x_scale  = recipe["scanpulses"].float_();
    cfg->y_scale  = xyz[1];
    cfg->z_scale  = xyz[2];
    cfg->z_offset = xyz[3];

    // ---- 2. 通用参数（common） ----
    const char* cp = (common_path && common_path[0]) ? common_path : kDefaultCommonPath;
    JsonDoc common;
    if (!common.load(cp))
        return fail(4, (std::string("通用参数文件不存在: ") + cp).c_str());
    if (!common["mmPulse"].has())
        return fail(5, "通用参数缺 mmPulse");
    cfg->mm_pulse = common["mmPulse"].float_(kMmPulseDef);

    // 补偿组：LensRatio "5x"→[0]，"10x"→[1]（与 Juxin Params.cpp:152-153 一致）
    const int idx = (lens == "5x") ? 0 : (lens == "10x") ? 1 : -1;
    if (idx < 0)
        return fail(6, (std::string("LensRatio 不支持: ") + lens + "（仅 5x/10x）").c_str());
    const std::vector<double> fit = common["compensate_All"].group(idx);
    if (fit.size() < 2)
        return fail(6, "通用参数缺 compensate_All 补偿组（或不足 2 个系数）");
    const std::vector<double> poly = common["compensateValue_All"].group(idx);   // 高阶可缺省
    cfg->comp_fit0 = fit[0];
    cfg->comp_fit1 = fit[1];
    cfg->comp_count = (int)std::min<size_t>(poly.size(), 6);
    for (int i = 0; i < cfg->comp_count; ++i)
        cfg->comp_poly[i] = poly[(size_t)i];
    return 0;
}

// =====================================================================
// 保存接口（独立）
// =====================================================================

int Point3DAlg_SaveCloud(const char* path, const float* xyz, int count, int binary)
{
    if (!path || !path[0] || !xyz || count <= 0)
        return -1;
    FILE* f = fopen(path, "wb");
    if (!f)
        return -2;
    // PLY 格式：binary=1 二进制 little-endian，0 ASCII 文本
    if (binary) {
        if (fprintf(f, "ply\nformat binary_little_endian 1.0\n"
                       "element vertex %d\n"
                       "property float x\nproperty float y\nproperty float z\n"
                       "end_header\n", count) < 0) {
            fclose(f);
            return -3;
        }
        if (fwrite(xyz, sizeof(float) * 3, (size_t)count, f) != (size_t)count) {
            fclose(f);
            return -3;
        }
    } else {
        if (fprintf(f, "ply\nformat ascii 1.0\n"
                       "element vertex %d\n"
                       "property float x\nproperty float y\nproperty float z\n"
                       "end_header\n", count) < 0) {
            fclose(f);
            return -3;
        }
        for (int i = 0; i < count; ++i)
            if (fprintf(f, "%.6f %.6f %.6f\n", xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]) < 0) {
                fclose(f);
                return -3;
            }
    }
    fclose(f);
    return 0;
}

// ---- 最小 PNG 写出（zlib 无压缩存储块 + CRC32） ----

namespace {

uint32_t Crc32(const uint8_t* data, size_t n, uint32_t crc)
{
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void PutU32(std::vector<uint8_t>& b, uint32_t v)
{
    b.push_back((uint8_t)(v >> 24)); b.push_back((uint8_t)(v >> 16));
    b.push_back((uint8_t)(v >> 8));  b.push_back((uint8_t)v);
}

// 追加一个完整 chunk（type + data + CRC）
void PutChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
    PutU32(out, (uint32_t)data.size());
    const size_t typePos = out.size();
    out.insert(out.end(), { (uint8_t)type[0], (uint8_t)type[1], (uint8_t)type[2], (uint8_t)type[3] });
    out.insert(out.end(), data.begin(), data.end());
    PutU32(out, Crc32(&out[typePos], out.size() - typePos, 0));
}

// bitDepth=8 或 16 的灰度 PNG；px 为逐像素值（8 位时取低 8 位）
bool WritePngGray(const char* path, const uint16_t* px, int rows, int cols, int bitDepth)
{
    std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

    std::vector<uint8_t> ihdr;
    PutU32(ihdr, (uint32_t)cols);
    PutU32(ihdr, (uint32_t)rows);
    ihdr.push_back((uint8_t)bitDepth);   // bit depth
    ihdr.push_back(0);    // color type: gray
    ihdr.push_back(0);    // compression
    ihdr.push_back(0);    // filter
    ihdr.push_back(0);    // interlace
    PutChunk(out, "IHDR", ihdr);

    // 原始数据：每行 1 字节 filter(0) + cols × 2 字节大端
    // 原始数据：每行 1 字节 filter(0) + 像素（16 位大端 2 字节 / 8 位 1 字节）
    const size_t bpc = (bitDepth == 16) ? 2 : 1;
    std::vector<uint8_t> raw;
    raw.reserve((size_t)rows * ((size_t)cols * bpc + 1));
    for (int r = 0; r < rows; ++r) {
        raw.push_back(0);
        for (int c = 0; c < cols; ++c) {
            const uint16_t v = px[(size_t)r * cols + c];
            if (bitDepth == 16) {
                raw.push_back((uint8_t)(v >> 8));
                raw.push_back((uint8_t)v);
            } else {
                raw.push_back((uint8_t)v);
            }
        }
    }
    // zlib 头（无压缩）+ 存储块 + adler32
    std::vector<uint8_t> idat = { 0x78, 0x01 };
    size_t off = 0;
    while (off < raw.size()) {
        const size_t chunk = std::min<size_t>(raw.size() - off, 0xFFFF);
        const uint8_t finalFlag = (off + chunk >= raw.size()) ? 1 : 0;
        idat.push_back(finalFlag);
        idat.push_back((uint8_t)(chunk & 0xFF));
        idat.push_back((uint8_t)(chunk >> 8));
        idat.push_back((uint8_t)~(chunk & 0xFF));
        idat.push_back((uint8_t)~((chunk >> 8) & 0xFF));
        idat.insert(idat.end(), raw.begin() + (long)off, raw.begin() + (long)(off + chunk));
        off += chunk;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    PutU32(idat, (b << 16) | a);
    PutChunk(out, "IDAT", idat);
    PutChunk(out, "IEND", {});

    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

} // namespace

// z 映射范围：zmin/zmax 传 0 时按数据自适应（仅有效点，0 = 无效）。
// 无有效点返回 false。
static bool AutoZRange(const float* depth, long long n, double& zmin, double& zmax)
{
    if (zmin != 0.0 || zmax != 0.0)
        return true;
    double lo = 1e30, hi = -1e30;
    for (long long i = 0; i < n; ++i)
        if (depth[i] != 0.0f) {
            if (depth[i] < lo) lo = depth[i];
            if (depth[i] > hi) hi = depth[i];
        }
    if (lo > hi)
        return false;
    zmin = lo;
    zmax = hi;
    return true;
}

int Point3DAlg_SaveDepth(const char* path, const float* depth, int rows, int cols,
                         double zmin, double zmax)
{
    if (!path || !path[0] || !depth || rows <= 0 || cols <= 0)
        return -1;
    if (!AutoZRange(depth, (long long)rows * cols, zmin, zmax))
        return -3;   // 无有效点
    if (zmax <= zmin)
        zmax = zmin + 1.0;

    // 16 位深度 PNG（z 线性映射 0..65535，无效点写 0）
    std::vector<uint16_t> px((size_t)rows * cols);
    const double k = 65535.0 / (zmax - zmin);
    for (long long i = 0; i < (long long)rows * cols; ++i) {
        if (depth[i] == 0.0f) {
            px[(size_t)i] = 0;   // 无效点
            continue;
        }
        double v = (depth[i] - zmin) * k;
        if (v < 0) v = 0;
        if (v > 65535) v = 65535;
        px[(size_t)i] = (uint16_t)v;
    }
    return WritePngGray(path, px.data(), rows, cols, 16) ? 0 : -2;
}

int Point3DAlg_SaveDepthView(const char* path, const float* depth, int rows, int cols,
                             double zmin, double zmax)
{
    if (!path || !path[0] || !depth || rows <= 0 || cols <= 0)
        return -1;
    if (!AutoZRange(depth, (long long)rows * cols, zmin, zmax))
        return -3;   // 无有效点
    if (zmax <= zmin)
        zmax = zmin + 1.0;

    // 8 位可视 PNG：z 线性映射 0..255（低=暗 高=亮），无效点写 0
    std::vector<uint16_t> px((size_t)rows * cols);
    const double k = 255.0 / (zmax - zmin);
    for (long long i = 0; i < (long long)rows * cols; ++i) {
        if (depth[i] == 0.0f) {
            px[(size_t)i] = 0;   // 无效点
            continue;
        }
        double v = (depth[i] - zmin) * k;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        px[(size_t)i] = (uint16_t)v;
    }
    return WritePngGray(path, px.data(), rows, cols, 8) ? 0 : -2;
}

// FOV 框标注：8 位 RGB 可视图上画彩色框 + FOV 序号（OpenCV 绘制）。
// rects[i*4..] = {x, y, w, h}（像素）；ids[i] = 序号（NULL 用下标 i）。
// 序号格式化为 "d:j"（id >= 1000 时拆为相机 d 与 FOV j）。
// 每个 FOV 的框颜色随机（按 id 哈希生成，饱和彩色，与灰度底图区分明显）。
int Point3DAlg_DrawFovBoxes(const char* path,
                            const float* depth, int rows, int cols,
                            double zmin, double zmax,
                            const int* rects, const int* ids, int n)
{
    if (!path || !path[0] || !depth || rows <= 0 || cols <= 0)
        return -1;
    if (!AutoZRange(depth, (long long)rows * cols, zmin, zmax))
        return -3;   // 无有效点
    if (zmax <= zmin)
        zmax = zmin + 1.0;

    // 8 位可视底图（灰度）：z 线性映射 0..255（低=暗 高=亮），无效点写 0
    cv::Mat img(rows, cols, CV_8UC3, cv::Scalar(0, 0, 0));
    const double k = 255.0 / (zmax - zmin);
    for (int r = 0; r < rows; ++r) {
        const float* src = depth + (size_t)r * cols;
        cv::Vec3b* dst = img.ptr<cv::Vec3b>(r);
        for (int c = 0; c < cols; ++c) {
            if (src[c] == 0.0f)
                continue;   // 无效点保持黑
            double v = ((double)src[c] - zmin) * k;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            const uint8_t g = (uint8_t)v;
            dst[c] = cv::Vec3b(g, g, g);
        }
    }

    // 画 FOV 框（每个 FOV 随机彩色，粗线）与序号（框左上角，OpenCV 文字）
    for (int i = 0; i < n; ++i) {
        const int x = rects[i*4], y = rects[i*4+1];
        const int w = rects[i*4+2], h = rects[i*4+3];
        if (w <= 0 || h <= 0)
            continue;
        const int id = ids ? ids[i] : i;
        // 随机彩色（按 id 哈希，HSV 全彩 -> BGR）
        const int hue = (id * 47) % 360;
        cv::Mat hsv(1, 1, CV_8UC3, cv::Vec3b((uint8_t)(hue / 2), 255, 255));
        cv::Mat bgr;
        cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);
        const cv::Scalar boxColor(bgr.at<cv::Vec3b>(0, 0));

        // 框（3 像素粗）
        cv::rectangle(img, cv::Rect(x, y, w, h), boxColor, 3);
        // 序号文本（框左上角，白底色块 + 深色大字，保证可读）
        char label[32];
        if (id >= 1000)
            snprintf(label, sizeof(label), "%d:%d", id / 1000, id % 1000);
        else
            snprintf(label, sizeof(label), "%d", id);
        const double fontScale = 1.2;
        const int thickness = 2;
        int baseline = 0;
        const cv::Size ts = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                             fontScale, thickness, &baseline);
        // 标签底色块（框色加深），文字用全白，粗字
        cv::Rect labelRoi(x + 4, y + 4, ts.width + 10, ts.height + baseline + 10);
        labelRoi &= cv::Rect(0, 0, cols, rows);
        if (!labelRoi.empty())
            cv::rectangle(img, labelRoi, cv::Scalar(0, 0, 0), cv::FILLED);
        cv::putText(img, label, cv::Point(x + 9, y + 9 + ts.height),
                    cv::FONT_HERSHEY_SIMPLEX, fontScale, boxColor, thickness, cv::LINE_AA);
    }
    return cv::imwrite(path, img) ? 0 : -2;
}

// =====================================================================
// SickReader 类
// =====================================================================

Point3DAlg_SickReader::Point3DAlg_SickReader(const Point3DAlg_SickConfig& cfg)
    : cfg_(cfg), width_(kSickWidth), rows_(0), cloudCount_(0),
      stitchRows_(0), stitchCols_(0)
{
}

void Point3DAlg_SickReader::LoadRaw(const std::vector<std::string>& folders, int frame)
{
    if (cfg_.y_scale <= 0.0 || cfg_.z_scale <= 0.0)
        throw std::runtime_error("SICK 参数无效：y_scale/z_scale <= 0（检查 LoadSickConfig）");
    if (folders.empty())
        throw std::runtime_error("folders 为空");

    devices_.clear();
    devices_.reserve(folders.size());

    // ---- 阶段 1：每相机只读编码器块 + .xml，建 FOV 结构 ----
    // 换行（pass）、方向（蛇形）、行偏移（运动表）、上下裁剪 ROI、左右列范围全部在此确定。
    for (const std::string& folder : folders) {
        Device dev;
        dev.folder = folder;
        ScanDevice(dev, frame);
        devices_.push_back(std::move(dev));
    }

    // 运动表：全机共用一份（Z:/ScanImage/3D/Scan3DPart*.json，同一运动台）。
    // 各相机文件夹里没有 json 时，从第一个找到运动表的相机取；所有相机统一用该表
    // 计算行偏移（行序一致：pass 按编码器分组，与运动表行一一对应）。
    {
        const std::vector<double>* shared = nullptr;
        for (const Device& dev : devices_)
            if (!dev.rowY.empty()) { shared = &dev.rowY; break; }
        if (shared)
            for (Device& dev : devices_)
                if (&dev.rowY != shared)
                    dev.rowY = *shared;
    }
    // 统一按共用运动表确定每个 FOV 的行偏移与上下裁剪 ROI（视野重叠）
    for (Device& dev : devices_)
        AssignFovRows(dev);

    // ---- 阶段 2：多线程按 FOV 读取 .dat 图像块 ----
    for (Device& dev : devices_)
        LoadFovImages(dev);

    // 相机 0 的原始数据对外可见（Raw()/Encoder()/Rows() 访问）
    {
        const Device& d0 = devices_[0];
        raw_.clear();
        enc_.clear();
        rows_ = 0;
        for (const Fov& f : d0.fovs) {
            raw_.insert(raw_.end(), f.raw.begin(), f.raw.end());
            enc_.insert(enc_.end(), f.encRow.begin(), f.encRow.end());
            rows_ += f.lines;
        }
        rowY_    = d0.rowY;
        width_   = kSickWidth;
    }
    // 派生数据失效，待重算
    depth_.clear();
    cloud_.clear();
    cloudCount_ = 0;
    stitchRows_ = 0;
    stitchCols_ = 0;
}

// 阶段 1：只读编码器块 + .xml，建 FOV 结构。
// 每个 .dat = 一个 FOV；换行判断（方向翻转/步距断裂）与 Juxin UpdateFovData 一致。
// 此阶段结束即确定：pass 序号、蛇形方向、拼接图尺寸（列范围）——图像块尚未读取。
void Point3DAlg_SickReader::ScanDevice(Device& dev, int frame)
{
    const std::string& folder = dev.folder;

    std::vector<std::string> datPaths = ListFiles(folder, ".dat");
    if (datPaths.empty())
        throw std::runtime_error("文件夹无 .dat 数据: " + folder);
    if (frame >= 0) {   // 指定帧：只读第 frame 个 .dat（按文件名顺序，从 0 计）
        if ((size_t)frame >= datPaths.size())
            throw std::runtime_error("帧号越界: " + std::to_string(frame) +
                                     "（共 " + std::to_string(datPaths.size()) + " 个 .dat）");
        datPaths.assign(1, datPaths[(size_t)frame]);
    }

    // ---- 逐文件只读编码器块（seek 跳过图像块，快） ----
    dev.fovs.clear();
    dev.fovs.reserve(datPaths.size());
    for (const std::string& p : datPaths) {
        FILE* f = fopen(p.c_str(), "rb");
        if (!f)
            throw std::runtime_error("数据文件打开失败: " + p);
        FSEEK64(f, 0, SEEK_END);
        const long long size = FTELL64(f);
        const long long rowBytes = (long long)kSickWidth * 2 + kEncRecBytes;
        const long long lines = size / rowBytes;   // 尾部不足一行时忽略
        FSEEK64(f, lines * (long long)kSickWidth * 2, SEEK_SET);   // 跳过图像块
        long long encAvail = (size - lines * (long long)kSickWidth * 2) / kEncRecBytes;
        if (encAvail > lines)
            encAvail = lines;
        std::vector<int32_t> a((size_t)lines, 0);
        if (encAvail > 0) {
            std::vector<int32_t> tmp((size_t)encAvail * 5);
            if (fread(tmp.data(), kEncRecBytes, (size_t)encAvail, f) != (size_t)encAvail)
                encAvail = 0;
            else
                for (long long i = 0; i < encAvail; ++i)
                    a[(size_t)i] = tmp[(size_t)i * 5];
        }
        fclose(f);
        if (lines <= 0)
            continue;

        // 尾部 0 值编码器按线性外推补全（与 Juxin ReadSickData chunkFlag=2 一致）
        long long endPos = -1;
        for (long long i = 0; i < lines; ++i)
            if (a[(size_t)i] != 0)
                endPos = i;
        if (endPos >= 0 && endPos < lines - 1) {
            const int32_t stepEnc = (endPos > 0)
                ? (int32_t)llround((double)(a[(size_t)endPos] - a[0]) / (double)endPos)
                : 0;
            for (long long i = endPos + 1; i < lines; ++i)
                a[(size_t)i] = a[(size_t)i - 1] + stepEnc;
        }

        Fov fov;
        fov.path    = p;
        fov.lines   = lines;
        fov.aFirst  = a[0];
        fov.aLast   = a[(size_t)(lines - 1)];
        fov.down    = fov.aLast < fov.aFirst;   // A 递减 = 蛇形返程段
        fov.encRow  = std::move(a);              // 每行编码器值（拼接写入用）
        dev.fovs.push_back(std::move(fov));
    }
    if (dev.fovs.empty())
        throw std::runtime_error("文件夹无有效 .dat 数据: " + folder);

    // ---- scanStep 自适应：相邻行 |dA| 的众数（参考数据为 30） ----
    std::unordered_map<int32_t, int> dCount;
    {
        const int32_t* prev = nullptr;
        for (const Fov& f : dev.fovs)
            for (long long i = 0; i < f.lines; ++i) {
                const int32_t* cur = &f.encRow[(size_t)i];
                if (prev)
                    ++dCount[(int32_t)llabs((long long)*cur - (long long)*prev)];
                prev = cur;
            }
    }
    int32_t scanStep = 30;
    int bestCount = -1;
    for (const auto& kv : dCount)
        if (kv.second > bestCount) {
            bestCount = kv.second;
            scanStep = kv.first;
        }
    dev.scanStep = (int)scanStep;
    const int32_t tol = std::max<int32_t>(2, scanStep / 5);

    // ---- 换行判断（与 Juxin UpdateFovData / Sick3DReader groupRows 一致） ----
    // FOV 内逐行：方向翻转或 |dA| 超步距容差 -> 新 pass。
    // FOV 之间：上一 FOV 末行与本 FOV 首行的编码器跳变同样判断。
    dev.passDown.clear();
    {
        int curPass = 0;
        int prevDir = 0;
        const int32_t* prevA = nullptr;
        bool passIsDown = false;
        for (Fov& f : dev.fovs) {
            for (long long li = 0; li < f.lines; ++li) {
                const int32_t a = f.encRow[(size_t)li];
                if (prevA) {
                    const int32_t d = a - *prevA;
                    const int ad = (d > tol ? 1 : 0) - (d < -tol ? 1 : 0);
                    const bool gap = (d > scanStep + tol) || (d < -(scanStep + tol));
                    const bool flip = (prevDir != 0 && ad != 0 && ad != prevDir);
                    if (gap || flip) {
                        ++curPass;
                        prevDir = 0;
                        passIsDown = false;
                    } else if (ad != 0) {
                        prevDir = ad;
                        passIsDown = (ad < 0);
                    }
                }
                // FOV 归属：首行定 pass（FOV 中间换行罕见，整 FOV 归新 pass 简化处理）
                if (li == 0) {
                    f.pass = curPass;
                    f.down = passIsDown;
                }
                prevA = &f.encRow[(size_t)li];
            }
            dev.passDown.push_back(f.down ? 1 : 0);
        }
        dev.nPass = curPass + 1;
    }

    // ---- 拼接图列范围（编码器跨度） ----
    int32_t aMin = 0, aMax = 0;
    bool first = true;
    for (const Fov& f : dev.fovs) {
        const int32_t lo = std::min(f.aFirst, f.aLast);
        const int32_t hi = std::max(f.aFirst, f.aLast);
        if (first || lo < aMin) aMin = lo;
        if (first || hi > aMax) aMax = hi;
        first = false;
    }
    dev.aMin = aMin;
    dev.aMax = aMax;
    dev.stitchCols = (dev.scanStep > 0 && !first)
        ? (long long)((aMax - aMin) / dev.scanStep) + 1
        : 0;

    // 每个 FOV 的列范围（ROI：左右边界）。
    // 列方向与 Juxin 一致（rotate 90 CCW 数值验证）：A 小 → 列小
    // （up: 列 = 原行正向；down flip 后列反向，统一 A 小 → 列小）。
    for (Fov& f : dev.fovs) {
        const int32_t lo = std::min(f.aFirst, f.aLast);
        const int32_t hi = std::max(f.aFirst, f.aLast);
        f.colFirst = (dev.scanStep > 0) ? (long long)llround(((double)lo - (double)aMin) / (double)dev.scanStep) : 0;
        f.colLast  = (dev.scanStep > 0) ? (long long)llround(((double)hi - (double)aMin) / (double)dev.scanStep) : 0;
    }

    // ---- 行中心 Y：Scan3DPart*.json（运动表，全机共用一份） ----
    dev.rowY.clear();
    for (const std::string& jp : ListFiles(folder, ".json"))
        if (jp.find("Scan3DPart") != std::string::npos)
            ReadCoordinateJsonY(jp, dev.rowY);
}

// 按共用运动表确定每个 FOV 的行偏移与上下裁剪 ROI（视野重叠）。
// 与 Juxin MxtStitchingDepthImg 的 overRows 逻辑一致：
//   overRows = 视野高 - 行中心间距（>0 即重叠）；裁掉上半 overRows/2，下半随下一行裁剪。
// FOV 的行偏移 = pass 行偏移 + 本 FOV 首行相对 pass 首行的推进量。
void Point3DAlg_SickReader::AssignFovRows(Device& dev)
{
    const double fovH = (double)kSickWidth * cfg_.y_scale;   // 行视野高度 mm

    // pass 行偏移（运动表行距；缺表按视野高度无重叠堆叠）
    dev.rowOff.assign((size_t)dev.nPass, 0);
    for (int p = 1; p < dev.nPass; ++p) {
        const double dy = ((size_t)p < dev.rowY.size() && (size_t)(p - 1) < dev.rowY.size())
            ? std::fabs(dev.rowY[(size_t)p] - dev.rowY[(size_t)(p - 1)])
            : fovH;
        dev.rowOff[(size_t)p] = dev.rowOff[(size_t)(p - 1)] +
                                (long long)llround(dy / cfg_.y_scale);
    }
    dev.stitchRows = (dev.nPass > 0)
        ? dev.rowOff[(size_t)(dev.nPass - 1)] + kSickWidth
        : 0;

    // 每个 FOV：行偏移（Y/轮廓方向）+ 轮廓方向裁剪 ROI
    // 行方向已反转（rr = rowOff + W-1-c，与 Juxin rotate CCW 一致）：
    //   c 大端 → 拼接图顶部（行小），c 小端 → 底部（行大）。
    // 裁剪（与 Juxin overRows/2 一致）：pass>0 裁顶部（c 大端），
    // 既裁掉与上一 pass 的视野重叠，也裁掉视野边缘坏像素（两端 ~1mm 异常值）。
    // 底部（c 小端）由下一 pass 的顶部覆盖语义处理；首/末 pass 不越界裁。
    const long long overHalf = [&] {
        if (dev.nPass < 2 || dev.rowY.size() < 2)
            return 0LL;
        const double dy = std::fabs(dev.rowY[1] - dev.rowY[0]);   // 行距 mm
        const double overRows = fovH / cfg_.y_scale - dy / cfg_.y_scale;   // 像素
        return (overRows > 0) ? (long long)llround(overRows * 0.5) : 0LL;
    }();
    for (Fov& f : dev.fovs) {
        f.rowOff = dev.rowOff[(size_t)f.pass];
        // 轮廓方向 ROI（c 的有效范围）：
        //   pass>0 裁 c 大端（顶部）overHalf；非末 pass 裁 c 小端（底部）overHalf
        f.cropTop = (f.pass > 0) ? (int)std::min<long long>(overHalf, kSickWidth / 2) : 0;   // 裁 c 大端
        f.cropBot = (f.pass < dev.nPass - 1) ? (int)std::min<long long>(overHalf, kSickWidth / 2) : 0;   // 裁 c 小端
    }
}

// 阶段 2：多线程按 FOV 读取 .dat 图像块（每个 FOV 独立文件，天然并行）
void Point3DAlg_SickReader::LoadFovImages(Device& dev)
{
    const size_t n = dev.fovs.size();
    if (n == 0)
        return;
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned nThreads = std::min<unsigned>(hw == 0 ? 4u : hw, (unsigned)n);

    std::atomic<size_t> next(0);
    std::atomic<int>    fail(0);
    std::string         failPath;

    auto worker = [&]() {
        while (fail.load(std::memory_order_relaxed) == 0) {
            const size_t i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= n)
                return;
            Fov& f = dev.fovs[i];
            FILE* fp = fopen(f.path.c_str(), "rb");
            if (!fp) {
                fail.store(1, std::memory_order_relaxed);
                return;
            }
            f.raw.resize((size_t)f.lines * kSickWidth);
            const size_t want = (size_t)(f.lines * (long long)kSickWidth * 2);
            if (fread(f.raw.data(), 1, want, fp) != want) {
                fclose(fp);
                fail.store(1, std::memory_order_relaxed);
                return;
            }
            fclose(fp);
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(nThreads);
    for (unsigned t = 0; t < nThreads; ++t)
        pool.emplace_back(worker);
    for (std::thread& t : pool)
        t.join();

    if (fail.load() != 0)
        throw std::runtime_error("数据文件读取失败（多线程加载阶段）: " + dev.folder);
}

// 单相机拼接深度图（懒生成，写入 dev.depth）—— 基于 FOV 结构直接写入。
// 每个 FOV 自带：行偏移（rowOff）、上下裁剪（cropTop）、列范围（colFirst..colLast）、
// 蛇形方向（down）与图像块（raw），无需全局行索引。
void Point3DAlg_SickReader::BuildDeviceDepth(Device& dev)
{
    if (!dev.depth.empty())
        return;
    // 单机拼接深度图（与 Juxin MxtStitchingDepthImg 语义一致）：
    //   行 = 轮廓轴（2560 × pass 数，含视野重叠裁剪），列 = 扫描轴（编码器行程/scanStep）；
    //   z_mm = raw × z_scale + z_offset - diff(col)（激光倾斜补偿，col 为原始轮廓列）。
    const int W = kSickWidth;
    dev.depth.assign((size_t)dev.stitchRows * (size_t)dev.stitchCols, 0.0f);

    // 补偿查找表：diff 只依赖轮廓列 c（0..2559），预计算一次，避免逐点 pow()
    std::vector<float> compLut((size_t)W);
    for (int c = 0; c < W; ++c) {
        double diff = 0.0;
        if (cfg_.comp_fit0 != 0.0)
            diff += ((double)c - W * 0.5) / cfg_.comp_fit0 * cfg_.comp_fit1;
        double p = 1.0;
        for (int i = 0; i < cfg_.comp_count; ++i) {
            diff += cfg_.comp_poly[i] * p;
            p *= (double)c;
        }
        compLut[(size_t)c] = (float)diff;
    }
    // z 换算也查表：raw 0..65535 → z（无补偿），z = raw*z_scale + z_offset
    std::vector<float> zLut(65536);
    for (int raw = 1; raw < 65536; ++raw)   // 0 = 无效点，保持 0
        zLut[(size_t)raw] = (float)((double)raw * cfg_.z_scale + cfg_.z_offset);

    const long long stride = dev.stitchCols;
    for (const Fov& f : dev.fovs) {
        if (f.raw.empty())
            continue;
        const long long rowOff = f.rowOff;
        for (long long li = 0; li < f.lines; ++li) {   // 扫描方向不裁剪（crop 只作用于轮廓方向）
            // 列号 = (A - aMin)/scanStep（与 Juxin 一致：rotate 90 CCW 数值验证，
            // up 列=原行正向、down flip 后列反向，统一 A 小 → 列小）
            const int32_t a = f.encRow[(size_t)li];
            long long col = (dev.scanStep > 0)
                ? (long long)llround(((double)a - (double)dev.aMin) / (double)dev.scanStep)
                : 0;
            if (col < 0 || col >= stride)
                continue;
            const uint16_t* rowRaw = &f.raw[(size_t)li * W];
            float* dst = &dev.depth[(size_t)col];
            // 轮廓方向裁剪（行反转后：cropTop 裁 c 大端=顶部，cropBot 裁 c 小端=底部）
            for (int c = f.cropBot; c < W - f.cropTop; ++c) {
                const uint16_t raw = rowRaw[c];
                if (raw == 0)
                    continue;   // 0 = 无效点
                const float z = zLut[raw] - compLut[(size_t)c];
                // 旋转 90°（与 Juxin rotate CCW 数值验证一致）：
                //   行 = W-1-轮廓列 c（原列反向：原列 0 → 拼接行 W-1）
                //   列 = (A-aMin)/scanStep（扫描方向）
                // down 的扫描方向反转由列号统一（Juxin 用 flip 实现，等效）。
                const long long rr = rowOff + (W - 1 - c);
                dst[(size_t)rr * (size_t)stride] = z;
            }
        }
    }
}

// 多相机交织合并（与 Juxin MxtStitchingDepthImg v2 的双相机交织一致）：
//   1) 每相机各自拼接成单机图（BuildDeviceDepth）；
//   2) 按各相机编码器最小值 aMin（= 起始脉冲）从小到大排序相机；
//   3) 各单机图左补 0 对齐到最小起始脉冲（列偏移 = (aMin[d]-minPulse)/scanStep）；
//   4) 列交织：合并图第 k 列取排序后第 k%N 台相机的第 k/N 列；
//   5) 左右全零列裁剪。
// 单相机时直接取该相机单机图（含裁剪）。
void Point3DAlg_SickReader::InterleaveMerge()
{
    const size_t n = devices_.size();
    if (n == 0)
        throw std::runtime_error("尚未加载数据（先调用 LoadRaw）");

    for (Device& dev : devices_)
        BuildDeviceDepth(dev);

    if (n == 1) {
        // 单相机：左右全零裁剪后直接作为最终图
        const Device& dev = devices_[0];
        long long left = 0, right = dev.stitchCols - 1;
        for (; left < dev.stitchCols; ++left) {
            bool any = false;
            for (long long r = 0; r < dev.stitchRows && !any; ++r)
                if (dev.depth[(size_t)r * (size_t)dev.stitchCols + (size_t)left] != 0.0f)
                    any = true;
            if (any) break;
        }
        for (; right >= left; --right) {
            bool any = false;
            for (long long r = 0; r < dev.stitchRows && !any; ++r)
                if (dev.depth[(size_t)r * (size_t)dev.stitchCols + (size_t)right] != 0.0f)
                    any = true;
            if (any) break;
        }
        stitchRows_ = dev.stitchRows;
        stitchCols_ = (left <= right) ? (right - left + 1) : 0;
        depth_.assign((size_t)stitchRows_ * (size_t)stitchCols_, 0.0f);
        for (long long r = 0; r < stitchRows_; ++r)
            memcpy(&depth_[(size_t)r * (size_t)stitchCols_],
                   &dev.depth[(size_t)r * (size_t)dev.stitchCols + (size_t)left],
                   (size_t)stitchCols_ * sizeof(float));

        // FOV 框（最终坐标 = 单机坐标 - 裁剪 left）
        fovBoxes_.clear();
        fovBoxIds_.clear();
        for (size_t j = 0; j < dev.fovs.size(); ++j) {
            const Fov& f = dev.fovs[j];
            const long long x0 = std::max(0LL, f.colFirst - left);
            const long long x1 = std::min(stitchCols_ - 1, f.colLast - left);
            const long long y0 = std::max(0LL, f.rowOff);
            const long long y1 = std::min(stitchRows_ - 1, f.rowOff + kSickWidth - 1);
            if (x1 < x0 || y1 < y0)
                continue;
            fovBoxes_.push_back((int)x0);
            fovBoxes_.push_back((int)y0);
            fovBoxes_.push_back((int)(x1 - x0 + 1));
            fovBoxes_.push_back((int)(y1 - y0 + 1));
            fovBoxIds_.push_back((int)j);
        }
        return;
    }

    // ---- 多相机：按起始脉冲（aMin）排序 ----
    std::vector<int> sortedDev(n);
    for (size_t i = 0; i < n; ++i) sortedDev[i] = (int)i;
    std::sort(sortedDev.begin(), sortedDev.end(), [&](int x, int y) {
        return devices_[(size_t)x].aMin < devices_[(size_t)y].aMin;
    });
    const int32_t minPulse = devices_[(size_t)sortedDev[0]].aMin;

    // ---- 左补 0 对齐 + 统一尺寸 ----
    // aligned[d]：行数 maxRows、列数 = offset + 单机列数（先不右补，交织时按需取 0）
    const long long maxRows = [&] {
        long long m = 0;
        for (const Device& dev : devices_)
            m = (dev.stitchRows > m) ? dev.stitchRows : m;
        return m;
    }();
    std::vector<long long> offset(n);
    long long maxCols = 0;   // 对齐后最大列数（offset + 单机列数）
    for (size_t i = 0; i < n; ++i) {
        const Device& dev = devices_[i];
        offset[i] = (dev.scanStep > 0)
            ? (long long)llround(((double)dev.aMin - (double)minPulse) / (double)dev.scanStep)
            : 0;
        if (offset[i] < 0) offset[i] = 0;
        maxCols = (offset[i] + dev.stitchCols > maxCols) ? offset[i] + dev.stitchCols : maxCols;
    }

    // ---- 列交织：合并图第 k 列 = 排序后第 k%n 台相机的第 k/n 列 ----
    // 按行批量拷贝（每行内逐列取，避免跨行随机访问）
    const long long totalCols = maxCols * (long long)n;
    std::vector<float> merged((size_t)maxRows * (size_t)totalCols, 0.0f);
    {
        // 预生成列映射：merged 列 k -> (源相机 d, 源列 srcCol)，-1 表示无数据
        std::vector<int>    srcDev((size_t)totalCols, -1);
        std::vector<long long> srcColOf((size_t)totalCols, -1);
        for (long long k = 0; k < totalCols; ++k) {
            const int d = sortedDev[(size_t)(k % (long long)n)];
            const long long sc = k / (long long)n - offset[(size_t)d];
            if (sc >= 0 && sc < devices_[(size_t)d].stitchCols) {
                srcDev[(size_t)k] = d;
                srcColOf[(size_t)k] = sc;
            }
        }
        for (Device& dev : devices_) {
            // 每台相机逐行拷贝到 merged（该相机拥有的列）
            for (long long r = 0; r < dev.stitchRows; ++r) {
                const float* srcRow = &dev.depth[(size_t)r * (size_t)dev.stitchCols];
                float* dstRow = &merged[(size_t)r * (size_t)totalCols];
                for (long long k = 0; k < totalCols; ++k)
                    if (srcDev[(size_t)k] >= 0) {
                        const float v = srcRow[(size_t)srcColOf[(size_t)k]];
                        if (v != 0.0f)
                            dstRow[(size_t)k] = v;
                    }
            }
        }
    }

    // ---- 左右全零列裁剪 ----
    long long left = 0, right = totalCols - 1;
    for (; left < totalCols; ++left) {
        bool any = false;
        for (long long r = 0; r < maxRows && !any; ++r)
            if (merged[(size_t)r * (size_t)totalCols + (size_t)left] != 0.0f)
                any = true;
        if (any) break;
    }
    for (; right >= left; --right) {
        bool any = false;
        for (long long r = 0; r < maxRows && !any; ++r)
            if (merged[(size_t)r * (size_t)totalCols + (size_t)right] != 0.0f)
                any = true;
        if (any) break;
    }
    stitchRows_ = maxRows;
    stitchCols_ = (left <= right) ? (right - left + 1) : 0;
    depth_.assign((size_t)stitchRows_ * (size_t)stitchCols_, 0.0f);
    for (long long r = 0; r < stitchRows_; ++r)
        memcpy(&depth_[(size_t)r * (size_t)stitchCols_],
               &merged[(size_t)r * (size_t)totalCols + (size_t)left],
               (size_t)stitchCols_ * sizeof(float));

    // ---- 记录 FOV 框（最终拼接图坐标，供可视标注）----
    // 单机列 sc -> 交织列 k = sc*n + rank（rank = 相机在 sortedDev 中的序号），
    // 再减去裁剪 left 得最终列。
    fovBoxes_.clear();
    fovBoxIds_.clear();
    std::vector<int> rankOf(n);
    for (size_t r = 0; r < n; ++r)
        rankOf[(size_t)sortedDev[r]] = (int)r;
    for (size_t d = 0; d < n; ++d) {
        const Device& dev = devices_[d];
        const int rank = rankOf[d];
        for (size_t j = 0; j < dev.fovs.size(); ++j) {
            const Fov& f = dev.fovs[j];
            // 行范围（拼接图行，行反转后 c 大端在顶部）
            const long long y0 = f.rowOff;                       // c=2559 端（顶部）
            const long long y1 = f.rowOff + kSickWidth - 1;      // c=0 端（底部）
            // 列范围：单机列 -> 交织列 -> 裁剪后最终列
            const long long k0 = f.colFirst * (long long)n + rank - left;
            const long long k1 = f.colLast  * (long long)n + rank - left;
            const long long x0 = std::max(0LL, std::min(k0, k1));
            const long long x1 = std::min(stitchCols_ - 1, std::max(k0, k1));
            const long long yy0 = std::max(0LL, std::min(y0, y1));
            const long long yy1 = std::min(stitchRows_ - 1, std::max(y0, y1));
            if (x1 < x0 || yy1 < yy0)
                continue;
            fovBoxes_.push_back((int)x0);
            fovBoxes_.push_back((int)yy0);
            fovBoxes_.push_back((int)(x1 - x0 + 1));
            fovBoxes_.push_back((int)(yy1 - yy0 + 1));
            fovBoxIds_.push_back((int)(d * 1000 + j));   // 相机 d 的 FOV j
        }
    }
}

const float* Point3DAlg_SickReader::GetDepth()
{
    if (devices_.empty())
        throw std::runtime_error("尚未加载数据（先调用 LoadRaw）");
    if (!depth_.empty())
        return depth_.data();

    // 单相机拼接 → 多相机交织合并（见 InterleaveMerge 注释）
    InterleaveMerge();
    if (depth_.empty() || stitchCols_ <= 0)
        throw std::runtime_error("拼接深度图为空（无有效数据）");
    return depth_.data();
}

// FOV 框信息（最终拼接图坐标，GetDepth 之后调用）
int Point3DAlg_SickReader::FovBoxes(std::vector<int>& rects, std::vector<int>& ids) const
{
    rects = fovBoxes_;
    ids   = fovBoxIds_;
    return (int)fovBoxIds_.size();
}

const float* Point3DAlg_SickReader::GetCloud(int* out_count)
{
    if (devices_.empty())
        throw std::runtime_error("尚未加载数据（先调用 LoadRaw）");
    if (!cloud_.empty()) {
        if (out_count)
            *out_count = cloudCount_;
        return cloud_.data();
    }
    (void)GetDepth();   // 依赖拼接深度图

    // 基于拼接深度图生成点云，坐标（mm）：
    //   x = col × (scanStep × mmPulse) / DeviceCount()
    //      扫描向步距用数据实测值（scanStep 自适应 × mmPulse），
    //      不用配方 scanpulses——配方镜头与数据不匹配时会错（如 5x 配方读 10x 数据）。
    //      交织后步距再除以相机数（N 台交织等效采样率 ×N）。
    //   y = (stitchRows/2 - row) × y_scale（轮廓轴，拼接图中心对齐）
    //   z = 深度图值（0 的点跳过）
    // 拼接图已含换行/重叠裁剪/多相机交织，点云直接扫拼接图即可。
    cloud_.reserve(depth_.size() / 2 * 3);
    const double halfRows = (double)stitchRows_ * 0.5;
    const double xStep = (double)devices_[0].scanStep * cfg_.mm_pulse / (double)devices_.size();
    for (long long r = 0; r < stitchRows_; ++r) {
        const double y = (halfRows - (double)r) * cfg_.y_scale;
        for (long long c = 0; c < stitchCols_; ++c) {
            const float z = depth_[(size_t)r * (size_t)stitchCols_ + (size_t)c];
            if (z == 0.0f)
                continue;
            cloud_.push_back((float)((double)c * xStep));
            cloud_.push_back((float)y);
            cloud_.push_back(z);
        }
    }
    if (cloud_.empty())
        throw std::runtime_error("无有效点（数据全为 0）");
    cloudCount_ = (int)(cloud_.size() / 3);
    if (out_count)
        *out_count = cloudCount_;
    return cloud_.data();
}
