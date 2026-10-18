#pragma once
#include <Engine/Font/Font.h>

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace hyp
{
    // progress 返回 false 时取消并返回 nullopt
    std::optional<std::vector<FontKerningPair>> bakeKerningPairs(
        std::span<const uint8_t> faceData,
        uint32_t faceIndex,
        std::span<const uint32_t> codepoints,
        const std::function<bool(float)>& progress);
}
