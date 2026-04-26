#pragma once

#include <cstdarg>
#include <cstdio>

//making a custom logger because iostream calls mutexes internally, which is not acceptable in this case
namespace logger
{

enum class LogLevel
{
    DEBUG,
    INFO,
    WARN,
    ERROR
};

inline const char* get_level_name(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:  
            return "INFO";
        case LogLevel::WARN:  
            return "WARN";
        case LogLevel::ERROR: 
            return "ERROR";
    }
    __builtin_unreachable();
}

inline void logv(LogLevel level, const char* fmt, va_list args) noexcept
{
    std::fprintf(stderr, "[%s] ", get_level_name(level));
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
}

//take in variadic arguments at pos 3
[[gnu::format(printf, 2, 3)]]
inline void log(LogLevel level, const char* fmt, ...) noexcept
{
    va_list args;
    va_start(args, fmt);
    logv(level, fmt, args);
    va_end(args);
}

#ifndef NDEBUG
[[gnu::format(printf, 1, 2)]]
inline void LOG_DEBUG(const char* fmt, ...) noexcept
{
    va_list args;
    va_start(args, fmt);
    logv(LogLevel::DEBUG, fmt, args);
    va_end(args);
}
#else
//no op for release builds
[[gnu::format(printf, 1, 2)]]
inline void LOG_DEBUG(const char* /*fmt*/, ...) noexcept 
{}
#endif

[[gnu::format(printf, 1, 2)]]
inline void LOG_INFO(const char* fmt, ...) noexcept
{
    va_list args;
    va_start(args, fmt);
    logv(LogLevel::INFO, fmt, args);
    va_end(args);
}

[[gnu::format(printf, 1, 2)]]
inline void LOG_WARN(const char* fmt, ...) noexcept
{
    va_list args;
    va_start(args, fmt);
    logv(LogLevel::WARN, fmt, args);
    va_end(args);
}

[[gnu::format(printf, 1, 2)]]
inline void LOG_ERROR(const char* fmt, ...) noexcept
{
    va_list args;
    va_start(args, fmt);
    logv(LogLevel::ERROR, fmt, args);
    va_end(args);
}

}
