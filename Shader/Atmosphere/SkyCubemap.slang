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
Sampler2D TransmittanceLutTexture;

[[vk::binding(2, 2)]]
Sampler2D MultiScatteringLutTexture;

[[vk::binding(3, 2)]]
RWTexture2DArray<float4> SkyCubemapImage;

// struct PushConstants
// {
//     uint cubeResolution;
//     uint instanceIndex;
// };

// [[vk::push_constant]]
// PushConstants PC;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 globalID : SV_DispatchThreadID)
{
    float2 uv = float2(globalID.xy + 0.5) / 512;

    //const InstanceData instanceData = sInstanceData[PC.instanceIndex];
    //float3 worldPos = getWorldPos(Atmosphere, instanceData.localToWorld.getTranslation());
    float3 worldDir = faceUVToCubemapDir(globalID.z, uv);

    float3 worldPos = getWorldPos(Atmosphere, sViewData[0].cameraOrigin);
    if (!moveToTopAtmosphere(Atmosphere, worldPos, worldDir))
    {
        SkyCubemapImage[globalID] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float3 lum, fms, trans;
    rayMarchAtmosphere(Atmosphere, TransmittanceLutTexture, MultiScatteringLutTexture, worldPos, worldDir, Atmosphere.sun0Direction, 0.3, 30.0, 9000000.0, lum, fms, trans);
    SkyCubemapImage[globalID] = float4(lum, 1.0);
}