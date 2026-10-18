#pragma once
#include <cstdint>
#include <string_view>
#include <Core/Reflection/Reflection.h>

namespace hyp
{
    struct uint128_t
    {
        uint64_t low = 0;
        uint64_t high = 0;

        friend bool operator==(const uint128_t& a, const uint128_t& b) noexcept
        {
            return a.low == b.low && a.high == b.high;
        }
    };

    template <> Type* Reflection::createType<uint128_t>();

    class Hash
    {
    public:
        enum class Algorithm32
        {
            FNV1a,
            xxHash,
            Murmur3,
            CityHash,
        };

        enum class Algorithm64
        {
            FNV1a,
            xxHash,
            Murmur3,
            CityHash,
        };

        enum class Algorithm128
        {
            xxHash,
        };

        static uint32_t compute(const void* data, size_t size, Algorithm32 algo) noexcept;
        static uint32_t compute(std::string_view str, Algorithm32 algo) noexcept;

        static uint64_t compute(const void* data, size_t size, Algorithm64 algo) noexcept;
        static uint64_t compute(std::string_view str, Algorithm64 algo) noexcept;

        static uint128_t compute(const void* data, size_t size, Algorithm128 algo) noexcept;
        static uint128_t compute(std::string_view str, Algorithm128 algo) noexcept;

        template <typename T>
        static uint32_t compute(const T& data, Algorithm32 algo) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>, "Hash::compute only supports trivially copyable types");
            return compute(static_cast<const void*>(&data), sizeof(T), algo);
        }

        template <typename T>
        static uint64_t compute(const T& data, Algorithm64 algo) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>, "Hash::compute only supports trivially copyable types");
            return compute(static_cast<const void*>(&data), sizeof(T), algo);
        }

        template <typename T>
        static uint128_t compute(const T& data, Algorithm128 algo) noexcept
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

        // low/high 用两个不同的黄金分割常量各自扩散, 避免两路结果只是同一个操作的重复
        template <class T>
        static inline void combine(uint128_t& seed, const T& v) noexcept
        {
            std::hash<T> hasher;
            const uint64_t h = static_cast<uint64_t>(hasher(v));
            seed.low ^= h + 0x9e3779b97f4a7c15ULL + (seed.low << 6) + (seed.low >> 2);
            seed.high ^= h + 0xff51afd7ed558ccdULL + (seed.high << 6) + (seed.high >> 2);
        }

        // v 本身已经是 128-bit(比如另一段子哈希), low/high 各自独立混入对应半区, 不经过
        // std::hash 压扁成一个 64-bit 值再扩散——那样会把 v 一半的熵在混入前就丢掉
        static inline void combine(uint128_t& seed, const uint128_t& v) noexcept
        {
            seed.low ^= v.low + 0x9e3779b97f4a7c15ULL + (seed.low << 6) + (seed.low >> 2);
            seed.high ^= v.high + 0xff51afd7ed558ccdULL + (seed.high << 6) + (seed.high >> 2);
        }

    private:
        static constexpr uint32_t fnv1a32(const void* data, size_t size) noexcept;
        static constexpr uint64_t fnv1a64(const void* data, size_t size) noexcept;

        static uint32_t xxHash32(const void* data, size_t size) noexcept;
        static uint64_t xxHash64(const void* data, size_t size) noexcept;
        static uint128_t xxHash128(const void* data, size_t size) noexcept;

        static uint32_t murmur3_32(const void* data, size_t size) noexcept;
        static uint64_t murmur3_64(const void* data, size_t size) noexcept;

        static uint32_t cityHash32(const void* data, size_t size) noexcept;
        static uint64_t cityHash64(const void* data, size_t size) noexcept;
    };
}
