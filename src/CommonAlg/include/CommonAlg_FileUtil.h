#ifndef COMMONALG_FILEUTIL_H
#define COMMONALG_FILEUTIL_H

// CommonAlg_FileUtil —— 文件与目录基础工具（跨平台，零第三方依赖）。
// 用途：扫描数据文件夹枚举、路径拼接/分解、目录创建与存在性判断。
// 全部函数失败时返回空/false，不抛异常。
// 编码注意：Windows 下 char 路径按 ANSI（936/GBK）码页解释（Win32 A 系列
//   API / fopen），非 UTF-8；中文路径请传 GBK 字节。
// 线程注意：全部函数无共享状态，可多线程并发调用。

#include "AlgExport.h"

#include <string>
#include <vector>

// ---------------- 文件枚举 ----------------

/// 列出目录下指定后缀的文件（按文件名排序 = 自然扫描顺序）。
/// 只枚举当前层：不递归子目录、不含目录项。
/// [in] folder  目录路径（正斜杠或反斜杠均可，内部统一为 '/'）
/// [in] ext     后缀含点，如 ".dat" / ".json"；空串表示全部文件；区分大小写
/// 返回：完整路径列表（folder + "/" + 文件名，'/' 分隔）；目录不存在返回空。
COMMONALG_EXPORT std::vector<std::string> CommonAlg_ListFiles(const std::string& folder,
                                             const std::string& ext);

// ---------------- 文件读写 ----------------

/// 读整个文件为字符串（二进制读，不做编码转换）。
/// [in]  path  文件路径
/// [out] out   文件内容（成功时覆盖）
/// 返回：false = 文件不存在或读取失败。
COMMONALG_EXPORT bool CommonAlg_ReadFile(const std::string& path, std::string& out);

/// 写字符串到文件（覆盖，二进制写；父目录须已存在，不自动创建）。
/// [in] path  目标路径
/// [in] data  待写内容（空串 = 清空/新建文件）
/// 返回：false = 打开或写入失败。
COMMONALG_EXPORT bool CommonAlg_WriteFile(const std::string& path, const std::string& data);

/// 追加文本到文件（二进制写；文件不存在则创建，父目录须已存在）。
/// [in] path  目标路径
/// [in] data  追加内容
/// 返回：false = 打开或写入失败。
COMMONALG_EXPORT bool CommonAlg_AppendFile(const std::string& path, const std::string& data);

// ---------------- 路径操作 ----------------

/// 文件是否存在（普通文件，不含目录）。
/// [in] path  文件路径
COMMONALG_EXPORT bool CommonAlg_FileExists(const std::string& path);

/// 目录是否存在。
/// [in] path  目录路径
COMMONALG_EXPORT bool CommonAlg_DirExists(const std::string& path);

/// 创建目录（含多级父目录）。已存在视为成功。
/// [in] path  要创建的目录路径；空串返回 false
/// 返回：false = 创建失败（无权限/路径被文件占用等）。
COMMONALG_EXPORT bool CommonAlg_MakeDir(const std::string& path);

/// 取文件名部分（含扩展名）："/a/b/c.dat" → "c.dat"
/// [in] path  任意形式的路径；返回空 = path 为空或以分隔符结尾
COMMONALG_EXPORT std::string CommonAlg_FileName(const std::string& path);

/// 取文件名主干（不含最后一个扩展名）："/a/b/c.dat" → "c"
/// [in] path  任意形式的路径；点开头的隐藏文件主干不变（"/a/.gitignore" → ".gitignore"）
COMMONALG_EXPORT std::string CommonAlg_FileStem(const std::string& path);

/// 取目录部分（不含末尾分隔符）："/a/b/c.dat" → "/a/b"
/// [in] path  任意形式的路径；无目录部分返回空
COMMONALG_EXPORT std::string CommonAlg_DirName(const std::string& path);

/// 取扩展名（含点）："/a/b/c.dat" → ".dat"
/// [in] path  任意形式的路径；无扩展名返回空；前导点（隐藏文件）不算
///            扩展名："/a/.gitignore" → ""（主干语义见 FileStem）
COMMONALG_EXPORT std::string CommonAlg_Extension(const std::string& path);

/// 拼路径（自动补 "/" 分隔符，不处理 ".."，不规范化 dir 中的既有分隔符）。
/// [in] dir   目录（末字符已是 '/' 或 '\\' 则不重复补、原样保留；空串直接返回 name）
/// [in] name  文件名或子路径
COMMONALG_EXPORT std::string CommonAlg_JoinPath(const std::string& dir, const std::string& name);

/// 同名不同扩展名的路径："/a/b/c.dat" + ".xml" → "/a/b/c.xml"
/// [in] path  原路径（分隔符统一为 "/"）
/// [in] ext   新扩展名（含点；传空串 = 去掉扩展名）
/// 注意：点落在目录部分（如 "/a.b/c"）视为无扩展名，直接追加 ext；
///       前导点无保护：ReplaceExtension(".gitignore", ".xml") → ".xml"。
COMMONALG_EXPORT std::string CommonAlg_ReplaceExtension(const std::string& path, const std::string& ext);

#endif // COMMONALG_FILEUTIL_H
