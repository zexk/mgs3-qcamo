// Copied from bbtracker (../bbtracker/src/common/log.h), MIT licensed.
#pragma once

namespace qcamo {

enum class LogLevel : int {
    Debug = 0,
    Info,
    Warn,
    Error,
};

bool log_init(const char* path);
void log_write(LogLevel lvl, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

} // namespace qcamo

#define LOG_DEBUG(...) ::qcamo::log_write(::qcamo::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) ::qcamo::log_write(::qcamo::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) ::qcamo::log_write(::qcamo::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) ::qcamo::log_write(::qcamo::LogLevel::Error, __VA_ARGS__)
