#include "Platform.h"

#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

namespace hyp
{
    namespace
    {
        std::filesystem::path resolveExecutableDir()
        {
#ifdef _WIN32
            // MAX_PATH(260) 在长路径支持开启的现代 Windows 上不是硬上限, 缓冲区不够时
            // GetModuleFileNameW 会返回缓冲区大小本身(而不是所需长度), 只能靠翻倍重试探测。
            std::vector<wchar_t> buffer(MAX_PATH);
            for (;;)
            {
                DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0)
                    return {};
                if (length < buffer.size())
                {
                    return std::filesystem::path(buffer.begin(), buffer.begin() + length).parent_path();
                }
                buffer.resize(buffer.size() * 2);
            }
#elif defined(__linux__)
            char buffer[PATH_MAX];
            ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
            if (length <= 0)
                return {};
            buffer[length] = '\0';
            return std::filesystem::path(buffer).parent_path();
#else
            return {};
#endif
        }
    }

    const std::filesystem::path& Platform::getExecutableDir()
    {
        static std::filesystem::path dir = resolveExecutableDir();
        return dir;
    }
}
