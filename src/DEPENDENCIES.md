# 依赖库版本说明

## 项目概述
本项目包含两个动态库：
- **Image2DAlg** - 2D 图像处理算法库
- **Point3DAlg** - 3D 点云处理算法库

## 核心依赖库

### OpenCV
| 项目 | 版本 | 用途 | 目录 |
|------|------|------|------|
| OpenCV | 4.4.0 | 2D 图像处理、特征提取、滤波、边缘检测 | `thrid/opencv-4.4.0/` |

**模块依赖：**
- core, imgproc, imgcodecs, features2d, highgui

### PCL (Point Cloud Library)
| 项目 | 版本 | 用途 | 目录 |
|------|------|------|------|
| PCL | 1.9.1 | 3D 点云处理、配准、滤波、特征、分割 | `thrid/PCL-1.9.1/` |

**模块依赖：**
- common, io, filters, features, registration, segmentation, kdtree, search, visualization

**传递依赖：**
- Boost 1.70+ (system, filesystem, thread, date_time, iostreams, serialization)
- Eigen 3.3+
- FLANN 1.9+
- VTK 8.2+ (可选，仅 visualization 模块)
- Qhull 2015.2+

## 编译要求

| 项目 | 要求 |
|------|------|
| C++ 标准 | C++17 |
| CMake | 3.16+ |
| 编译器 | MSVC 2019+ / GCC 8+ / Clang 7+ |

## 输出产物

构建后生成 (在 `build/output/<Config>/`)：
```
Image2DAlg.dll          # Windows 动态库
libImage2DAlg.so        # Linux 动态库
Point3DAlg.dll
libPoint3DAlg.so
```

## 部署说明 (ComUI 插件格式)

交付目录结构 `comdll/algo/<名称>/`：
```
<名称>/
├── Image2DAlg.dll
├── Point3DAlg.dll
├── opencv_world440.dll          # OpenCV 运行时 (Windows)
├── pcl_common-1.9.dll           # PCL 运行时
├── pcl_io-1.9.dll
├── pcl_filters-1.9.dll
├── pcl_features-1.9.dll
├── pcl_registration-1.9.dll
├── pcl_segmentation-1.9.dll
├── pcl_kdtree-1.9.dll
├── pcl_search-1.9.dll
├── boost_system-vc142-mt-x64-1_70.dll
├── boost_filesystem-vc142-mt-x64-1_70.dll
├── boost_thread-vc142-mt-x64-1_70.dll
├── eigen3/                      # Eigen 头文件 (仅编译期)
├── your_2d.node.json            # 2D 算子声明
└── your_3d.node.json            # 3D 算子声明
```

## 版本兼容性

| 组件 | 最低版本 | 推荐版本 | 当前使用 |
|------|----------|----------|----------|
| OpenCV | 4.0.0 | 4.4.0 | 4.4.0 |
| PCL | 1.8.0 | 1.9.1 | 1.9.1 |
| Boost | 1.65 | 1.70+ | 1.70+ |
| Eigen | 3.3 | 3.3+ | 3.3+ |

## 许可证

- OpenCV: BSD 3-Clause
- PCL: BSD 3-Clause
- Boost: BSL-1.0
- Eigen: MPL2

本项目代码遵循相同许可证或项目指定许可证。