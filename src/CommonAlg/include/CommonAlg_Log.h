#ifndef COMMONALG_LOG_H
#define COMMONALG_LOG_H

// CommonAlg_Log —— 轻量日志（分级 + 时间戳 + 可选文件输出）。
// 设计：无第三方依赖；线程安全（内部互斥）；格式固定便于 grep/滚动。
//
// 用法：
//   CommonAlg_LogSetFile("D:/logs/app.log");     // 可选：同时写文件
//   CommonAlg_LogSetLevel(CommonAlg_LogLevel::Info);
//   LOG_INFO("加载完成，共 %d 个文件", n);        // printf 风格可变参
//   LOG_WARN("跳过早退：%s", reason.c_str());
//   LOG_ERROR("打开失败: %s", path);
//
// 说明：
//   - 低于当前级别的日志直接丢弃（零格式化开销，先判级再格式化的宏实现）
//   - 输出格式：[2026-11-05 14:03:22.123][INFO ][文件:行] 消息
//   - 未调用 SetFile 时只输出到 stderr
//   - 日志级别默认 Info

#include "AlgExport.h"

#include <string>

// 日志级别（数值越大越严重；SetLevel 之下的级别被丢弃）
enum class CommonAlg_LogLevel {
    Trace = 0,   // 最细粒度跟踪（循环内逐条调试）
    Debug = 1,   // 开发期诊断信息
    Info  = 2,   // 常规运行信息（默认级别）
    Warn  = 3,   // 可恢复异常 / 值得注意但不中断
    Error = 4,   // 操作失败（读取失败 / 打不开文件等）
    Off   = 5,   // 关闭全部日志
};

// ---------------- 配置 ----------------

/// 设置最低输出级别（低于该级别的日志被丢弃）。
/// [in] level  新级别（默认 Info；线程安全，可随时调用）
COMMONALG_EXPORT void CommonAlg_LogSetLevel(CommonAlg_LogLevel level);

/// 当前级别（即最近一次 SetLevel 的值；未设置时为 Info）。
COMMONALG_EXPORT CommonAlg_LogLevel CommonAlg_LogGetLevel();

/// 设置日志文件路径（追加写；目录须已存在，库不代建）。传空串关闭文件输出。
/// 调用即先关闭旧日志文件再打开新文件；返回 false = 新文件打不开
/// （此时旧文件输出也已关闭，之后仅 stderr；可随时调用，线程安全）。
/// Windows 下 path 按 ANSI（936/GBK）码页解释（非 UTF-8）。
COMMONALG_EXPORT bool CommonAlg_LogSetFile(const std::string& path);

/// 关闭日志文件（之后只输出 stderr）。
COMMONALG_EXPORT void CommonAlg_LogCloseFile();

// ---------------- 输出（一般用下面的宏，不直接调用） ----------------

/// 写一条日志（一般不直接调用，走下方宏）。线程安全：多线程并发调用
/// 单条完整不交叉；写文件后立即 flush（进程崩溃时已写日志不丢）。
/// [in] level  级别
/// [in] file   源文件名（宏自动传 __FILE__；输出只显示文件名部分）
/// [in] line   行号（宏自动传 __LINE__）
/// [in] fmt    printf 风格格式串（变参个数/类型须与之匹配，否则未定义行为）
/// 消息体超约 2047 字节截断；每条同时输出到 stderr 与日志文件（若已设置）。
COMMONALG_EXPORT void CommonAlg_LogWrite(CommonAlg_LogLevel level, const char* file, int line,
                        const char* fmt, ...);

// ---------------- 便捷宏（先判级，避免无谓格式化） ----------------

#define COMMONALG_LOG_AT(lvl, ...)                                            \
    do {                                                                       \
        if ((int)(lvl) >= (int)CommonAlg_LogGetLevel())                        \
            CommonAlg_LogWrite((lvl), __FILE__, __LINE__, __VA_ARGS__);        \
    } while (0)

#define LOG_TRACE(...) COMMONALG_LOG_AT(CommonAlg_LogLevel::Trace, __VA_ARGS__)
#define LOG_DEBUG(...) COMMONALG_LOG_AT(CommonAlg_LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...)  COMMONALG_LOG_AT(CommonAlg_LogLevel::Info,  __VA_ARGS__)
#define LOG_WARN(...)  COMMONALG_LOG_AT(CommonAlg_LogLevel::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) COMMONALG_LOG_AT(CommonAlg_LogLevel::Error, __VA_ARGS__)

// 带模块前缀的别名（推荐使用：LOG_INFO 这类短名容易与 glog/spdlog/Qt 冲突，
// 若项目已引入同类日志库，请改用下面这组）
#define CALG_LOG_TRACE(...) LOG_TRACE(__VA_ARGS__)
#define CALG_LOG_DEBUG(...) LOG_DEBUG(__VA_ARGS__)
#define CALG_LOG_INFO(...)  LOG_INFO(__VA_ARGS__)
#define CALG_LOG_WARN(...)  LOG_WARN(__VA_ARGS__)
#define CALG_LOG_ERROR(...) LOG_ERROR(__VA_ARGS__)

#endif // COMMONALG_LOG_H
