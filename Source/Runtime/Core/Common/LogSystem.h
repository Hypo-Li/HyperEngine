#pragma once
#include <string_view>
#include <format>
#include <source_location>
#include <atomic>

namespace hyp
{
    enum class LogLevel : uint8_t
    {
        Debug,
        Info,
        Warn,
        Error
    };

    class LogSystem
    {
    public:
        static LogSystem& get();

        LogSystem();

        ~LogSystem();

        LogSystem(const LogSystem&) = delete;
        LogSystem& operator=(const LogSystem&) = delete;

        void init();

        void shutdown();

        void setLogLevel(LogLevel level);

        template <typename... Args>
        void log(const std::source_location& location, LogLevel logLevel, std::format_string<Args...> fmt, Args&&... args)
        {
            uint8_t currentLevel = static_cast<uint8_t>(mLogLevel.load(std::memory_order_relaxed));
            if (currentLevel <= static_cast<uint8_t>(logLevel))
                logInternal(location, logLevel, fmt.get(), std::make_format_args(args...));
        }

        template <typename... Args>
        void debug(const std::source_location& location, std::format_string<Args...> fmt, Args&&... args)
        {
            log(location, LogLevel::Debug, fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        void info(const std::source_location& location, std::format_string<Args...> fmt, Args&&... args)
        {
            log(location, LogLevel::Info, fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        void warn(const std::source_location& location, std::format_string<Args...> fmt, Args&&... args)
        {
            log(location, LogLevel::Warn, fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        void error(const std::source_location& location, std::format_string<Args...> fmt, Args&&... args)
        {
            log(location, LogLevel::Error, fmt, std::forward<Args>(args)...);
        }

    private:
        void logInternal(std::source_location location, LogLevel level, std::string_view fmt, std::format_args args);

        std::atomic<LogLevel> mLogLevel = LogLevel::Info;
    };

#define LOG_DEBUG(...) LogSystem::get().debug(std::source_location::current(), __VA_ARGS__)
#define LOG_INFO(...) LogSystem::get().info(std::source_location::current(), __VA_ARGS__)
#define LOG_WARN(...) LogSystem::get().warn(std::source_location::current(), __VA_ARGS__)
#define LOG_ERROR(...) LogSystem::get().error(std::source_location::current(), __VA_ARGS__)
}
