#ifndef SHAPELY_CLOUD_IO_H
#define SHAPELY_CLOUD_IO_H

// shapely::CloudIO —— 点云文件读写（独立成类以隔离重型 IO 依赖）。
// 支持 PCD / PLY / TXT；模板显式实例化白名单同 CloudProcessor。

#include "CloudProcessor.h"
#include "ShapelyAlg_export.h"      // 自动生成的导出宏

#include <string>
#include <vector>

namespace shapely {

template <typename PointT>
class SHAPELYALG_EXPORT CloudIO
{
public:
    CloudIO() = delete;   // 纯静态工具类

    using CloudPtr = typename pcl::PointCloud<PointT>::Ptr;

    // ---------------- PCD ----------------
    static CloudPtr ioLoadPCD(const std::string& filename);
    static bool ioSavePCD(const std::string& filename, const CloudPtr& cloud);

    // ---------------- PLY ----------------
    static CloudPtr ioLoadPLY(const std::string& filename);
    static bool ioSavePLY(const std::string& filename, const CloudPtr& cloud, bool is_binary);

    // ---------------- TXT ----------------
    // delimiter：分隔符（如 " " / "," / "\t"）；columns：XYZ 列号（0 起，默认 0,1,2）
    static CloudPtr ioLoadTXT(const std::string& filename, const std::string& delimiter,
                              int col_x = 0, int col_y = 1, int col_z = 2);
    static bool ioSaveTXT(const std::string& filename, const CloudPtr& cloud,
                          const std::string& delimiter);
};

} // namespace shapely

#endif // SHAPELY_CLOUD_IO_H
