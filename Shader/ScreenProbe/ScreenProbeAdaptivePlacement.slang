// 自适应探针分配: 每个统一网格 tile 内撒 kScreenProbeNumAdaptiveSamplesPerTile 个候选子采样点,
// 对每个候选点先假装只用统一探针网格插值, 插值权重总和不够(< kMinProbeInterpolationWeight)
// 就说明周围统一探针跟这个点合不来(通常是深度/法线突变边缘), 单独给它分配一个探针。全局预算
// 封顶(kScreenProbeAdaptiveProbeAllocationFraction * 统一探针数), 不是按 tile 分配。跟 UE
// ScreenProbeAdaptivePlacementMarkCS(LumenScreenProbeGather.usf:1731-1806)对应, 见
// Shader/ScreenProbe/ScreenProbeCommon.slang 顶部的偏差说明; 这里把 UE 拆开的 mark+间接
// dispatch 压缩成单个 pass 直接分配, 不追求同一套 GPU 端两阶段压缩优化
#include "../Common/Binding.slang"
#include "../Common/BindingAttributes.slang"
#include "../Common/GBufferEncoding.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(1, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(2, 2)]]
DepthTexture2D SceneDepthImage;

// 跟统一探针(ScreenProbeDownsampleUniform.slang)共用同一张图集, 不是两张独立纹理——跟 UE
// ScreenProbeAtlasBufferSize 一张图集装下统一+自适应两种探针同一个做法。读: 邻近四角的统一
// 探针深度, 算插值权重; 写: 新分配的自适应探针自己的数据, 写在 atlasCoord >= 统一探针数量
// 那一段(见 C++ 侧 ScreenProbePlacementPass::getAtlasViewSize), 跟统一探针互不覆盖
[[vk::binding(3, 2)]]
[format("r32f")]
RWTexture2D<float> ScreenProbeSceneDepth;

[[vk::binding(4, 2)]]
[format("rg8")]
RWTexture2D<float2> ScreenProbeWorldNormal;

[[vk::binding(5, 2)]]
RWStructuredBuffer<uint> AdaptiveScreenProbePosAndTileIndices;

// InterlockedAdd 的三参数重载在 WGSL compute stage 编译不过
[[vk::binding(6, 2)]]
RWStructuredBuffer<Atomic<uint>> AdaptiveScreenProbeTotalAndTileCounts;

[[vk::binding(7, 2)]]
[UnfilterableFloat]
Texture2D<float4> VelocityImage;

[[vk::binding(8, 2)]]
[format("r16f")]
RWTexture2D<float> ScreenProbeWorldSpeed;

struct ScreenProbeAdaptivePlacementPushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    uint2 tileGridSize;
    uint2 atlasViewSize;
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
};
[[vk::push_constant]]
ScreenProbeAdaptivePlacementPushConstants PC;

// numthreads 要求字面量(WGSL 后端对 [numthreads(...)] 里的具名常量表达式不支持 const 折叠),
// 跟 ScreenProbeCommon.slang::kScreenProbeNumAdaptiveSamplesPerTile 手动保持一致
[shader("compute")]
[numthreads(8, 1, 1)]
void ComputeMain(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    uint2 tileCoord = groupID.xy;
    if (any(tileCoord >= PC.tileGridSize))
        return;

    uint sampleIndex = groupThreadID.x;
    uint2 screenPosition = clamp(
        getAdaptiveSampleScreenPosition(tileCoord, sampleIndex, kScreenProbeNumAdaptiveSamplesPerTile, PC.viewRectMin, screenProbeFrameIndex()),
        PC.viewRectMin,
        PC.viewRectMin + PC.viewExtent - 1u);

    float deviceZ = SceneDepthImage[screenPosition].r;
    if (deviceZ == 0.0)
        return;

    uint2 gbufferA = GbufferAImage[screenPosition];
    float4 gbufferB = GbufferBImage[screenPosition];
    ShadingModel shadingModel;
    bool hasAnisotropy;
    unpackShadingModelByte(gbufferB.w, shadingModel, hasAnisotropy);
    if (shadingModel == ShadingModel::Unlit)
        return;

    float2 uv = (float2(screenPosition) + 0.5) * uViewData.resolution.zw;
    float sampleDepth = screenProbeLinearDepth(deviceZ, uViewData);
    float3 sampleViewPosition = screenProbeViewPositionAtDepth(uv, sampleDepth, uViewData);
    float3 normalWS = decodeGBufferNormal(gbufferA.r);
    float3 sampleViewNormal = mul(normalWS, uViewData.worldToViewRot);

    UniformProbeCorners corners = getUniformProbeCorners(
        float2(screenPosition), float2(0.0, 0.0), PC.viewRectMin, PC.viewExtent, PC.tileGridSize, screenProbeFrameIndex());
    float4 cornerProbeDepths;
    for (uint cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
        cornerProbeDepths[cornerIndex] = ScreenProbeSceneDepth[getUniformProbeCornerTileCoord(corners, cornerIndex)];

    ScreenProbeUpsampleWeights uniformWeights = calculateUniformUpsampleWeights(
        corners, cornerProbeDepths, PC.viewRectMin, screenProbeFrameIndex(),
        sampleViewPosition, sampleViewNormal, sampleDepth, uViewData);
    if (dot(uniformWeights.weights, float4(1.0)) >= kMinProbeInterpolationWeight)
        return;

    uint newAdaptiveIndex = AdaptiveScreenProbeTotalAndTileCounts[0].add(1u);
    // 预算用完就放弃这个候选点, 不回退计数器——跟 UE 一样, 超出预算的候选就是没有探针可用,
    // 会在 M5-M6 的插值阶段用 fallbackWeights 兜底
    if (newAdaptiveIndex >= PC.maxNumAdaptiveProbes)
        return;

    uint globalProbeIndex = PC.numUniformProbes + newAdaptiveIndex;
    uint2 atlasCoord = screenProbeAtlasCoordFromIndex(globalProbeIndex, PC.atlasViewSize);

    ScreenProbeSceneDepth[atlasCoord] = sampleDepth;
    ScreenProbeWorldNormal[atlasCoord] = octEncode(normalWS);
    ScreenProbeWorldSpeed[atlasCoord] = calculateWorldSpaceSpeed(sampleViewPosition, VelocityImage[screenPosition], uViewData);
    AdaptiveScreenProbePosAndTileIndices[newAdaptiveIndex] = packScreenPosition(screenPosition);

    uint tileLinearIndex = tileCoord.y * PC.tileGridSize.x + tileCoord.x;
    uint slotInTile = AdaptiveScreenProbeTotalAndTileCounts[getTileAdaptiveProbeCountOffset(tileLinearIndex)].add(1u);
    if (slotInTile < kScreenProbeNumAdaptiveSamplesPerTile)
        AdaptiveScreenProbePosAndTileIndices[getTileAdaptiveProbeIndexOffset(PC.maxNumAdaptiveProbes, tileLinearIndex, slotInTile)] = globalProbeIndex;
}
