#include "LogSystem.h"

#include <spdlog/spdlog.h>

namespace hyp
{
    LogSystem& LogSystem::get()
    {
        static LogSystem instance;
        return instance;
    }

    LogSystem::LogSystem()
    {
    }

    LogSystem::~LogSystem()
    {
    }

    void LogSystem::init()
    {
    }

    void LogSystem::shutdown()
    {
    }

    void LogSystem::setLogLevel(LogLevel level)
    {
        mLogLevel.store(level, std::memory_order_relaxed);
    }

    spdlog::level::level_enum toSpdlogLevel(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug:
            return spdlog::level::debug;
        case LogLevel::Info:
            return spdlog::level::info;
        case LogLevel::Warn:
            return spdlog::level::warn;
        case LogLevel::Error:
            return spdlog::level::err;
        default:
            return spdlog::level::off;
        }
    }

    spdlog::source_loc toSpdlogLocation(std::source_location location)
    {
        spdlog::source_loc loc;
        loc.filename = location.file_name();
        loc.line = location.line();
        loc.funcname = location.function_name();
        return loc;
    }

    void LogSystem::logInternal(std::source_location location, LogLevel level, std::string_view fmt, std::format_args args)
    {
        std::string message = std::vformat(fmt, args);
        spdlog::log(toSpdlogLocation(location), toSpdlogLevel(level), message);
    }

}
