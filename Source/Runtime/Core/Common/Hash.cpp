#include "Hash.h"

namespace hyp
{
    uint64_t Hash::compute(const void* data, size_t size, Algorithm algo) noexcept
    {
        switch (algo)
        {
        case Algorithm::FNV1a32: return fnv1a32(data, size);
        case Algorithm::FNV1a64: return fnv1a64(data, size);
        case Algorithm::Murmur3_32: return murmur3_32(data, size);
        case Algorithm::Murmur3_64: return murmur3_64(data, size);
        case Algorithm::xxHash32: return xxHash32(data, size);
        case Algorithm::xxHash64: return xxHash64(data, size);
        case Algorithm::CityHash64: return cityHash64(data, size);
        default: return 0;
        }
    }

    uint64_t Hash::compute(std::string_view str, Algorithm algo) noexcept
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
        return 0;
    }

    uint64_t Hash::xxHash64(const void* data, size_t size) noexcept
    {
        return 0;
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
