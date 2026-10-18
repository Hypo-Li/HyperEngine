// Radiosity [A] Trace —— Surface Cache 间接光 trace pass: 每个探针沿半球方向打一条 GDF 射线,
// 命中处采 FinalLightingAtlas(上一帧合成结果, 多次反弹的来源), 未命中处采天光, 写进
// TraceRadianceAtlas。每页 128×128 dispatch = 32×32 探针 × 4×4 光线, pixelInPage = probe*4+rayTexel,
// 跟 card 图集/CardDirectLighting 同一个页粒度。跟 UE LumenRadiosityDistanceFieldTracingCS
// (LumenRadiosity.usf)对照, 见 Document/Lumen_Radiosity_Plan.md §3[A]。
#include "../Common/Binding.slang"
#include "../Common/CardTexelDecode.slang"
#include "../Common/MonteCarlo.slang"
#include "../Common/RadiosityProbeSampling.slang"
#include "../Common/SkyLightData.slang"
#include "../GlobalDistanceField/GlobalDistanceFieldQuery.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

[[vk::binding(0, 2)]]
StructuredBuffer<uint> MeshShapeData;

[[vk::binding(1, 2)]]
StructuredBuffer<MeshInstanceData> MeshInstanceDataBuffer;

[[vk::binding(2, 2)]]
StructuredBuffer<uint> GdfIndirection;

[[vk::binding(3, 2)]]
Texture3D<float> GdfAtlas;

[[vk::binding(4, 2)]]
SamplerState LinearSampler;

[[vk::binding(5, 2)]]
Texture2D<float4> CardNormalAtlas;

[[vk::binding(6, 2)]]
Texture2D<float> CardDepthAtlas;

[[vk::binding(7, 2)]]
StructuredBuffer<uint> ObjectGrid;

[[vk::binding(8, 2)]]
StructuredBuffer<uint> CardPages;

[[vk::binding(9, 2)]]
Texture2D<float4> CardFinalLightingAtlas;

[[vk::binding(10, 2)]]
Texture2D<float4> CardAlbedoAtlas;

[[vk::binding(11, 2)]]
StructuredBuffer<uint> SceneConstants;

// mip 0 = 接近零粗糙度的 GGX 预滤波, 近似原始天空辐射度, 按射线方向直接采即可得到方向性
// L(dir)——不用 SkyLightData 里的 SH(那是无 /π 的漫反射辐照度, 不是辐射度), 见
// Document/Lumen_Radiosity_Plan.md §6.1。场景没有天光时绑 DefaultResources 的黑 cube
[[vk::binding(12, 2)]]
TextureCube<float4> SkyLightPrefilter;

[[vk::binding(13, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> TraceRadianceAtlas;

// 这条路径只关心 resolveGlobalDistanceFieldHit() 混合出来的 cardLighting(FinalLighting),
// 不需要这三张的采样结果——但它们是那个函数现在的必传参数(命中着色几个模式都要用),
// 不是这个 pass 自己需要采的数据
[[vk::binding(14, 2)]]
Texture2D<float4> CardEmissiveAtlas;

[[vk::binding(15, 2)]]
Texture2D<float4> CardDirectLightingAtlas;

[[vk::binding(16, 2)]]
Texture2D<float4> CardIndirectLightingAtlas;

[[vk::binding(17, 2)]]
Texture2D<float4> BlueNoiseVec2Texture;

[[vk::binding(18, 2)]]
StructuredBuffer<float4> SkyLightData;

// 跟 CardDirectLightingConstants(CardDirectLightingPass.cpp)同一套页身份字段, 少了
// sourceAtlasBrickGridExtent(不追 per-mesh MDF)、多了 gdfAtlasBrickGridExtent
struct RadiosityTraceConstants
{
    float cardCenterX, cardCenterY, cardCenterZ;
    float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ;
    uint directionIndex;
    uint packedAddress; // mCardPageTable 打包值, 用 decodeCardPageAddress() 解出像素原点+实际尺寸
    uint pageIndexX, pageIndexY;
    uint pageCountX, pageCountY;
    uint instanceIndex;
    int gdfAtlasBrickGridExtent;
    uint temporalIndex; // 这张卡 Radiosity 被更新的次数, 决定探针位置和光线方向的抖动
};
[[vk::push_constant]]
RadiosityTraceConstants PC;

WorldCoord loadReferenceOrigin()
{
    WorldCoord origin;
    origin.tile = int3(asint(SceneConstants[0]), asint(SceneConstants[1]), asint(SceneConstants[2]));
    origin.offset = asfloat(uint3(SceneConstants[3], SceneConstants[4], SceneConstants[5]));
    return origin;
}

// SurfaceBias: r.LumenScene.Radiosity.DistanceFieldSurfaceSlopeBias 默认值(5cm)
static const float kSurfaceBias = 0.05;
// MinTraceDistance: DistanceFieldSurfaceBias 默认值(10cm)
static const float kMinTraceDistance = 0.10;
// MaxTraceDistance: 跟 GDF 4 级 cascade 最外层半径一致(已确认, 见 Plan §6.2)
static const float kMaxTraceDistance = 200.0;
// GLumenRadiosityMaxRayIntensity
static const float kMaxRayIntensity = 40.0;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID)
{
    CardPageRegion pageRegion = decodeCardPageAddress(PC.packedAddress);

    uint2 pixelInPage = groupId.xy * 8 + localId.xy;
    if (pixelInPage.x >= uint(pageRegion.pixelExtent.x) || pixelInPage.y >= uint(pageRegion.pixelExtent.y))
        return;

    int2 atlasTexelCoord = pageRegion.pixelOrigin + int2(pixelInPage);
    uint2 probeCoord = pixelInPage / 4;
    uint2 rayTexel = pixelInPage % 4;

    uint temporalIndex = PC.temporalIndex;
    uint2 probeJitter = getRadiosityProbeJitter(temporalIndex);
    uint2 probeTexel = probeCoord * 4 + probeJitter;

    CardTexelInfo probeInfo = decodeCardTexel(
        probeTexel,
        uint2(PC.pageIndexX, PC.pageIndexY),
        uint2(PC.pageCountX, PC.pageCountY),
        uint2(pageRegion.pixelOrigin),
        uint2(pageRegion.pixelExtent),
        PC.directionIndex,
        float3(PC.cardCenterX, PC.cardCenterY, PC.cardCenterZ),
        float3(PC.cardHalfExtentsX, PC.cardHalfExtentsY, PC.cardHalfExtentsZ),
        PC.instanceIndex,
        CardDepthAtlas,
        CardNormalAtlas);

    if (!probeInfo.valid)
    {
        TraceRadianceAtlas[atlasTexelCoord] = float4(0, 0, 0, 1);
        return;
    }

    uint2 probeGlobalCoord = probeCoord + uint2(pageRegion.pixelOrigin) / 4;
    float2 probeTexelCenter = getRadiosityProbeTexelCenter(BlueNoiseVec2Texture, temporalIndex, probeGlobalCoord);
    float2 probeUV = (float2(rayTexel) + probeTexelCenter) / 4.0;

    // RADIOSITY_PROBE_MAPPING = PROBE_HEMISPHERE_UNIFORM(UE 默认), PDF = 1/(2π)
    float4 hemisphereSample = uniformSampleHemisphere(probeUV);
    float3 localRayDir = hemisphereSample.xyz;

    float3x3 tangentBasis = getTangentBasisFrisvad(probeInfo.normalWS);
    float3 worldRayDir = mul(localRayDir, tangentBasis);

    WorldCoord rayOrigin = probeInfo.worldPos;
    rayOrigin.offset += probeInfo.normalWS * kSurfaceBias;
    rayOrigin.offset += worldRayDir * kSurfaceBias;

    WorldCoord referenceOrigin = loadReferenceOrigin();
    GlobalDistanceFieldCascadeInfo cascades[kGlobalDistanceFieldCascadeCount];
    for (uint i = 0; i < uint(kGlobalDistanceFieldCascadeCount); ++i)
        cascades[i] = globalDistanceFieldCascadeInfoAtIndex(referenceOrigin, i);

    GlobalDistanceFieldTraceInput traceInput = setupGlobalDistanceFieldTraceInput(
        rayOrigin, worldRayDir, kMinTraceDistance, kMaxTraceDistance, 1.0, 1.0);
    GlobalDistanceFieldTraceResult traceResult = traceGlobalDistanceField(
        cascades, GdfIndirection, GdfAtlas, LinearSampler, PC.gdfAtlasBrickGridExtent, traceInput);

    float3 radiance;
    if (traceResult.hit)
    {
        WorldCoord hitPos = rayOrigin;
        hitPos.offset += worldRayDir * traceResult.hitDistance;

        GlobalDistanceFieldResolveResult resolveResult = resolveGlobalDistanceFieldHit(
            cascades[traceResult.hitCascadeIndex], traceResult.hitCascadeIndex, hitPos,
            GdfIndirection, GdfAtlas, PC.gdfAtlasBrickGridExtent, worldRayDir,
            ObjectGrid, MeshInstanceDataBuffer, MeshShapeData, LinearSampler,
            CardPages, CardFinalLightingAtlas, CardAlbedoAtlas, CardNormalAtlas,
            CardEmissiveAtlas, CardDirectLightingAtlas, CardIndirectLightingAtlas, CardDepthAtlas);

        radiance = resolveResult.cardHit ? resolveResult.cardLighting : float3(0.0);

        // 防自激: 近距离命中大概率是打回探针自身附近, 不压掉会在反弹闭环里正反馈发散,
        // 对照 UE EvaluateGlobalDistanceFieldHit() 的 RadianceFactor
        float voxelExtent = globalDistanceFieldVoxelExtent(cascades[traceResult.hitCascadeIndex]);
        float radianceFactor = smoothstep(1.5 * voxelExtent, 2.0 * voxelExtent, traceResult.hitDistance);
        radiance *= radianceFactor;
    }
    else
    {
        radiance = SkyLightPrefilter.SampleLevel(LinearSampler, worldRayDir, 0.0).rgb
            * SkyLightData[kSkyLightRadianceScaleIndex].xyz;
    }

    float peak = max(radiance.r, max(radiance.g, radiance.b));
    if (peak > kMaxRayIntensity)
        radiance *= kMaxRayIntensity / peak;

    TraceRadianceAtlas[atlasTexelCoord] = float4(radiance, 1.0);
}
