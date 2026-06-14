#define NO_MULTI_SCATTERING_LUT_TEXTURE
#define GROUND_ALBEDO_ENABLED
#include "AtmosphereCommon.slang"

[[vk::binding(0, 2)]]
ConstantBuffer<AtmosphereParameters> Atmosphere;

[[vk::binding(1, 2)]]
Sampler2D TransmittanceLutTexture;

[[vk::binding(2, 2)]]
RWTexture2D<float4> MultiScatteringLutImage;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 globalID : SV_DispatchThreadID)
{
    float2 uv = (globalID.xy + 0.5) / float2(32, 32);
    float lightZenithCos = uv.x * 2.0 - 1.0;
    float3 lightDir = float3(0, sqrt(1.0 - lightZenithCos * lightZenithCos), lightZenithCos);
    float viewHeight = Atmosphere.bottomRadiusKm + uv.y * (Atmosphere.topRadiusKm - Atmosphere.bottomRadiusKm);
    float3 worldPos = float3(0, 0, viewHeight);
    float3 worldDir = float3(0, 0, 1);

    float sphereSolidAngle = 4.0 * PI;
    float isotropicPhase = 1.0 / sphereSolidAngle;
    float3 lum0, fms0, trans0;
    rayMarchAtmosphere(Atmosphere, TransmittanceLutTexture, worldPos, worldDir, lightDir, 0.3, 15.0, 9000000.0, lum0, fms0, trans0);
    float3 lum1, fms1, trans1;
    rayMarchAtmosphere(Atmosphere, TransmittanceLutTexture, worldPos, -worldDir, lightDir, 0.3, 15.0, 9000000.0, lum1, fms1, trans1);
    float3 integratedIlluminance = (sphereSolidAngle * 0.5) * (lum0 + lum1);
    float3 fms = 0.5 * (fms0 + fms1);
    float3 fmsSqr = fms * fms;
    float3 inScatteredLuminance = integratedIlluminance * isotropicPhase;
    float3 L = inScatteredLuminance * (1.0 + fms + fmsSqr + fms * fmsSqr + fmsSqr * fmsSqr);
    MultiScatteringLutImage[globalID.xy] = float4(L, 1);
}