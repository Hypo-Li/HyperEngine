#include "LogSystem.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace hyp
{
    namespace
    {
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

        // Log 窗口里一行显示不下完整的绝对路径, 只留文件名+行号——跟 spdlog 那份写进文件的
        // 完整 source_loc 是两份独立的格式化结果, 互不影响。
        std::string toShortLocation(const std::source_location& location)
        {
            return std::filesystem::path(location.file_name()).filename().string() + ":" + std::to_string(location.line());
        }
    }

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

    void LogSystem::init(const std::filesystem::path& logDir)
    {
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);

        auto timestamp = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        std::string filename = std::format("HyperEngine_{:%Y%m%d_%H%M%S}.log", timestamp);

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((logDir / filename).string(), true);
        // 空字符串: spdlog 默认格式化器只在 logger_name 非空时才拼进 "[name]" 这一段
        // (pattern_formatter-inl.h 里 full_formatter::format() 的判断)——没有代码按名字
        // 查过这个 logger(没用到 spdlog::get()), 名字本身只在这里体现, 留空直接省掉这一段。
        auto logger = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list{ consoleSink, fileSink });
        spdlog::set_default_logger(logger);

        // spdlog logger 自己也有一套独立的 level 阈值, 默认 info——实际过滤完全交给上面
        // mLogLevel/setLogLevel() 那一道闸门, 这里放到最宽松让 spdlog 自己的阈值形同虚设,
        // 避免两处 level 状态不同步
        logger->set_level(spdlog::level::trace);

        // 崩溃时不希望日志停留在 libc 的文件缓冲区里没写出去——Warn/Error 每次都立即 flush
        // (最有可能是崩溃前最后一条有价值的信息), Debug/Info 量大、没必要每条都 flush,
        // 靠下面的定时 flush 兜底(几秒钟的丢失窗口可以接受, 跟"程序正常退出时才统一写"那种
        // 整份丢失的风险完全是两回事)。
        logger->flush_on(spdlog::level::warn);
        spdlog::flush_every(std::chrono::seconds(3));
    }

    void LogSystem::shutdown()
    {
        spdlog::shutdown();
    }

    void LogSystem::setLogLevel(LogLevel level)
    {
        mLogLevel.store(level, std::memory_order_relaxed);
    }

    void LogSystem::logInternal(std::source_location location, LogLevel level, std::string_view fmt, std::format_args args)
    {
        std::string message = std::vformat(fmt, args);
        spdlog::log(toSpdlogLocation(location), toSpdlogLevel(level), message);

        LogEntry entry{ level, message, toShortLocation(location), std::chrono::system_clock::now() };

        std::lock_guard<std::mutex> lock(mHistoryMutex);
        mHistory.push_back(std::move(entry));
        mLevelCounts[static_cast<size_t>(level)]++;
        if (mHistory.size() > kMaxHistorySize)
        {
            mLevelCounts[static_cast<size_t>(mHistory.front().level)]--;
            mHistory.pop_front();
        }
        mVersion++;
    }

    void LogSystem::withHistory(const std::function<void(const std::deque<LogEntry>&, const std::array<uint32_t, kLogLevelCount>&, uint64_t)>& callback) const
    {
        std::lock_guard<std::mutex> lock(mHistoryMutex);
        callback(mHistory, mLevelCounts, mVersion);
    }

    void LogSystem::clearHistory()
    {
        std::lock_guard<std::mutex> lock(mHistoryMutex);
        mHistory.clear();
        mLevelCounts.fill(0);
        mVersion++;
    }
}
