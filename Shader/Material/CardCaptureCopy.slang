// 把临时捕获图集里的一页拷进 SurfaceCacheAtlas 物理图集的四个几何层。
// 不需要 sInstanceData/uViewData, 不 include Common/Binding.slang, 跟 CombineFinalLighting.slang 同理。

[[vk::binding(0, 2)]]
Texture2D<float4> CaptureAlbedo;

[[vk::binding(1, 2)]]
Texture2D<float4> CaptureNormal;

[[vk::binding(2, 2)]]
Texture2D<float4> CaptureEmissive;

[[vk::binding(3, 2)]]
DepthTexture2D CaptureDepth;

[[vk::binding(4, 2)]]
[format("rgba8")]
WTexture2D<float4> AlbedoAtlas;

[[vk::binding(5, 2)]]
[format("rgba8")]
WTexture2D<float4> NormalAtlas;

[[vk::binding(6, 2)]]
[format("rgba16f")]
WTexture2D<float4> EmissiveAtlas;

[[vk::binding(7, 2)]]
[format("r16")]
WTexture2D<float> DepthAtlas;

// 跟 CardCaptureCopyPass.cpp 里的 CardCaptureCopyConstants 逐字段对应
struct CardCaptureCopyConstants
{
    uint captureOriginX;
    uint captureOriginY;
    uint atlasOriginX;
    uint atlasOriginY;
    uint extentX;
    uint extentY;
};
[[vk::push_constant]]
CardCaptureCopyConstants PC;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    uint2 pixelInPage = groupId.xy * 8 + localId.xy;
    if (pixelInPage.x >= PC.extentX || pixelInPage.y >= PC.extentY)
        return;

    uint2 captureCoord = uint2(PC.captureOriginX, PC.captureOriginY) + pixelInPage;
    uint2 atlasCoord = uint2(PC.atlasOriginX, PC.atlasOriginY) + pixelInPage;

    AlbedoAtlas.Store(atlasCoord, CaptureAlbedo[captureCoord]);
    NormalAtlas.Store(atlasCoord, CaptureNormal[captureCoord]);
    EmissiveAtlas.Store(atlasCoord, CaptureEmissive[captureCoord]);
    DepthAtlas.Store(atlasCoord, CaptureDepth[captureCoord]);
}
