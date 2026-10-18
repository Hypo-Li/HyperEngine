#include "Hash.h"

#include <xxhash.h>

namespace hyp
{
    template <> Type* Reflection::createType<uint128_t>()
    {
        auto s = new TStructure<uint128_t>("uint128_t");
        s->addProperty("low", &uint128_t::low);
        s->addProperty("high", &uint128_t::high);
        return s;
    }

    uint32_t Hash::compute(const void* data, size_t size, Algorithm32 algo) noexcept
    {
        switch (algo)
        {
        case Algorithm32::FNV1a: return fnv1a32(data, size);
        case Algorithm32::Murmur3: return murmur3_32(data, size);
        case Algorithm32::xxHash: return xxHash32(data, size);
        case Algorithm32::CityHash: return cityHash32(data, size);
        default: return 0;
        }
    }

    uint32_t Hash::compute(std::string_view str, Algorithm32 algo) noexcept
    {
        return compute(str.data(), str.size(), algo);
    }

    uint64_t Hash::compute(const void* data, size_t size, Algorithm64 algo) noexcept
    {
        switch (algo)
        {
        case Algorithm64::FNV1a: return fnv1a64(data, size);
        case Algorithm64::Murmur3: return murmur3_64(data, size);
        case Algorithm64::xxHash: return xxHash64(data, size);
        case Algorithm64::CityHash: return cityHash64(data, size);
        default: return 0;
        }
    }

    uint64_t Hash::compute(std::string_view str, Algorithm64 algo) noexcept
    {
        return compute(str.data(), str.size(), algo);
    }

    uint128_t Hash::compute(const void* data, size_t size, Algorithm128 algo) noexcept
    {
        switch (algo)
        {
        case Algorithm128::xxHash: return xxHash128(data, size);
        default: return uint128_t{};
        }
    }

    uint128_t Hash::compute(std::string_view str, Algorithm128 algo) noexcept
    {
        return compute(str.data(), str.size(), algo);
    }

    constexpr uint32_t Hash::fnv1a32(const void* data, size_t size) noexcept
    {
        constexpr uint32_t FNV1A32_OFFSET_BASIS = 2166136261u;
        constexpr uint32_t FNV1A32_PRIME = 16777619u;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint32_t hash = FNV1A32_OFFSET_BASIS;
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= FNV1A32_PRIME;
        }
        return hash;
    }

    constexpr uint64_t Hash::fnv1a64(const void* data, size_t size) noexcept
    {
        constexpr uint64_t FNV1A64_OFFSET_BASIS = 14695981039346656037ull;
        constexpr uint64_t FNV1A64_PRIME = 1099511628211ull;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint64_t hash = FNV1A64_OFFSET_BASIS;
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= FNV1A64_PRIME;
        }
        return hash;
    }

    uint32_t Hash::xxHash32(const void* data, size_t size) noexcept
    {
        return XXH32(data, size, 0);
    }

    uint64_t Hash::xxHash64(const void* data, size_t size) noexcept
    {
        return XXH64(data, size, 0);
    }

    uint128_t Hash::xxHash128(const void* data, size_t size) noexcept
    {
        XXH128_hash_t h = XXH3_128bits(data, size);
        return uint128_t{ h.low64, h.high64 };
    }

    uint32_t Hash::murmur3_32(const void* data, size_t size) noexcept
    {
        return 0;
    }

    uint64_t Hash::murmur3_64(const void* data, size_t size) noexcept
    {
        return 0;
    }

    uint32_t Hash::cityHash32(const void* data, size_t size) noexcept
    {
        return 0;
    }

    uint64_t Hash::cityHash64(const void* data, size_t size) noexcept
    {
        return 0;
    }

}
