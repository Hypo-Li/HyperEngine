#pragma once
#include <cstdint>
#include <string_view>

namespace hyp
{
    class Hash
    {
    public:
        enum class Algorithm
        {
            FNV1a32,
            FNV1a64,
            xxHash32,
            xxHash64,
            Murmur3_32,
            Murmur3_64,
            CityHash32,
            CityHash64,
        };

        static uint64_t compute(const void* data, size_t size, Algorithm algo) noexcept;
        static uint64_t compute(std::string_view str, Algorithm algo) noexcept;

        template <typename T>
        static uint64_t compute(const T& data, Algorithm algo) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>, "Hash::compute only supports trivially copyable types");
            return compute(static_cast<const void*>(&data), sizeof(T), algo);
        }

        template <class T>
        static inline void combine(std::size_t& seed, const T& v) noexcept
        {
            std::hash<T> hasher;
            seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }

    private:
        static constexpr uint32_t fnv1a32(const void* data, size_t size) noexcept;
        static constexpr uint64_t fnv1a64(const void* data, size_t size) noexcept;

        static uint32_t xxHash32(const void* data, size_t size) noexcept;
        static uint64_t xxHash64(const void* data, size_t size) noexcept;

        static uint32_t murmur3_32(const void* data, size_t size) noexcept;
        static uint64_t murmur3_64(const void* data, size_t size) noexcept;

        static uint32_t cityHash32(const void* data, size_t size) noexcept;
        static uint64_t cityHash64(const void* data, size_t size) noexcept;
    };
}
