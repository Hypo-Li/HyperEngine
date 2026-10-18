// 对应 UE DiffuseIndirectComposite.usf 的 Screen Probe Gather 路径(DiffuseIndirectComposite.usf:560-660), 见 Document/Lumen_ScreenProbeGather_Plan.md §6.7。
#include "../Common/Binding.slang"
#include "../Common/MaterialIO.slang"
#include "../Material/ShadingParams.slang"
#include "../Material/BxDF.slang"
#include "../Reflection/ReflectionCommon.slang"
#include "ScreenProbeShortRangeAOCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(1, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(3, 2)]]
Texture2D<float4> GbufferDImage;

[[vk::binding(4, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(5, 2)]]
Texture2D<float4> DiffuseIndirect;

[[vk::binding(6, 2)]]
Texture2D<uint> ShortRangeAOTexture;

[[vk::binding(7, 2)]]
Texture2D<float4> RoughSpecularIndirect;

[[vk::binding(8, 2)]]
Texture2D<float4> SpecularIndirect;

[[vk::binding(9, 2)]]
Texture2D DfgLut;

[[vk::binding(10, 2)]]
SamplerState LutSampler;

struct ScreenProbeDiffuseIndirectCompositePushConstants
{
    uint2 viewRectMin;
};
[[vk::push_constant]]
ScreenProbeDiffuseIndirectCompositePushConstants PC;

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
    uint2 screenCoord = uint2(svPosition.xy);
    float sceneDepth = SceneDepthImage[screenCoord].r;
    if (sceneDepth == 0)
        return float4(0, 0, 0, 1);

    float2 uv = svPosition.xy / uViewData.resolution.xy;
    float4 fragPosCS = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, sceneDepth, 1);
    float4 fragPosVS = mul(fragPosCS, uViewData.clipToView);
    fragPosVS *= 1.0 / fragPosVS.w;
    float3 viewDirWS = mul(uViewData.worldToViewRot, normalize(-fragPosVS.xyz));

    uint2 gbufferA = GbufferAImage[screenCoord];
    float4 gbufferB = GbufferBImage[screenCoord];
    float4 gbufferC = GbufferCImage[screenCoord];
    float4 gbufferD = GbufferDImage[screenCoord];
    ShadingParams sp = decodeShadingParams(gbufferA, gbufferB, gbufferC, gbufferD, viewDirWS);
    if (sp.shadingModel == ShadingModel::Unlit)
        return float4(0, 0, 0, 1);

    bool isClearCoat = sp.shadingModel == ShadingModel::Clear_Coat;
    // UE 全程只用一个 WorldNormal(=清漆顶层)算 NoV, 我们反过来存, clearCoatNormalWS 才是那个角色;
    // 非清漆像素 clearCoatNormalWS == normalWS, 这行对两种情况都成立
    float noV = saturate(dot(sp.clearCoatNormalWS, viewDirWS));

    float3 diffuseColor = sp.albedo;
    if (sp.shadingModel == ShadingModel::Cloth)
        diffuseColor += sp.fuzzColor * sp.clothAmount;

    float3 specularColor = sp.F0;
    if (isClearCoat)
    {
        remapClearCoatDiffuseAndSpecularColor(
            gbufferC.xyz, gbufferB.x, gbufferB.z, sp.roughness, sp.clearCoat, noV, diffuseColor, specularColor);
    }

    uint2 pixel = screenCoord - PC.viewRectMin;
    float3 diffuseIndirect = DiffuseIndirect[pixel].rgb;
    float3 bentNormal = unpackScreenBentNormal(ShortRangeAOTexture[pixel]);
    float ao = saturate(length(bentNormal));
    float3 diffuseOcclusion = AOMultiBounce(min(sp.albedo, kLumenMaxAOMultibounceAlbedo), ao);
    float3 diffuseLighting = diffuseIndirect * diffuseColor * diffuseOcclusion;

    float3 roughSpecular = RoughSpecularIndirect[pixel].rgb * calculateSpecularOcclusion(sp.clearCoatNormalWS, sp.roughness, ao, viewDirWS, bentNormal);

    float3 specularLighting;
    if (isClearCoat)
    {
        // UE ClearCoatLayerCombine(DiffuseIndirectComposite.usf:246-259): 底层(粗糙镜面)和顶层
        // (专属反射光线)是独立的第三条合成路径, 不走下面非清漆分支的 alpha 混合
        float2 dfgBottom = DfgLut.SampleLevel(LutSampler, float2(noV, sp.roughness), 0.0).rg;
        float3 bottomLayer = roughSpecular * clearCoatBottomLayerEnvBRDF(specularColor, dfgBottom, sp.clearCoat);

        float2 dfgTop = DfgLut.SampleLevel(LutSampler, float2(noV, sp.clearCoatRoughness), 0.0).rg;
        float topBlend = envBRDFFadingF90(float3(0.04), dfgTop).x * sp.clearCoat;

        float3 topLayer = SpecularIndirect[screenCoord].rgb;
        specularLighting = lerp(bottomLayer, topLayer, topBlend);
    }
    else
    {
        float specularAlpha = reflectionCombineAlpha(sp.roughness);
        specularLighting = roughSpecular * (1.0 - specularAlpha);
        if (specularAlpha > 0.0)
            specularLighting += SpecularIndirect[screenCoord].rgb * specularAlpha;

        float2 dfg = DfgLut.SampleLevel(LutSampler, float2(noV, sp.roughness), 0.0).rg;
        specularLighting *= envBRDFFadingF90(specularColor, dfg);
    }

    return float4(diffuseLighting + specularLighting, 1);
}
