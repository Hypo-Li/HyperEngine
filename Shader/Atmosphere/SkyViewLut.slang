#include "../Common/Binding.slang"
#define VARIABLE_SAMPLE_COUNT_ENABLED
#define MIE_RAYLEIGH_PHASE_ENABLED
#define MULTI_SCATTERING_APPROX_SAMPLING_ENABLED
#define PREMULTIPLY_LIGHTING
#include "AtmosphereCommon.slang"

[[vk::binding(0, 2)]]
ConstantBuffer<AtmosphereParameters> Atmosphere;

[[vk::binding(1, 2)]]
SamplerState LutSampler;

[[vk::binding(2, 2)]]
Texture2D<float4> TransmittanceLutTexture;

[[vk::binding(3, 2)]]
Texture2D<float4> MultiScatteringLutTexture;

[[vk::binding(4, 2)]]
[format("rgba16f")]
WTexture2D<float4> SkyViewLutImage;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 globalID : SV_DispatchThreadID)
{
    float2 uv = (globalID.xy + 0.5) / float2(192, 104);
    float3 worldPos = getWorldPos(Atmosphere, uViewData.cameraOrigin);
    float viewHeight = length(worldPos);
    float viewZenithCos, lightViewCos, lightViewSin;
    uvToSkyViewLutParameters(Atmosphere, viewHeight, uv, viewZenithCos, lightViewCos, lightViewSin);

    float3 upVector = worldPos / viewHeight;
    float lightZenithCos = dot(upVector, Atmosphere.sun0Direction);
    float3 lightDir = normalize(float3(sqrt(max(1.0 - lightZenithCos * lightZenithCos, 0.0)), 0.0, lightZenithCos));

    float viewZenithSin = sqrt(max(1.0 - viewZenithCos * viewZenithCos, 0.0));
    float3 worldDir;
    worldDir.x = viewZenithSin * lightViewCos;
    worldDir.y = viewZenithSin * lightViewSin;
    worldDir.z = viewZenithCos;

    worldPos = float3(0.0, 0.0, viewHeight);
    if (!moveToTopAtmosphere(Atmosphere, worldPos, worldDir))
    {
        SkyViewLutImage.Store(globalID.xy, float4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    float3 lum, fms, trans;
    rayMarchAtmosphere(Atmosphere, LutSampler, TransmittanceLutTexture, MultiScatteringLutTexture, worldPos, worldDir, lightDir, 0.3, 30.0, 9000000.0, lum, fms, trans);
    SkyViewLutImage.Store(globalID.xy, float4(lum, dot(trans, float3(1.0 / 3.0))));
}