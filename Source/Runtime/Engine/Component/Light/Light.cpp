#include "Light.h"

namespace hyp
{
    REGISTER_CLASS(Light)

    void Light::setIntensity(float intensity)
    {
        if (mIntensity == intensity)
            return;

        mIntensity = intensity;
        applyLightIntensity();
    }

    void Light::setColor(float3 linearColor)
    {
        if (mColor == linearColor)
            return;

        mColor = linearColor;
        applyLightIntensity();
    }

    void Light::setUseTemperature(bool useTemperature)
    {
        if (mUseTemperature == useTemperature)
            return;

        mUseTemperature = useTemperature;
        applyLightIntensity();
    }

    void Light::setTemperature(float temperature)
    {
        if (mTemperature == temperature)
            return;

        mTemperature = temperature;
        applyLightIntensity();
    }

    float3 Light::calculateIntensity()
    {
        float3 temperatureColor = float3(1.0f);
        if (mUseTemperature)
        {
            float t = std::clamp(mTemperature, 1000.0f, 15000.0f);

            // Approximate Planckian locus in CIE 1960 UCS
            float u = (0.860117757f + 1.54118254e-4f * t + 1.28641212e-7f * t * t) / (1.0f + 8.42420235e-4f * t + 7.08145163e-7f * t * t);
            float v = (0.317398726f + 4.22806245e-5f * t + 4.20481691e-8f * t * t) / (1.0f - 2.89741816e-5f * t + 1.61456053e-7f * t * t);

            float x = 3.0f * u / (2.0f * u - 8.0f * v + 4.0f);
            float y = 2.0f * v / (2.0f * u - 8.0f * v + 4.0f);
            float z = 1.0f - x - y;

            float Y = 1.0f;
            float X = Y / y * x;
            float Z = Y / y * z;

            // XYZ to RGB with BT.709 primaries
            float R = 3.2404542f * X + -1.5371385f * Y + -0.4985314f * Z;
            float G = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
            float B = 0.0556434f * X + -0.2040259f * Y + 1.0572252f * Z;

            // The XYZ to RGB transform can result in negative values, so we need to clamp here.
            temperatureColor = float3(std::max(0.0f, R), std::max(0.0f, G), std::max(0.0f, B));
        }
        return temperatureColor * mColor * mIntensity;
    }

    template <> Type* Reflection::createType<Light>()
    {
        auto c = new TClass<Light, Component>("Light");
        c->addProperty("Intensity", &Light::mIntensity);
        c->addProperty("Color", &Light::mColor);
        c->addProperty("Use Temperature", &Light::mUseTemperature);
        c->addProperty("Temperature", &Light::mTemperature);
        c->addProperty("Cast Shadows", &Light::mCastShadows);
        return c;
    }
}
