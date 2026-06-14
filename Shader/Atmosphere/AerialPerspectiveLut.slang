#include "../Common/Binding.slang"
#define MIE_RAYLEIGH_PHASE_ENABLED
#define MULTI_SCATTERING_APPROX_SAMPLING_ENABLED
#include "AtmosphereCommon.slang"

[[vk::binding(0, 2)]]
ConstantBuffer<AtmosphereParameters> Atmosphere;

[[vk::binding(1, 2)]]
Sampler2D TransmittanceLutTexture;

[[vk::binding(2, 2)]]
Sampler2D MultiScatteringLutTexture;

[[vk::binding(3, 2)]]
RWTexture3D<float4> AerialPerspectiveLutImage;

[shader("compute")]
[numthreads(4, 4, 4)]
void ComputeMain(uint3 globalID : SV_DispatchThreadID)
{
    ViewData viewData = sViewData[0];
    float2 uv = (globalID.xy + 0.5) / float2(32, 32);
    float4 clipSpace = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, 0.5, 1.0);
    float4 hViewPos = mul(clipSpace, viewData.clipToView);
    float3 worldDir = normalize(mul(viewData.worldToViewRot, hViewPos.xyz / hViewPos.w));
    float3 worldPos = getWorldPos(Atmosphere, viewData.cameraOrigin);

    float slice = (globalID.z + 0.5) / SLICE_COUNT;
    slice *= slice * SLICE_COUNT;
    float tMax = aerialPerspectiveSliceToDepth(slice);
    // ray marching endpoint
    float3 newPos = worldPos + tMax * worldDir;
    float viewHeight = length(newPos);
    // determine if the endpoint is underground
    if (viewHeight <= Atmosphere.bottomRadiusKm + PLANET_RADIUS_OFFSET)
    {
        // move the endpoint above the ground
        newPos = normalize(newPos) * (Atmosphere.bottomRadiusKm + PLANET_RADIUS_OFFSET + 0.01);
        worldDir = normalize(newPos - worldPos);
        tMax = length(newPos - worldPos);
    }
    float tMaxMax = tMax;
    viewHeight = length(worldPos);
    // determine whether the viewpoint is outside the atmosphere
    if (viewHeight >= Atmosphere.topRadiusKm)
    {
        float3 prevWorldPos = worldPos;
        // move the viewpoint around the atmosphere and avoid inefficient stepping
        if (!moveToTopAtmosphere(Atmosphere, worldPos, worldDir))
        {
            AerialPerspectiveLutImage[globalID] = float4(0, 0, 0, 1);
            return;
        }
        float lengthToAtmosphere = length(prevWorldPos - worldPos);
        // determines whether the maximum length of the step is less than the length of the move
        if (tMaxMax < lengthToAtmosphere)
        {
            AerialPerspectiveLutImage[globalID] = float4(0, 0, 0, 1);
            return;
        }
        // the maximum length of the corrected step is the length of the step actually taken in the atmosphere
        tMaxMax = max(0.0, tMaxMax - lengthToAtmosphere);
    }
    const float sampleCount = max(1.0, float(globalID.z + 1.0) * 2.0);
    float3 lum, fms, trans;
    rayMarchAtmosphere(Atmosphere, TransmittanceLutTexture, MultiScatteringLutTexture, worldPos, worldDir, Atmosphere.sun0Direction, 0.3, sampleCount, tMaxMax, lum, fms, trans);
    AerialPerspectiveLutImage[globalID] = float4(lum, 1.0 - dot(trans, float3(1.0/3.0)));
}