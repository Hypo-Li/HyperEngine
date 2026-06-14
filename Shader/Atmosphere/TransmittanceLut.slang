#include "AtmosphereCommon.slang"

[[vk::binding(0, 2)]]
ConstantBuffer<AtmosphereParameters> Atmosphere;

[[vk::binding(1, 2)]]
RWTexture2D<float4> TransmittanceLutImage;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 globalID : SV_DispatchThreadID)
{
    float2 uv = (globalID.xy + 0.5) / float2(256, 64);
    float viewHeight, viewZenithCos;
    uvToTransmittanceLutParameters(Atmosphere, uv, viewHeight, viewZenithCos);
    float3 worldPos = float3(0, 0, viewHeight);
    float3 worldDir = float3(0, sqrt(1.0 - viewZenithCos * viewZenithCos), viewZenithCos);
    float3 transmittance = exp(-getOpticalDepth(Atmosphere, worldPos, worldDir, 10.0));
    TransmittanceLutImage[globalID.xy] = float4(transmittance, 1);
}