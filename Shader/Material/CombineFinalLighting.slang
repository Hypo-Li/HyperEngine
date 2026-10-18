// Surface Cache 最终光照合成——按 CombineFinalLightingPass 已经收集好的每一页, 逐 texel 合成
// FinalLighting = max(0, (DirectLighting + IndirectLighting)·Albedo/π + Emissive), 跟 UE
// CombineFinalLighting()/LumenSurfaceCacheSampling.ush 的公式一致。M3 起 IndirectLighting
// 由 RadiosityPass 填充——GDF 命中改采这张图之后, 现有 DirectLighting 可视化模式画面在
// M1(Indirect 恒为 0)时应逐像素不变(公式等价), 这是 Document/Lumen_Radiosity_Plan.md M1
// 的验收标准。
#include "../Common/Binding.slang"
#include "../Common/FloatQuantization.slang"
#include "../Common/MeshShapeCard.slang"

[[vk::binding(0, 2)]]
Texture2D<float4> CardAlbedoAtlas;

[[vk::binding(1, 2)]]
Texture2D<float4> CardEmissiveAtlas;

[[vk::binding(2, 2)]]
Texture2D<float4> DirectLightingAtlas;

[[vk::binding(3, 2)]]
Texture2D<float4> IndirectLightingAtlas;

// 跟 hyp::SurfaceCacheAtlas::mFinalLightingAtlas 的 B10G11R11_UFLOAT_PACK32 对应
[[vk::binding(4, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> FinalLightingAtlas;

// 跟 CombineFinalLightingPass.cpp 里的 CombineFinalLightingConstants 逐字段对应
struct CombineFinalLightingConstants
{
    uint packedAddress; // mCardPageTable 打包值, 用 decodeCardPageAddress() 解出像素原点+实际尺寸
};
[[vk::push_constant]]
CombineFinalLightingConstants PC;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    CardPageRegion pageRegion = decodeCardPageAddress(PC.packedAddress);

    uint2 pixelInPage = groupId.xy * 8 + localId.xy;
    if (pixelInPage.x >= uint(pageRegion.pixelExtent.x) || pixelInPage.y >= uint(pageRegion.pixelExtent.y))
        return;

    int2 atlasTexelCoord = pageRegion.pixelOrigin + int2(pixelInPage);

    float3 albedo = CardAlbedoAtlas[atlasTexelCoord].rgb;
    float3 emissive = CardEmissiveAtlas[atlasTexelCoord].rgb;
    float3 directLighting = DirectLightingAtlas[atlasTexelCoord].rgb;
    float3 indirectLighting = IndirectLightingAtlas[atlasTexelCoord].rgb;

    float3 finalLighting = max((directLighting + indirectLighting) * albedo * (1.0 / PI) + emissive, 0.0);
    FinalLightingAtlas[atlasTexelCoord] = float4(
        quantizeForFloatRenderTarget(finalLighting, int3(atlasTexelCoord, int(uFrameData.frameIndex % 8u + 2u))), 1.0);
}
