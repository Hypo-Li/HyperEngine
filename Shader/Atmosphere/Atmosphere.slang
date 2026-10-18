#include "../Common/Binding.slang"
#define VARIABLE_SAMPLE_COUNT_ENABLED
#define MIE_RAYLEIGH_PHASE_ENABLED
#define MULTI_SCATTERING_APPROX_SAMPLING_ENABLED
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
Texture2D<float4> SkyViewLutTexture;

[[vk::binding(5, 2)]]
Texture3D<float4> AerialPerspectiveLutTexture;

[[vk::binding(6, 2)]]
DepthTexture2D SceneDepthImage;

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

[shader("vertex")]
float4 VertexMain(in uint vertexID : SV_VertexID) : SV_Position
{
    return float4(kPositions[vertexID], 0, 1);
}

static constexpr float kCosHalfApex = cos(kSunAngularRadiusDeg * DEG_TO_RAD);

[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    float sceneDepth = SceneDepthImage[uint2(svPosition.xy)].r;

    float preExposure = getPreExposure(uViewData);
    float2 uv = svPosition.xy / uViewData.resolution.xy;
    float4 clipSpace = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, 0.5, 1.0);
    float4 viewSpace = mul(clipSpace, uViewData.clipToView);
    viewSpace *= 1.0 / viewSpace.w;
    float3 worldDir = normalize(mul(uViewData.worldToViewRot, viewSpace.xyz));
    float3 worldPos = getWorldPos(Atmosphere, uViewData.cameraOrigin);
    float viewHeight = length(worldPos);
    bool intersectGround = rayIntersectSphere(worldPos, worldDir, float4(0, 0, 0, Atmosphere.bottomRadiusKm)) >= 0.0;
    float3 light0Dir = Atmosphere.sun0Direction;
    float3 light0Color = Atmosphere.sun0Intensity;

    float3 outLuminance = float3(0.0);
    if (sceneDepth == 0.0)
    {
        float VoL = dot(worldDir, light0Dir); // world space VoL
        float3 upVector = worldPos / viewHeight;

        if (!intersectGround && VoL > kCosHalfApex)
        {
            float viewZenithCos = max(dot(light0Dir, upVector), 0.0);
            float3 trans = getTransmittance(Atmosphere, TransmittanceLutTexture, LutSampler, viewZenithCos, viewHeight);
            float softEdge = clamp(2.0 * (VoL - kCosHalfApex) / (1.0 - kCosHalfApex), 0.0, 1.0);
            outLuminance += trans * softEdge * light0Color * 100.0;
        }

        if (viewHeight < Atmosphere.topRadiusKm)
        {
            float viewZenithCos = dot(worldDir, upVector);
            float3 sideVector = normalize(cross(upVector, worldDir));
            float3 forwardVector = normalize(cross(sideVector, upVector));
            float2 lightOnPlane = float2(dot(light0Dir, forwardVector), dot(light0Dir, sideVector));
            lightOnPlane = normalize(lightOnPlane);
            float lightViewCos = lightOnPlane.x;

            float2 skyViewUV;
            skyViewLutParametersToUV(Atmosphere, intersectGround, viewZenithCos, lightViewCos, viewHeight, skyViewUV);
            float4 skyView = SkyViewLutTexture.SampleLevel(LutSampler, skyViewUV, 0.0);
            outLuminance += skyView.rgb;
            // 视线射向行星表面时背景被行星挡住
            return float4(outLuminance * preExposure, intersectGround ? 0.0 : skyView.a);
        }
    }

    if (!moveToTopAtmosphere(Atmosphere, worldPos, worldDir))
    {
        return float4(outLuminance * preExposure, 1.0);
    }

    clipSpace.z = sceneDepth;
    viewSpace = mul(clipSpace, uViewData.clipToView);
    viewSpace *= 1.0 / viewSpace.w;
    float tDepth = abs(viewSpace.z) / 1000.0;
    if (sceneDepth == 0)
        tDepth = -1;

    if ((viewHeight - Atmosphere.bottomRadiusKm) < (Atmosphere.topRadiusKm - Atmosphere.bottomRadiusKm) * 0.05)
    {
        float slice = aerialPerspectiveDepthToSlice(tDepth);
        float weight = 1.0;
        if (slice < 0.5)
        {
            weight = clamp(slice * 2.0, 0.0, 1.0);
            slice = 0.5;
        }
        float w = sqrt(slice / SLICE_COUNT);
        float4 AP = weight * AerialPerspectiveLutTexture.SampleLevel(LutSampler, float3(uv, w), 0.0);
        return float4(AP.rgb * light0Color * preExposure, 1.0 - AP.a);
    }
    else
    {
        float3 lum, fms, trans;
        rayMarchAtmosphere(Atmosphere, LutSampler, TransmittanceLutTexture, MultiScatteringLutTexture, worldPos, worldDir, light0Dir, interleavedGradientNoise(svPosition.xy, uFrameData.frameIndex % 8), 0.0, 9000000, lum, fms, trans);
        float transmittance = (sceneDepth == 0.0 && intersectGround) ? 0.0 : dot(trans, float3(0.3333333));
        return float4(lum * light0Color * preExposure, transmittance);
    }
}
