#pragma once
#include "Font.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hyp
{
    struct FontFaceInfo
    {
        std::string familyName;
        std::string styleName;
        uint32_t faceCount = 0;
        uint32_t glyphCount = 0;
        uint32_t unitsPerEm = 0;
    };

    struct FontBakeResult
    {
        // 每页 pageSize * pageSize 个像素, 格式为 getFontPagePixelFormat(sdfType)
        std::vector<std::vector<uint8_t>> pages;
        // 按 codepoint 升序
        std::vector<FontGlyph> glyphs;
        FontGlyph notdefGlyph;
        // 按 (left, right) 升序
        std::vector<FontKerningPair> kerning;
        float ascent = 0.0f;
        float descent = 0.0f;
        float lineGap = 0.0f;
        float underlinePosition = 0.0f;
        float underlineThickness = 0.0f;
        std::vector<uint32_t> missingCodepoints;
    };

    // 运行期没有实现, getService<IFontUtility>() 返回 nullptr
    class IFontUtility
    {
    public:
        virtual ~IFontUtility() = default;

        virtual std::optional<FontFaceInfo> inspectFace(std::span<const uint8_t> faceData, uint32_t faceIndex) = 0;

        // 可以在后台线程调用
        // progress 返回 false 表示取消, 取消和失败都返回 nullopt
        virtual std::optional<FontBakeResult> bake(
            std::span<const uint8_t> faceData,
            const FontBakeSettings& settings,
            const std::function<bool(float)>& progress) = 0;
    };
}
