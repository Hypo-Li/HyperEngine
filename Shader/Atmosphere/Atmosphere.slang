#include "../Common/Binding.slang"
#define VARIABLE_SAMPLE_COUNT_ENABLED
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
Sampler2D SkyViewLutTexture;

[[vk::binding(4, 2)]]
Sampler3D AerialPerspectiveLutTexture;

[[vk::binding(5, 2)]]
Texture2D<float> SceneDepthImage;

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

[shader("vertex")]
void VertexMain(in uint vertexID : SV_VertexID, out float4 oPosition : SV_Position)
{
    oPosition = float4(kPositions[vertexID], 0, 1);
}

static constexpr float kSunDiskRadius = 0.53;
static constexpr float kCosHalfApex = cos(kSunDiskRadius * DEG_TO_RAD);

float interleavedGradientNoise(float2 uv, float frameId)
{
	uv += frameId * (float2(47, 17) * 0.695);
    static constexpr float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(uv, magic.xy)));
}

[shader("fragment")]
void FragmentMain(in float4 svPosition : SV_Position, out float4 oColor : SV_Target)
{
    float sceneDepth = SceneDepthImage[uint2(svPosition.xy)].r;

    ViewData viewData = sViewData[0];
    float2 uv = svPosition.xy / viewData.resolution.xy;
    float4 clipSpace = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, 0.5, 1.0);
    float4 viewSpace = mul(clipSpace, viewData.clipToView);
    viewSpace *= 1.0 / viewSpace.w;
    float3 worldDir = normalize(mul(viewData.worldToViewRot, viewSpace.xyz));
    float3 worldPos = getWorldPos(Atmosphere, viewData.cameraOrigin);
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
            float3 trans = getTransmittance(Atmosphere, TransmittanceLutTexture, viewZenithCos, viewHeight);
            float softEdge = clamp(2.0 * (VoL - kCosHalfApex) / (1.0 - kCosHalfApex), 0.0, 1.0);
            outLuminance += trans * softEdge;
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
            outLuminance += SkyViewLutTexture.SampleLevel(skyViewUV, 0.0).rgb;
            oColor = float4(outLuminance, 1.0);
            return;
        }
    }

    if (!moveToTopAtmosphere(Atmosphere, worldPos, worldDir))
    {
        oColor = float4(outLuminance, 1.0);
        return;
    }

    clipSpace.z = sceneDepth;
    viewSpace = mul(clipSpace, viewData.clipToView);
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
        float4 AP = weight * AerialPerspectiveLutTexture.SampleLevel(float3(uv, w), 0.0);
        oColor = float4(AP.rgb * light0Color, 1.0 - AP.a);
    }
    else
    {
        float3 lum, fms, trans;
        rayMarchAtmosphere(Atmosphere, TransmittanceLutTexture, MultiScatteringLutTexture, worldPos, worldDir, light0Dir, interleavedGradientNoise(svPosition.xy, 0 % 8), 0.0, 9000000, lum, fms, trans);
        oColor = float4(lum * light0Color, dot(trans, float3(0.3333333)));
    }
}