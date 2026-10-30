// CloudIO.cpp —— 点云文件读写（PCD/PLY/TXT）。
// 重型 IO 头只在 .cpp include；显式实例化白名单同 CloudProcessor。
// Boost 1.68 + MSVC 2022 兼容垫片见 BoostCompat.h（include pcl/io 前必须先引入）。

#include "BoostCompat.h"

#include "CloudIO.h"

#include <pcl/io/pcd_io.h>
#include <pcl/io/ply_io.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace shapely {

// ---------------- PCD ----------------

template <typename PointT>
typename CloudIO<PointT>::CloudPtr CloudIO<PointT>::ioLoadPCD(const std::string& filename)
{
    CloudPtr out(new pcl::PointCloud<PointT>);
    if (pcl::io::loadPCDFile(filename, *out) < 0)
        return nullptr;
    return out;
}

template <typename PointT>
bool CloudIO<PointT>::ioSavePCD(const std::string& filename, const CloudPtr& cloud)
{
    if (!cloud)
        return false;
    return pcl::io::savePCDFileBinary(filename, *cloud) == 0;
}

// ---------------- PLY ----------------

template <typename PointT>
typename CloudIO<PointT>::CloudPtr CloudIO<PointT>::ioLoadPLY(const std::string& filename)
{
    CloudPtr out(new pcl::PointCloud<PointT>);
    if (pcl::io::loadPLYFile(filename, *out) < 0)
        return nullptr;
    return out;
}

template <typename PointT>
bool CloudIO<PointT>::ioSavePLY(const std::string& filename, const CloudPtr& cloud, bool is_binary)
{
    if (!cloud)
        return false;
    if (is_binary)
        return pcl::io::savePLYFileBinary(filename, *cloud) == 0;
    return pcl::io::savePLYFileASCII(filename, *cloud) == 0;
}

// ---------------- TXT ----------------

template <typename PointT>
typename CloudIO<PointT>::CloudPtr CloudIO<PointT>::ioLoadTXT(const std::string& filename,
                                                             const std::string& delimiter,
                                                             int col_x, int col_y, int col_z)
{
    std::ifstream f(filename);
    if (!f)
        return nullptr;
    CloudPtr out(new pcl::PointCloud<PointT>);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        // 分隔符切分（空格分隔时连续空格视为一个）
        std::vector<std::string> tok;
        if (delimiter == " " || delimiter.empty()) {
            std::istringstream ss(line);
            std::string t;
            while (ss >> t)
                tok.push_back(t);
        } else {
            size_t a = 0;
            while (true) {
                const size_t b = line.find(delimiter, a);
                tok.push_back(line.substr(a, (b == std::string::npos) ? std::string::npos : b - a));
                if (b == std::string::npos)
                    break;
                a = b + delimiter.size();
            }
        }
        const int need = std::max(col_x, std::max(col_y, col_z)) + 1;
        if ((int)tok.size() < need)
            continue;
        PointT p;
        p.x = (float)atof(tok[(size_t)col_x].c_str());
        p.y = (float)atof(tok[(size_t)col_y].c_str());
        p.z = (float)atof(tok[(size_t)col_z].c_str());
        out->push_back(p);
    }
    return out;
}

template <typename PointT>
bool CloudIO<PointT>::ioSaveTXT(const std::string& filename, const CloudPtr& cloud,
                                const std::string& delimiter)
{
    if (!cloud)
        return false;
    std::ofstream f(filename);
    if (!f)
        return false;
    const std::string& d = delimiter.empty() ? " " : delimiter;
    for (const PointT& p : cloud->points)
        f << p.x << d << p.y << d << p.z << "\n";
    return true;
}

// =====================================================================
// 显式实例化白名单
// =====================================================================

template class CloudIO<pcl::PointXYZ>;
template class CloudIO<pcl::PointXYZI>;
template class CloudIO<pcl::PointXYZRGB>;

} // namespace shapely
