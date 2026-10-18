#pragma once
#include <algorithm>
#include <optional>

namespace hyp
{
    template <typename T>
    using TOptRef = std::optional<std::reference_wrapper<T>>;

    template <typename T>
    using TOptCRef = std::optional<std::reference_wrapper<const T>>;

    template<std::size_t N>
    struct FixedString
    {
        char data[N];

        constexpr FixedString(const char(&s)[N])
        {
            std::copy_n(s, N, data);
        }
    };

    enum class AllocateFrequency
    {
        Unique,
        Low,
        Medium,
        High,
    };

    template <typename T, AllocateFrequency ALLOC_FREQ>
    static consteval size_t poolChunkCapacity()
    {
        if constexpr (ALLOC_FREQ == AllocateFrequency::Unique)
            return 1;

        constexpr size_t size = sizeof(T);

        constexpr size_t pageTarget = (ALLOC_FREQ == AllocateFrequency::Low) ? 4096 : // 1 Page
            (ALLOC_FREQ == AllocateFrequency::Medium) ? 16384 : // 4 Pages
            65536;  // 16 Pages

        size_t count = pageTarget / size;

        if constexpr (ALLOC_FREQ == AllocateFrequency::High)
        {
            count = std::max<size_t>(count, 512);
        }
        else if constexpr (ALLOC_FREQ == AllocateFrequency::Medium)
        {
            count = std::max<size_t>(count, 128);
        }
        else if constexpr (ALLOC_FREQ == AllocateFrequency::Low)
        {
            count = std::max<size_t>(count, 32);
        }

        // 确保对象数量是 16 的倍数
        return (count + 15) & ~15;
    }
}
