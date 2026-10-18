// 新捕获页的光照状态重置: Direct/Indirect 清零, Final 取自发光, Radiosity 累积帧计数清零,
// 对应 UE CopyCardCaptureLightingToAtlasPS 在不做历史重采样时写出的值。
// 不需要 sInstanceData/uViewData, 不 include Common/Binding.slang, 跟 CombineFinalLighting.slang 同理。

[[vk::binding(0, 2)]]
Texture2D<float4> CaptureEmissive;

[[vk::binding(1, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> DirectLightingAtlas;

[[vk::binding(2, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> IndirectLightingAtlas;

[[vk::binding(3, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> FinalLightingAtlas;

[[vk::binding(4, 2)]]
[format("r8")]
RWTexture2D<float> RadiosityNumFramesAccumulatedAtlas;

// 跟 CardLightingResetPass.cpp 里的 CardLightingResetConstants 逐字段对应
struct CardLightingResetConstants
{
    uint captureOriginX;
    uint captureOriginY;
    uint atlasOriginX;
    uint atlasOriginY;
    uint extentX;
    uint extentY;
};
[[vk::push_constant]]
CardLightingResetConstants PC;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    uint2 pixelInPage = groupId.xy * 8 + localId.xy;
    if (pixelInPage.x >= PC.extentX || pixelInPage.y >= PC.extentY)
        return;

    uint2 captureCoord = uint2(PC.captureOriginX, PC.captureOriginY) + pixelInPage;
    uint2 atlasCoord = uint2(PC.atlasOriginX, PC.atlasOriginY) + pixelInPage;

    float3 emissive = CaptureEmissive[captureCoord].rgb;

    DirectLightingAtlas[atlasCoord] = float4(0.0, 0.0, 0.0, 1.0);
    IndirectLightingAtlas[atlasCoord] = float4(0.0, 0.0, 0.0, 1.0);
    FinalLightingAtlas[atlasCoord] = float4(max(emissive, 0.0), 1.0);

    // 一个线程组恰好覆盖一个 card tile
    if (localId.x == 0 && localId.y == 0)
        RadiosityNumFramesAccumulatedAtlas[uint2(PC.atlasOriginX, PC.atlasOriginY) / 8 + groupId.xy] = 0.0;
}
