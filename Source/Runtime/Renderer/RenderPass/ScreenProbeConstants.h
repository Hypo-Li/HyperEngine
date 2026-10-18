#pragma once
#include <cstdint>

namespace hyp
{
    enum class ScreenProbeIrradianceFormat : uint32_t
    {
        SH3 = 0,
        Octahedral = 1,
    };

    // 跟 Shader/ScreenProbe/ScreenProbeIrradianceFormat.slang 里的 SCREEN_PROBE_IRRADIANCE_FORMAT 必须保持一致
    constexpr ScreenProbeIrradianceFormat kScreenProbeIrradianceFormat = ScreenProbeIrradianceFormat::SH3;

    // 跟 Shader/ScreenProbe/ScreenProbeCommon.slang 里的同名常量必须保持一致
    constexpr uint32_t kScreenProbeIrradianceWithBorderResolution = 8u;

    constexpr uint32_t kScreenProbeRadianceSHDirectionalTexelsPerProbe = 6u;

    // SH3 要 ambient + directional 两张图, 八面体只要一张带边框图集, 消费方的 BindGroupCache 大小随之差 1
    constexpr size_t kScreenProbeIrradianceTextureCount =
        kScreenProbeIrradianceFormat == ScreenProbeIrradianceFormat::SH3 ? 2u : 1u;
}
