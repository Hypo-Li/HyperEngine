#pragma once
#include "RenderableProxy.h"

#include <Core/Math/Vector.h>
#include <RenderBase/Vulkan/Buffer.h>
#include <RenderBase/Vulkan/CommandBuffer.h>

namespace hyp
{
    struct AtmosphereShaderParameters
    {
        float3 rayleighScattering;
        float rayleighScaleHeight;
        float3 mieScattering;
        float mieScaleHeight;
        float3 mieAbsorption;
        float miePhaseG;
        float3 ozoneAbsorption;
        float ozoneHeight;
        float ozoneThickness;
        float multiScatteringFactor;
        float bottomRadiusKm;
        float topRadiusKm;
        float3 groundAlbedo;
        float padding0;
        float3 sun0Direction;
        float padding1;
        float3 sun0Intensity;
        float padding2;
        float3 sun1Direction;
        float padding3;
        float3 sun1Intensity;
    };

    class AtmosphereProxy : public RenderableProxy
    {
    public:
        AtmosphereProxy(const AtmosphereShaderParameters& parameters);

        ~AtmosphereProxy() noexcept;

        void updateParameters(const AtmosphereShaderParameters& parameters);

        void setSunLight(uint32_t sunIndex, float3 direction, float3 intensity);

        void prepare(vk::CommandBuffer& cmd);

        vk::Buffer& getParametersBuffer() const noexcept
        {
            return *mParametersBuffer;
        }

    private:
        void uploadToGPU(vk::CommandBuffer& cmd);

    private:
        AtmosphereShaderParameters mParameters;
        bool mParametersDirty = false;
        TRef<vk::Buffer> mParametersBuffer;
    };

    template <>
    struct TProxyTraits<ProxyType::Atmosphere>
    {
        using Proxy = AtmosphereProxy;
    };
}
