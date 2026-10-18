#include "DerivedDataCache.h"

#include <cstdio>
#include <fstream>

namespace hyp
{
    DerivedDataCache::DerivedDataCache(std::filesystem::path cacheRoot, std::string category) :
        mCacheRoot(std::move(cacheRoot)),
        mCategory(std::move(category))
    {
    }

    std::filesystem::path DerivedDataCache::getCacheFilePath(uint128_t key) const
    {
        char hex[33];
        std::snprintf(hex, sizeof(hex), "%016llx%016llx",
            static_cast<unsigned long long>(key.high), static_cast<unsigned long long>(key.low));
        std::filesystem::path dir = mCacheRoot / mCategory / std::string(hex, hex + 2);
        return dir / (std::string(hex) + ".bin");
    }

    std::optional<std::vector<uint8_t>> DerivedDataCache::tryLoad(uint128_t key) const
    {
        std::ifstream file(getCacheFilePath(key), std::ios::binary);
        if (!file)
            return std::nullopt;

        file.seekg(0, std::ios::end);
        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(size);
        file.read(reinterpret_cast<char*>(data.data()), size);
        return data;
    }

    void DerivedDataCache::save(uint128_t key, std::span<const uint8_t> data) const
    {
        std::filesystem::path path = getCacheFilePath(key);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
            return;

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return;

        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
}
