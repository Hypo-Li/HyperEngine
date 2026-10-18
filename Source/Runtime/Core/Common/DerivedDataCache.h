#pragma once
#include "Hash.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hyp
{
    // 按内容哈希寻址的本地缓存文件读写——文件路径 = cacheRoot / category / <key 的前 2 位 hex> /
    // <key 的 32 位 hex>.bin。只管"怎么找到文件、怎么读写原始字节",不关心 payload 内部怎么组帧
    // (比如 ShaderCache 存的是两段变长 blob),组帧格式由调用方自己在这层之上处理。
    //
    // key 用 128-bit——不引入冲突检测机制, 单纯靠 128-bit 空间把碰撞概率压到物理上可以无视的
    // 量级(调用方必须保证自己拼 key 时全程走 128-bit 的 Hash::compute/combine, 中途混进任何
    // 只有 64-bit 真实熵的值都会把这份保证拉低回 64-bit)。
    //
    // 缓存文件本身可随时删除、可重建, 不做任何失效检测——命中与否只影响要不要重新调用具体的
    // 生成逻辑, 不影响正确性。
    class DerivedDataCache
    {
    public:
        DerivedDataCache(std::filesystem::path cacheRoot, std::string category);

        std::optional<std::vector<uint8_t>> tryLoad(uint128_t key) const;

        void save(uint128_t key, std::span<const uint8_t> data) const;

    private:
        std::filesystem::path getCacheFilePath(uint128_t key) const;

        std::filesystem::path mCacheRoot;
        std::string mCategory;
    };
}
