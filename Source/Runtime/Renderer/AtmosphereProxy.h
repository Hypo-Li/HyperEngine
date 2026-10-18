#pragma once
#include "SceneElementProxy.h"

#include <Core/Math/Vector.h>
#include <RHI/Buffer.h>
#include <RHI/CommandList.h>

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
        float padding4;
    };

    class AtmosphereProxy : public SceneElementProxy
    {
    public:
        struct Desc
        {
            bool visible = true;
            AtmosphereShaderParameters parameters;
        };

        AtmosphereProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~AtmosphereProxy() noexcept override;

        void updateParameters(const AtmosphereShaderParameters& parameters);

        void setSunLight(uint32_t sunIndex, float3 direction, float3 intensity);

        void prepare(rhi::CommandList& cmdList);

        rhi::Buffer& getParametersBuffer() const noexcept
        {
            return *mParametersBuffer;
        }

    private:
        void uploadToGPU(rhi::CommandList& cmdList);

    private:
        AtmosphereShaderParameters mParameters;
        bool mParametersDirty = false;
        TRef<rhi::Buffer> mParametersBuffer;
    };

    template <>
    struct TProxyTraits<ProxyType::Atmosphere>
    {
        using Proxy = AtmosphereProxy;
    };
}
