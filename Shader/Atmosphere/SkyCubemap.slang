#include "../Common/Binding.slang"
#define VARIABLE_SAMPLE_COUNT_ENABLED
#define MIE_RAYLEIGH_PHASE_ENABLED
#define MULTI_SCATTERING_APPROX_SAMPLING_ENABLED
#define PREMULTIPLY_LIGHTING
#include "AtmosphereCommon.slang"
#include "../Common/Cubemap.slang"

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
RWTexture2DArray<float4> SkyCubemapImage;

struct PushConstants
{
    WorldCoord captureOrigin;
    uint hasBackground;
};
[[vk::push_constant]]
PushConstants PC;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 globalID : SV_DispatchThreadID)
{
    uint width, height, layers;
    SkyCubemapImage.GetDimensions(width, height, layers);

    if (globalID.x >= width || globalID.y >= height || globalID.z >= layers)
        return;

    float2 uv = float2(globalID.xy + 0.5) / float2(width, height);

    float3 worldDir = faceUVToCubemapDir(globalID.z, uv);

    float3 background = PC.hasBackground != 0 ? SkyCubemapImage[globalID].rgb : float3(0.0);

    float3 worldPos = getWorldPos(Atmosphere, PC.captureOrigin);
    bool intersectGround = rayIntersectSphere(worldPos, worldDir, float4(0, 0, 0, Atmosphere.bottomRadiusKm)) >= 0.0;
    if (!moveToTopAtmosphere(Atmosphere, worldPos, worldDir))
    {
        SkyCubemapImage[globalID] = float4(background, 1.0);
        return;
    }
    float3 lum, fms, trans;
    rayMarchAtmosphere(Atmosphere, LutSampler, TransmittanceLutTexture, MultiScatteringLutTexture, worldPos, worldDir, Atmosphere.sun0Direction, 0.3, 30.0, 9000000.0, lum, fms, trans);
    float transmittance = intersectGround ? 0.0 : dot(trans, float3(1.0 / 3.0));
    SkyCubemapImage[globalID] = float4(background * transmittance + lum, 1.0);
}
