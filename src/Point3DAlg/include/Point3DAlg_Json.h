#ifndef POINT3DALG_JSON_H
#define POINT3DALG_JSON_H

// JSON 读取封装（内部 nlohmann::json，见 thrid/nlohmann_json）。
// 用法：
//   JsonDoc doc;
//   if (!doc.load(path)) ...;                  // 文件不存在
//   double v = doc["scanpulses"].float_(0.0);   // 缺键/类型不符返回默认值
//   std::string s = doc["LensRatio"].string_(); // 缺键返回默认值
//   std::vector<double> a = doc["scaleXYZ"].array();
//   auto g = doc["compensate_All"].group(1);    // 二维数组第 1 组
//   auto row = doc["Coordinate 3D"].at(i);      // 数组第 i 个元素（越界为空值）
//   doc["Coordinate 3D"].size();
//
// GBK 容错：nlohmann 要求严格 UTF-8，而 Juxin 配置文件可能是 GBK 编码
// （如 "LensRatio 镜头倍率 5x 10x" 中文注释键）。load 时先检测编码：
//   1. UTF-8 BOM → 去 BOM 直接 parse；
//   2. 合法 UTF-8 → 直接 parse；
//   3. 其他（GBK 等 ANSI）→ 按 Windows 936 码页转 UTF-8 再 parse。
// 转换后中文键名正确还原（仅注释用途，不影响按键取值）。

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

class JsonValue;

class JsonDoc
{
public:
    // 读文件 → 检测编码 → 转 UTF-8 → 解析；文件不存在或解析失败返回 false
    bool load(const char* path);

    // 取 key 的 value（缺键返回空值，不抛异常）
    JsonValue operator[](const char* key) const;

private:
    friend class JsonValue;
    nlohmann::json root_ = nlohmann::json::value_t::discarded;
};

// 一个 value 的视图：所有取值方法对缺键/类型不符都返回默认值，不抛异常
class JsonValue
{
public:
    bool has() const;                                        // 键存在且非 null
    double float_(double def = 0.0) const;                   // 数值
    std::string string_(const std::string& def = "") const;  // 字符串
    std::vector<double> array() const;                       // 数值数组
    std::vector<double> group(int idx) const;                // 二维数值数组的第 idx 组
    JsonValue at(size_t i) const;                            // 数组的第 i 个元素
    size_t size() const;                                     // 数组元素个数

private:
    friend class JsonDoc;
    explicit JsonValue(const nlohmann::json* j) : j_(j) {}
    const nlohmann::json* j_ = nullptr;   // 空指针 = 缺键/越界
};

#endif // POINT3DALG_JSON_H
