#pragma once
#include <string_view>
#include <format>
#include <source_location>
#include <atomic>
#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

namespace hyp
{
    enum class LogLevel : uint8_t
    {
        Debug,
        Info,
        Warn,
        Error
    };

    constexpr size_t kLogLevelCount = 4;

    // 一条已经格式化好的日志记录——独立于 spdlog(控制台/文件 sink 走 spdlog 自己的机制),
    // 这份历史只服务于想展示"最近 N 条日志"的场景(比如 Editor 的 Log 窗口)。message 是
    // 格式化后的最终文本(不是 fmt 字符串, 不能再当格式串用), location 是"文件名:行号"这种
    // 精简形式(不是 source_location::file_name() 的完整绝对路径, 那样在窗口里太占地方)。
    struct LogEntry
    {
        LogLevel level;
        std::string message;
        std::string location;
        std::chrono::system_clock::time_point time;
    };

    class LogSystem
    {
    public:
        static LogSystem& get();

        LogSystem();

        ~LogSystem();

        LogSystem(const LogSystem&) = delete;
        LogSystem& operator=(const LogSystem&) = delete;

        // 只应在进程启动时调一次(Engine 构造函数最开头, 保证尽量早就有日志能力), 装好
        // spdlog 的控制台 + 文件双 sink。logDir 建议传绝对路径(比如
        // Platform::getExecutableDir() / "Logs")——不依赖进程启动时的当前工作目录是否等于
        // 可执行文件所在目录; 目录不存在会自动创建。
        void init(const std::filesystem::path& logDir = "Logs");

        // 只应在进程退出前调一次(Engine 析构函数最末尾, 保证尽量晚才停止, 后面清理逻辑里
        // 如果还有日志也能被记录下来)。
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

        // 给 Editor 的 Log 窗口用: 在锁保护下把历史记录 + 每个 level 当前的条数 + 一个单调
        // 递增的 version 交给 callback——callback 内部想怎么遍历(配合 ImGuiListClipper 之类)
        // 都行, 但不能把 entries 的引用带到 callback 返回之后继续用, 回调一返回锁就释放了,
        // 后续写日志(环形淘汰最旧一条)随时可能让它失效。写日志的线程如果恰好撞上这次读取,
        // 会被这把锁短暂挡一下(最多就是遍历/画一份日志列表的时间, 亚毫秒级), 可以接受。
        //
        // version 在每次 push/clear 时递增, 不会因为环形缓冲区丢弃最旧条目而回退或停滞
        // (缓冲区打满之后 entries.size() 会一直停在上限, 单看条数没法判断"是否有新日志",
        // 调用方(比如 Log 窗口的按 level/文本过滤结果缓存)靠比较这个 version 判断要不要
        // 重新算一遍, 不用每帧都重新扫一遍 entries)。
        void withHistory(const std::function<void(const std::deque<LogEntry>& entries, const std::array<uint32_t, kLogLevelCount>& levelCounts, uint64_t version)>& callback) const;

        void clearHistory();

    private:
        void logInternal(std::source_location location, LogLevel level, std::string_view fmt, std::format_args args);

        std::atomic<LogLevel> mLogLevel = LogLevel::Info;

        // 环形缓冲区上限——日志条目本身很轻(就是一个已经格式化好的字符串), 不像
        // UndoStack::kMaxDepth 那样要考虑闭包捕获的开销, 可以留得宽松很多; 只是为了不让
        // 长时间挂着不关的 Editor 进程无限攒日志占内存, 不是为了性能。
        static constexpr size_t kMaxHistorySize = 5000;

        mutable std::mutex mHistoryMutex;
        std::deque<LogEntry> mHistory;
        std::array<uint32_t, kLogLevelCount> mLevelCounts{};
        uint64_t mVersion = 0;
    };

#define LOG_DEBUG(...) LogSystem::get().debug(std::source_location::current(), __VA_ARGS__)
#define LOG_INFO(...) LogSystem::get().info(std::source_location::current(), __VA_ARGS__)
#define LOG_WARN(...) LogSystem::get().warn(std::source_location::current(), __VA_ARGS__)
#define LOG_ERROR(...) LogSystem::get().error(std::source_location::current(), __VA_ARGS__)
}
