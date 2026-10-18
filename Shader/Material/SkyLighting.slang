#include "../Common/Binding.slang"
#include "../Common/MaterialIO.slang"
#include "ShadingParams.slang"
#include "BxDF.slang"
#include "../Common/SkyLightData.slang"

[[vk::binding(0, 2)]]
SamplerState LinearSampler;

[[vk::binding(1, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(3, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(4, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(5, 2)]]
TextureCube<float4> SkyLightSpecularPrefilter;

// SkyLightPass(SHCoeffGather + SHCoeffReduce)产出的打包球谐系数(元素 0..6), 已经在 GPU 上按 BxDF.slang
// evaluateIBL() 期望的"7 个 float4 二次多项式"格式打包好, 不需要再乘归一化常数
[[vk::binding(6, 2)]]
StructuredBuffer<float4> SkyLightData;

[[vk::binding(7, 2)]]
Texture2D DfgLut;

[[vk::binding(8, 2)]]
SamplerState LutSampler;

// AOPass(AOGenerate -> AOSpatialFilter -> AOTemporalFilter)收敛好的屏幕空间 AO——已经在自己
// 独立的时域历史里稳定过, 这里拿到的是干净的结果, 不需要关心背后的噪声/收敛问题。AO 关闭时
// IndirectLightingPass 回退到白纹理, 数值上等价于没有屏幕空间 AO。
[[vk::binding(9, 2)]]
Texture2D<float> AmbientOcclusionImage;

[[vk::binding(10, 2)]]
Texture2D<float4> GbufferDImage;

struct SkyLightingPushConstants
{
    uint applyDiffuse;
};
[[vk::push_constant]]
SkyLightingPushConstants PC;

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

    uint2 gbufferA = GbufferAImage[uint2(svPosition.xy)];
    float4 gbufferB = GbufferBImage[uint2(svPosition.xy)];
    float4 gbufferC = GbufferCImage[uint2(svPosition.xy)];
    float4 gbufferD = GbufferDImage[uint2(svPosition.xy)];

    float2 uv = svPosition.xy / uViewData.resolution.xy;

    float4 fragPosCS = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, sceneDepth, 1);
    float4 fragPosVS = mul(fragPosCS, uViewData.clipToView);
    fragPosVS *= 1.0 / fragPosVS.w;
    float3 viewDirVS = normalize(-fragPosVS.xyz);
    float3 viewDirWS = mul(uViewData.worldToViewRot, viewDirVS);

    ShadingParams sp = decodeShadingParams(gbufferA, gbufferB, gbufferC, gbufferD, viewDirWS);
    if (sp.shadingModel == ShadingModel::Unlit)
    {
        return float4(0, 0, 0, 1);
    }

    // 材质烘焙 AO(gbufferC.w, 已经在 sp.occlusion 里)x 屏幕空间 AO, 两者相乘而不是互相覆盖——
    // 分别捕捉的是静态网格自遮蔽和动态场景遮蔽两种信息。AmbientOcclusionImage 用 sampler 按 UV
    // 采样, 不是按像素坐标直接 Load——AO 关闭时 IndirectLightingPass.cpp 会把这个槽位绑成
    // DefaultResources 的 1x1 白纹理(参照 BloomTexture 关闭时的同一套回退方式), 按绝对像素
    // 坐标 Load 会越界(1x1 纹理在全分辨率像素坐标下几乎处处越界, Vulkan 下是未定义行为), 按
    // UV 采样配合 clamp 采样器则始终落在这张纹理仅有的那个像素上, 不管实际分辨率是多少都能
    // 正确取到白色。
    float ssao = AmbientOcclusionImage.SampleLevel(LinearSampler, uv, 0.0).r;
    float occlusion = sp.occlusion * ssao;

    float4 packedSH[7];
    [unroll]
    for (int i = 0; i < 7; ++i)
    {
        packedSH[i] = SkyLightData[i];
    }

    float3 diffuseIBL, specularIBL;
    evaluateIBL(sp, packedSH, DfgLut, LutSampler, SkyLightSpecularPrefilter, LinearSampler, diffuseIBL, specularIBL);

    float3 radianceScale = SkyLightData[kSkyLightRadianceScaleIndex].xyz;
    diffuseIBL *= radianceScale;
    specularIBL *= radianceScale;

    float NoV = saturate(dot(sp.normalWS, viewDirWS));
    float specularOcclusion = saturate(
        pow(NoV + occlusion, sp.roughness * sp.roughness) - 1.0 + occlusion);
    float3 color = (PC.applyDiffuse != 0 ? diffuseIBL * occlusion : float3(0.0)) + specularIBL * specularOcclusion;

    return float4(color * getPreExposure(uViewData), 1);
}
