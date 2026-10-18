// GDF/MDF 可视化 pass 用的 fullscreen shader: 沿每个像素的相机射线找命中点(GDF 粗测距或
// MDF 精确求交, 由 PC.useMeshDistanceField 这个跟可视化模式正交的开关选择, 见
// hyp::SceneVisualizeMode 的注释), 反查表面归属物体+Surface Cache 数据, 按 PC.visualizeMode
// 画出这次命中的某一项。见 Source/Runtime/Renderer/RenderPass/GlobalDistanceFieldVisualizerPass.cpp
#include "../Common/Binding.slang"
#include "GlobalDistanceFieldQuery.slang"
#include "../PostProcess/ColorGradingCommon.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

[[vk::binding(0, 2)]]
StructuredBuffer<uint> GdfIndirection;

[[vk::binding(1, 2)]]
Texture3D<float> GdfAtlas;

[[vk::binding(2, 2)]]
SamplerState LinearSampler;

[[vk::binding(3, 2)]]
StructuredBuffer<uint> MeshShapeData;

[[vk::binding(4, 2)]]
StructuredBuffer<uint> ObjectGrid;

[[vk::binding(5, 2)]]
StructuredBuffer<MeshInstanceData> MeshInstanceDataBuffer;

[[vk::binding(6, 2)]]
StructuredBuffer<uint> CardPages;

// FinalLighting 已经是 albedo/PI * direct + emissive 的合成结果(CombineFinalLightingPass),
// 命中查询不再需要单独采 direct/emissive 现场重新组合, 见 GlobalDistanceFieldQuery.slang
[[vk::binding(7, 2)]]
Texture2D<float4> CardFinalLightingAtlas;

[[vk::binding(8, 2)]]
Texture2D<float4> CardAlbedoAtlas;

[[vk::binding(9, 2)]]
Texture2D<float4> CardEmissiveAtlas;

[[vk::binding(10, 2)]]
Texture2D<float> CardDepthAtlas;

[[vk::binding(11, 2)]]
Texture2D<float4> CardNormalAtlas;

[[vk::binding(12, 2)]]
Texture2D<float4> CardIndirectLightingAtlas;

// 只是 CardDirectLightingPass 写的纯 Direct 项(不含 Indirect、不乘 albedo/π)——命中着色的
// DirectLighting 模式和下面的 AtlasDirectLighting 原始图集模式都采这张
[[vk::binding(13, 2)]]
Texture2D<float4> CardDirectLightingAtlas;

// per-mesh MDF 源数据(RenderBackend::getMeshDistanceFieldBrickAtlas()), 只有
// PC.useMeshDistanceField 为真时才会被采样, 见 resolveMdfHit()
[[vk::binding(14, 2)]]
Texture3D<float> MeshDistanceFieldBrickAtlas;

struct GlobalDistanceFieldVisualizerPushConstants
{
    int gdfAtlasBrickGridExtent;
    int mdfAtlasBrickGridExtent;
    uint visualizeMode;
    uint useMeshDistanceField;
};
[[vk::push_constant]]
GlobalDistanceFieldVisualizerPushConstants PC;

// 跟 hyp::SceneVisualizeMode(Renderer/RendererDefines.h)的 1-14 这几项枚举值必须逐项一致
static const uint kLumenVisualizeModeGeometryNormal = 1;
static const uint kLumenVisualizeModeAlbedo = 2;
static const uint kLumenVisualizeModeNormal = 3;
static const uint kLumenVisualizeModeEmissive = 4;
static const uint kLumenVisualizeModeCardWeights = 5;
// 只是 DirectLightingAtlas 原始采样, 不乘 albedo——跟 FinalLighting 的区别见后者注释
static const uint kLumenVisualizeModeDirectLighting = 6;
static const uint kLumenVisualizeModeIndirectLighting = 7;
// 命中着色返回的是 (Direct+Indirect)·Albedo/π + Emissive, 想单独看不乘 albedo 的纯
// Direct/Indirect 项用上面两个模式
static const uint kLumenVisualizeModeFinalLighting = 8;
// Atlas 六项必须留在枚举末尾: shadeSurfaceCacheAtlas() 用 >= AtlasAlbedo 判断走不走图集路径
static const uint kLumenVisualizeModeAtlasAlbedo = 9;
static const uint kLumenVisualizeModeAtlasNormal = 10;
static const uint kLumenVisualizeModeAtlasDepth = 11;
static const uint kLumenVisualizeModeAtlasDirectLighting = 12;
static const uint kLumenVisualizeModeAtlasIndirectLighting = 13;
static const uint kLumenVisualizeModeAtlasFinalLighting = 14;

struct VertexOutput
{
    float4 svPosition : SV_Position;
};

static const float2 kPositions[3] = { float2(-1, -3), float2(3, 1), float2(-1, 1) };

[shader("vertex")]
VertexOutput VertexMain(in uint vertexID : SV_VertexID)
{
    VertexOutput output;
    output.svPosition = float4(kPositions[vertexID], 0, 1);
    return output;
}

// 4 级 cascade 加起来覆盖的最大跨度, 当 sphere tracing 的硬截止距离用——每一级自己的
// box 相交区间已经把步进限制在这一级实际覆盖的范围内(见 traceGlobalDistanceField),
// 这里只是个不需要精确到个位的兜底上限, 留够 3 倍余量
static const float kMaxTraceDistance =
    kGlobalDistanceFieldInnermostHalfExtent * float(1 << (kGlobalDistanceFieldCascadeCount - 1)) * 3.0;

float3 traceAndShade(float2 pixelPos, GlobalDistanceFieldCascadeInfo cascades[kGlobalDistanceFieldCascadeCount])
{
    const float3 backgroundColor = float3(0.05, 0.05, 0.08);

    float2 uv = pixelPos / uViewData.resolution.xy;
    float4 clipSpace = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, 0.5, 1.0);
    float4 viewSpace = mul(clipSpace, uViewData.clipToViewUnjittered);
    viewSpace *= 1.0 / viewSpace.w;
    float3 rayDirVS = normalize(viewSpace.xyz);
    float3 rayDirWS = normalize(mul(rayDirVS, transpose(uViewData.worldToViewRot)));

    // 逐像素抖动步长, 把命中点的步进量化误差从空间相干的硬边打散成噪声
    float stepFactor = lerp(0.8, 1.0, interleavedGradientNoise(pixelPos, uFrameData.frameIndex % 8));

    GlobalDistanceFieldTraceInput traceInput = setupGlobalDistanceFieldTraceInput(uViewData.cameraOrigin, rayDirWS, 0.0, kMaxTraceDistance, stepFactor, 1.0);
    // 跟 UE LumenVisualize.usf 的 Lumen Scene 可视化设置一致
    traceInput.expandSurfaceUsingRayTimeInsteadOfMaxDistance = false;
    traceInput.initialMaxDistance = 0.0;

    // GDF 的外扩窄带比 MDF 精确表面"胖", 射线可能擦到这圈外扩区触发 GDF 命中、但 MDF 精确
    // 求交在那里根本找不到表面(resolveMdfHit valid=false, 见该函数的注释)——这种情况不能
    // 直接摆背景色, 越过这次假命中继续往后追, 直到真的追到这个像素该画的 MDF 表面, 或者
    // 重试次数耗尽(安全上限, 防止退化场景反复卡在同一段)
    const uint kMaxMdfContinueAttempts = 8;
    GlobalDistanceFieldResolveResult resolveResult;
    resolveResult.valid = false;
    for (uint attempt = 0; attempt <= kMaxMdfContinueAttempts; ++attempt)
    {
        GlobalDistanceFieldTraceResult traceResult = traceGlobalDistanceField(
            cascades, GdfIndirection, GdfAtlas, LinearSampler, PC.gdfAtlasBrickGridExtent, traceInput);

        if (!traceResult.hit)
            return backgroundColor;

        WorldCoord hitPos = uViewData.cameraOrigin;
        hitPos.offset += rayDirWS * traceResult.hitDistance;

        // GDF 粗测距永远先跑一遍(Object Grid 是按 GDF brick 网格索引的, MDF 精确求交也要靠它
        // 找候选)——PC.useMeshDistanceField 只决定命中反查这一步怎么做, 见
        // hyp::SceneVisualizeMode 的注释
        resolveResult = PC.useMeshDistanceField != 0
            ? resolveMdfHit(
                cascades[traceResult.hitCascadeIndex], traceResult.hitCascadeIndex, hitPos,
                GdfIndirection, GdfAtlas, PC.gdfAtlasBrickGridExtent, LinearSampler,
                uViewData.cameraOrigin, rayDirWS, kMaxTraceDistance,
                ObjectGrid, MeshInstanceDataBuffer, MeshShapeData,
                MeshDistanceFieldBrickAtlas, PC.mdfAtlasBrickGridExtent,
                CardPages, CardFinalLightingAtlas, CardAlbedoAtlas, CardNormalAtlas,
                CardEmissiveAtlas, CardDirectLightingAtlas, CardIndirectLightingAtlas, CardDepthAtlas)
            : resolveGlobalDistanceFieldHit(
                cascades[traceResult.hitCascadeIndex], traceResult.hitCascadeIndex, hitPos,
                GdfIndirection, GdfAtlas, PC.gdfAtlasBrickGridExtent, rayDirWS,
                ObjectGrid, MeshInstanceDataBuffer,
                MeshShapeData, LinearSampler,
                CardPages, CardFinalLightingAtlas, CardAlbedoAtlas, CardNormalAtlas,
                CardEmissiveAtlas, CardDirectLightingAtlas, CardIndirectLightingAtlas, CardDepthAtlas);

        if (resolveResult.valid || PC.useMeshDistanceField == 0)
            break;

        // 越过这次假命中所在 cascade 的一段体素宽度再继续追, 避免下一次 trace 立刻在同一
        // 圈外扩窄带里重新触发同一个假命中
        float voxelExtent = globalDistanceFieldVoxelExtent(cascades[traceResult.hitCascadeIndex]);
        traceInput.minTraceDistance = traceResult.hitDistance + 2.0 * voxelExtent;
    }

    if (!resolveResult.valid)
        return backgroundColor;

    // 几何法线是 card 查找的输入而不是结果, 没有任何一张 card 覆盖到也照样有意义
    if (PC.visualizeMode == kLumenVisualizeModeGeometryNormal)
        return resolveResult.normalWS * 0.5 + 0.5;

    if (!resolveResult.cardHit)
        return resolveResult.color;

    switch (PC.visualizeMode)
    {
    // Lumen 分支不经过 ColorGradingPass, 线性颜色量在这里代替它做输出编码
    case kLumenVisualizeModeAlbedo:
        return linearToSRGB(resolveResult.cardAlbedo);
    case kLumenVisualizeModeNormal:
        return resolveResult.cardBlendedNormalWS * 0.5 + 0.5;
    case kLumenVisualizeModeEmissive:
        return linearToSRGB(resolveResult.cardEmissive);
    case kLumenVisualizeModeCardWeights:
        return resolveResult.cardWeightsColor;
    case kLumenVisualizeModeDirectLighting:
        return linearToSRGB(resolveResult.cardDirectLighting);
    case kLumenVisualizeModeIndirectLighting:
        return linearToSRGB(resolveResult.cardIndirectLighting);
    case kLumenVisualizeModeFinalLighting:
    default:
        return linearToSRGB(resolveResult.cardLighting);
    }
}

// 不追踪场景, 直接把整张 SurfaceCacheAtlas 按屏幕高度等比贴出来——采样端反推不出"这个 texel
// 为什么没被画过", 只能直接看图集本身
bool shadeSurfaceCacheAtlas(float2 pixelPos, out float3 outColor)
{
    outColor = float3(0.0);
    if (PC.visualizeMode < kLumenVisualizeModeAtlasAlbedo)
        return false;

    const float kAtlasExtentInTexels = float(kSurfaceCacheAtlasResolution);
    float2 atlasUV = pixelPos / uViewData.resolution.yy;
    if (any(atlasUV > 1.0))
    {
        outColor = float3(0.05, 0.05, 0.08);
        return true;
    }

    int2 texelCoord = int2(atlasUV * kAtlasExtentInTexels);
    float depth = CardDepthAtlas[texelCoord];
    // 页边界画一条格线, 用来数清楚哪几页真的被捕获过
    bool onPageBorder = any(int2(texelCoord % kSurfaceCachePageSize) == int2(0, 0));

    if (depth <= kCardBackgroundDepth)
        outColor = float3(1.0, 0.0, 1.0); // 品红: 这个 texel 没被画过
    else if (PC.visualizeMode == kLumenVisualizeModeAtlasAlbedo)
        outColor = CardAlbedoAtlas[texelCoord].rgb;
    else if (PC.visualizeMode == kLumenVisualizeModeAtlasNormal)
        outColor = CardNormalAtlas[texelCoord].rgb;
    else if (PC.visualizeMode == kLumenVisualizeModeAtlasFinalLighting)
        outColor = CardFinalLightingAtlas[texelCoord].rgb;
    else if (PC.visualizeMode == kLumenVisualizeModeAtlasIndirectLighting)
        outColor = CardIndirectLightingAtlas[texelCoord].rgb;
    else if (PC.visualizeMode == kLumenVisualizeModeAtlasDirectLighting)
        outColor = CardDirectLightingAtlas[texelCoord].rgb;
    else
        outColor = float3(depth);

    if (onPageBorder)
        outColor = float3(0.0, 1.0, 0.0);
    return true;
}

[shader("fragment")]
float4 FragmentMain(in VertexOutput input) : SV_Target
{
    float3 atlasColor;
    if (shadeSurfaceCacheAtlas(input.svPosition.xy, atlasColor))
        return float4(atlasColor, 1.0);

    GlobalDistanceFieldCascadeInfo cascades[kGlobalDistanceFieldCascadeCount];
    for (uint i = 0; i < uint(kGlobalDistanceFieldCascadeCount); ++i)
        cascades[i] = globalDistanceFieldCascadeInfoAtIndex(uViewData.cameraOrigin, i);

    return float4(traceAndShade(input.svPosition.xy, cascades), 1.0);
}
