#pragma once
#include <filesystem>

namespace hyp
{
    // 平台相关的静态工具方法集合, 不持有状态、不需要实例化。以后其它"和具体操作系统相关但
    // 和窗口无关"的能力(比如查系统级缓存/配置目录)也应该往这里加, 不要每加一个能力就新开
    // 一个类。
    class Platform
    {
    public:
        Platform() = delete;

        // 当前运行的可执行文件所在目录的绝对路径(不含文件名)。用 GetModuleFileNameW/
        // /proc/self/exe 定位, 不依赖 argv[0]——argv[0] 由调用方决定, 可能是相对路径、
        // 也可能被设成任意值, 不能保证准确反映可执行文件的真实位置。
        static const std::filesystem::path& getExecutableDir();
    };
}
