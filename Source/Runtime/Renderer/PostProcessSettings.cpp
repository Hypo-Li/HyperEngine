#include "PostProcessSettings.h"

namespace hyp
{
    template <> Type* Reflection::createType<AntiAliasingMethod>()
    {
        auto e = new TEnumeration<AntiAliasingMethod>("AntiAliasingMethod");
        e->addValue("None", AntiAliasingMethod::None);
        e->addValue("TAA", AntiAliasingMethod::TAA);
        return e;
    }

    template <> Type* Reflection::createType<AntiAliasingSettings>()
    {
        auto s = new TStructure<AntiAliasingSettings>("AntiAliasingSettings");
        s->addProperty("Method", &AntiAliasingSettings::method);
        return s;
    }

    template <> Type* Reflection::createType<ExposureMode>()
    {
        auto e = new TEnumeration<ExposureMode>("ExposureMode");
        e->addValue("Manual", ExposureMode::Manual);
        e->addValue("Automatic", ExposureMode::Automatic);
        return e;
    }

    template <> Type* Reflection::createType<MeteringMode>()
    {
        auto e = new TEnumeration<MeteringMode>("MeteringMode");
        e->addValue("Histogram", MeteringMode::Histogram);
        return e;
    }

    template <> Type* Reflection::createType<ExposureSettings>()
    {
        auto s = new TStructure<ExposureSettings>("ExposureSettings");
        s->addProperty("Enabled", &ExposureSettings::enabled);
        s->addProperty("Mode", &ExposureSettings::mode);
        s->addProperty("Aperture", &ExposureSettings::aperture);
        s->addProperty("Shutter Speed", &ExposureSettings::shutterSpeed);
        s->addProperty("ISO", &ExposureSettings::iso);
        s->addProperty("Metering Mode", &ExposureSettings::meteringMode);
        s->addProperty("Min EV100", &ExposureSettings::minEV100);
        s->addProperty("Max EV100", &ExposureSettings::maxEV100);
        s->addProperty("Histogram Min EV100", &ExposureSettings::histogramMinEV100);
        s->addProperty("Histogram Max EV100", &ExposureSettings::histogramMaxEV100);
        s->addProperty("Histogram Low Percent", &ExposureSettings::histogramLowPercent);
        s->addProperty("Histogram High Percent", &ExposureSettings::histogramHighPercent);
        s->addProperty("Adaptation Speed Up", &ExposureSettings::adaptationSpeedUp);
        s->addProperty("Adaptation Speed Down", &ExposureSettings::adaptationSpeedDown);
        s->addProperty("Exposure Compensation", &ExposureSettings::exposureCompensation);
        return s;
    }

    template <> Type* Reflection::createType<ColorGradingSettings>()
    {
        auto s = new TStructure<ColorGradingSettings>("ColorGradingSettings");
        s->addProperty("Temperature", &ColorGradingSettings::temperature);
        s->addProperty("Tint", &ColorGradingSettings::tint);
        s->addProperty("Out Red", &ColorGradingSettings::outRed);
        s->addProperty("Out Green", &ColorGradingSettings::outGreen);
        s->addProperty("Out Blue", &ColorGradingSettings::outBlue);
        s->addProperty("Shadows", &ColorGradingSettings::shadows);
        s->addProperty("Midtones", &ColorGradingSettings::midtones);
        s->addProperty("Highlights", &ColorGradingSettings::highlights);
        s->addProperty("Ranges", &ColorGradingSettings::ranges);
        s->addProperty("Slope", &ColorGradingSettings::slope);
        s->addProperty("Offset", &ColorGradingSettings::offset);
        s->addProperty("Power", &ColorGradingSettings::power);
        s->addProperty("Contrast", &ColorGradingSettings::contrast);
        s->addProperty("Vibrance", &ColorGradingSettings::vibrance);
        s->addProperty("Saturation", &ColorGradingSettings::saturation);
        return s;
    }

    template <> Type* Reflection::createType<BloomSettings>()
    {
        auto s = new TStructure<BloomSettings>("BloomSettings");
        s->addProperty("Enabled", &BloomSettings::enabled);
        s->addProperty("Intensity", &BloomSettings::intensity);
        s->addProperty("Iterations", &BloomSettings::iterations);
        s->addProperty("Upsample Radius", &BloomSettings::upsampleRadius);
        return s;
    }

    template <> Type* Reflection::createType<AmbientOcclusionQuality>()
    {
        auto e = new TEnumeration<AmbientOcclusionQuality>("AmbientOcclusionQuality");
        e->addValue("Low", AmbientOcclusionQuality::Low);
        e->addValue("Medium", AmbientOcclusionQuality::Medium);
        e->addValue("High", AmbientOcclusionQuality::High);
        e->addValue("Very High", AmbientOcclusionQuality::VeryHigh);
        e->addValue("Ultra", AmbientOcclusionQuality::Ultra);
        return e;
    }

    template <> Type* Reflection::createType<AmbientOcclusionMethod>()
    {
        auto e = new TEnumeration<AmbientOcclusionMethod>("AmbientOcclusionMethod");
        e->addValue("SSAO", AmbientOcclusionMethod::SSAO);
        e->addValue("GTAO", AmbientOcclusionMethod::GTAO);
        return e;
    }

    template <> Type* Reflection::createType<AmbientOcclusionSettings>()
    {
        auto s = new TStructure<AmbientOcclusionSettings>("AmbientOcclusionSettings");
        s->addProperty("Enabled", &AmbientOcclusionSettings::enabled);
        s->addProperty("Method", &AmbientOcclusionSettings::method);
        s->addProperty("Intensity", &AmbientOcclusionSettings::intensity);
        s->addProperty("Power", &AmbientOcclusionSettings::power);
        s->addProperty("Radius", &AmbientOcclusionSettings::radius);
        s->addProperty("Fade Distance", &AmbientOcclusionSettings::fadeDistance);
        s->addProperty("Fade Radius", &AmbientOcclusionSettings::fadeRadius);
        s->addProperty("Quality", &AmbientOcclusionSettings::quality);
        return s;
    }

    template <> Type* Reflection::createType<ToneMapper>()
    {
        auto e = new TEnumeration<ToneMapper>("ToneMapper");
        e->addValue("Linear", ToneMapper::Linear);
        e->addValue("ACES", ToneMapper::ACES);
        e->addValue("UE Filmic", ToneMapper::UE_Filmic);
        return e;
    }

    template <> Type* Reflection::createType<ToneMappingSettings>()
    {
        auto s = new TStructure<ToneMappingSettings>("ToneMappingSettings");
        s->addProperty("Tone Mapper", &ToneMappingSettings::toneMapper);
        return s;
    }

    template <> Type* Reflection::createType<PostProcessSettings>()
    {
        auto s = new TStructure<PostProcessSettings>("PostProcessSettings");
        s->addProperty("Anti Aliasing", &PostProcessSettings::antiAliasing);
        s->addProperty("Exposure", &PostProcessSettings::exposure);
        s->addProperty("Bloom", &PostProcessSettings::bloom);
        s->addProperty("Ambient Occlusion", &PostProcessSettings::ambientOcclusion);
        s->addProperty("Color Grading", &PostProcessSettings::colorGrading);
        s->addProperty("Tone Mapping", &PostProcessSettings::toneMapping);
        return s;
    }
}
