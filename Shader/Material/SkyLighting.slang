#include "../Common/Binding.slang"
#include "BxDF.slang"

[[vk::binding(0, 2)]]
SamplerState LinearSampler;

[[vk::binding(1, 2)]]
Texture2D<float4> GbufferAImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(3, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(4, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(5, 2)]]
TextureCube<float4> SkyLightSpecularPrefilter;

// SkyLightPass(SHCoeffGather + SHCoeffReduce)产出的打包球谐系数, 已经在 GPU 上按 BxDF.slang
// evaluateIBL() 期望的"7 个 float4 二次多项式"格式打包好, 不需要再乘归一化常数
[[vk::binding(6, 2)]]
StructuredBuffer<float4> SkyLightPackedSHCoeff;

[[vk::binding(7, 2)]]
Texture2D DfgLut;

[[vk::binding(8, 2)]]
SamplerState LutSampler;

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

[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    float sceneDepth = SceneDepthImage[uint2(svPosition.xy)].r;
    if (sceneDepth == 0)
    {
        return float4(0, 0, 0, 1);
    }

    float4 gbufferA = GbufferAImage[uint2(svPosition.xy)];
    float4 gbufferB = GbufferBImage[uint2(svPosition.xy)];
    float4 gbufferC = GbufferCImage[uint2(svPosition.xy)];

    float3 normalWS = gbufferA.xyz * 2 - 1;
    float metallic = max(gbufferB.x, 0.045);
    float roughness = gbufferB.y;
    float specular = gbufferB.z;
    float3 baseColor = gbufferC.xyz;
    float occlusion = gbufferC.w;

    float3 albedo = baseColor * (1 - metallic);
    float3 F0 = lerp(float3(0.16 * specular * specular), baseColor, metallic);

    ViewData viewData = sViewData[0];

    float2 uv = svPosition.xy / viewData.resolution.xy;
    float4 fragPosCS = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, sceneDepth, 1);
    float4 fragPosVS = mul(fragPosCS, viewData.clipToView);
    fragPosVS *= 1.0 / fragPosVS.w;
    float3 viewDirVS = normalize(-fragPosVS.xyz);
    float3 viewDirWS = mul(viewData.worldToViewRot, viewDirVS);
    float3 reflectDirWS = reflect(-viewDirWS, normalWS);

    float4 packedSH[7];
    [unroll]
    for (int i = 0; i < 7; ++i)
    {
        packedSH[i] = SkyLightPackedSHCoeff[i];
    }

    float3 color = evaluateIBL(
        albedo,
        roughness,
        F0,
        normalWS,
        viewDirWS,
        reflectDirWS,
        packedSH,
        DfgLut,
        LutSampler,
        SkyLightSpecularPrefilter,
        LinearSampler
    );

    return float4(color * occlusion, 1);
}
