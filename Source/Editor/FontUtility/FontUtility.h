#pragma once
#include <Engine/Font/FontUtility.h>

namespace hyp
{
    class FontUtility : public IFontUtility
    {
    public:
        std::optional<FontFaceInfo> inspectFace(std::span<const uint8_t> faceData, uint32_t faceIndex) override;

        std::optional<FontBakeResult> bake(
            std::span<const uint8_t> faceData,
            const FontBakeSettings& settings,
            const std::function<bool(float)>& progress) override;
    };
}
