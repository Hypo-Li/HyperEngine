#pragma once
#include <Core/Math/Vector.h>

namespace hyp
{
    enum class AntiAliasingMethod
    {
        None,
        TAA,
    };

    template <> Type* Reflection::createType<AntiAliasingMethod>();

    struct AntiAliasingSettings
    {
        AntiAliasingMethod method = AntiAliasingMethod::TAA;
    };

    enum class ExposureMode
    {
        Manual,
        Automatic,
    };

    template <> Type* Reflection::createType<ExposureMode>();

    enum class MeteringMode
    {
        Histogram,
    };

    template <> Type* Reflection::createType<MeteringMode>();

    struct ExposureSettings
    {
        bool enabled = true;
        ExposureMode mode = ExposureMode::Automatic;

        // Manual
        float aperture = 4.0f;
        float shutterSpeed = 1.0f / 60.0f;
        float iso = 100.0f;

        // Automatic
        MeteringMode meteringMode = MeteringMode::Histogram;
        float minEV100 = -10.0f;
        float maxEV100 = 20.0f;
        float histogramMinEV100 = -10.0f;
        float histogramMaxEV100 = 20.0f;
        float histogramLowPercent = 10.0f;
        float histogramHighPercent = 90.0f;
        float adaptationSpeedUp = 3.0f;
        float adaptationSpeedDown = 1.0f;

        // Common
        float exposureCompensation = 1.0f;

    };

    template <> Type* Reflection::createType<ExposureSettings>();

    struct ColorGradingSettings
    {
        // white balance
        float temperature;
        float tint;

        // channel mixer
        float3 outRed;
        float3 outGreen;
        float3 outBlue;

        // tonal ranges
        float4 shadows;
        float4 midtones;
        float4 highlights;
        float4 ranges;

        // color decision list
        float3 slope;
        float3 offset;
        float3 power;

        // adjustments
        float contrast;
        float vibrance;
        float saturation;
    };

    template <> Type* Reflection::createType<ColorGradingSettings>();

    enum class ToneMapper
    {
        Linear,
        ACES,
        UE_Filmic,
    };

    template <> Type* Reflection::createType<ToneMapper>();

    struct ToneMappingSettings
    {
        ToneMapper toneMapper = ToneMapper::UE_Filmic;
    };

    template <> Type* Reflection::createType<ToneMappingSettings>();

    struct BloomSettings
    {
        bool enabled = true;
        float intensity = 0.01f;
        uint32_t iterations = 6;
        float upsampleRadius = 1.0f;
    };

    template <> Type* Reflection::createType<BloomSettings>();

    enum class AmbientOcclusionQuality
    {
        Low,
        Medium,
        High,
        VeryHigh,
        Ultra,
    };

    template <> Type* Reflection::createType<AmbientOcclusionQuality>();

    enum class AmbientOcclusionMethod
    {
        SSAO,
        GTAO,
    };

    template <> Type* Reflection::createType<AmbientOcclusionMethod>();

    struct AmbientOcclusionSettings
    {
        bool enabled = true;
        AmbientOcclusionMethod method = AmbientOcclusionMethod::SSAO;
        // 对应 UE AmbientOcclusionIntensity 默认值(Scene.cpp:567)
        float intensity = 0.5f;
        float power = 2.0f;
        // UE AmbientOcclusionRadius 的米制等价值(view space 相对半径, /400cm 换算成 /4m,
        // 具体数值仍要按场景实际尺度试调)
        float radius = 2.0f;
        // View-space distance where AO has completely faded out. A non-positive
        // value disables distance fading.
        float fadeDistance = 80.0f;
        float fadeRadius = 50.0f;
        // 只有 GTAO 用到, SSAO 暂时固定在 UE AmbientOcclusionQuality=50 等价档位
        AmbientOcclusionQuality quality = AmbientOcclusionQuality::High;
    };

    template <> Type* Reflection::createType<AmbientOcclusionSettings>();

    template <> Type* Reflection::createType<AntiAliasingSettings>();

    struct PostProcessSettings
    {
        AntiAliasingSettings antiAliasing;
        ExposureSettings exposure;
        BloomSettings bloom;
        AmbientOcclusionSettings ambientOcclusion;
        ColorGradingSettings colorGrading;
        ToneMappingSettings toneMapping;
    };

    template <> Type* Reflection::createType<PostProcessSettings>();
}
