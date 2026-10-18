// 对应 UE ScreenProbeTraceScreenTexturesCS 和 ScreenProbeTraceVoxelsCS(LumenScreenProbeTracing.usf:56-366、712-872),
// UE 每一级拆成独立 pass 加 compaction, 这里单 pass 逐光线走完整条链, 见 Document/Lumen_ScreenProbeGather_Plan.md §6.1。
// M7.4: 追踪方向经 GenerateRays(M7.3)的重定向表间接决定, 不再直接用原生纹素坐标, 见下面
// unpackRayInfo 调用点——不做 UE 的 ConeHalfAngle(锥追踪半径), 这个引擎的 GDF/体素追踪本来就
// 没有锥半径参数(见计划文档 M3b"resolveGlobalDistanceFieldHit 没有 UE 的锥半径参数"那条偏差)。
// M3c: GDF 之前多一级 Mesh SDF 近场(traceMeshSDFNearField(), GlobalDistanceFieldQuery.slang),
// GDF 的起点从 kMinTraceDistance 改成 kTraceMeshSDFsDistance——近场这一段改由精确 per-mesh
// SDF 覆盖, 不再靠 GDF 体素化兜底。
#include "../Common/Binding.slang"
#include "../Common/BindingAttributes.slang"
#include "../Common/HZBTrace.slang"
#include "../Common/SkyLightData.slang"
#include "../GlobalDistanceField/GlobalDistanceFieldQuery.slang"
#include "ScreenProbeCommon.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

[[vk::binding(0, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

[[vk::binding(1, 2)]]
Texture2D<float2> ScreenProbeWorldNormal;

[[vk::binding(2, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbePosAndTileIndices;

[[vk::binding(3, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbeTotalAndTileCounts;

[[vk::binding(4, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(5, 2)]]
Texture2D<float2> SceneHZBImage;

[[vk::binding(6, 2)]]
[UnfilterableFloat]
Texture2D<float4> VelocityImage;

[[vk::binding(7, 2)]]
Texture2D<float4> TAAHistoryColor;

[[vk::binding(8, 2)]]
Texture2D<float> SceneDepthHistory;

// mip 0 是接近零粗糙度的 GGX 预滤波, 近似原始天空辐射度
[[vk::binding(9, 2)]]
TextureCube<float4> SkyLightPrefilter;

[[vk::binding(10, 2)]]
Texture2D<float4> BlueNoiseVec2Texture;

[[vk::binding(11, 2)]]
SamplerState PointClampSampler;

[[vk::binding(12, 2)]]
SamplerState LinearSampler;

[[vk::binding(13, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> TraceRadiance;

[[vk::binding(14, 2)]]
[format("r32ui")]
RWTexture2D<uint> TraceHit;

[[vk::binding(15, 2)]]
StructuredBuffer<uint> MeshShapeData;

[[vk::binding(16, 2)]]
StructuredBuffer<MeshInstanceData> MeshInstanceDataBuffer;

[[vk::binding(17, 2)]]
StructuredBuffer<uint> GdfIndirection;

[[vk::binding(18, 2)]]
Texture3D<float> GdfAtlas;

[[vk::binding(19, 2)]]
Texture2D<float4> CardNormalAtlas;

[[vk::binding(20, 2)]]
Texture2D<float> CardDepthAtlas;

[[vk::binding(21, 2)]]
StructuredBuffer<uint> ObjectGrid;

[[vk::binding(22, 2)]]
StructuredBuffer<uint> CardPages;

[[vk::binding(23, 2)]]
Texture2D<float4> CardFinalLightingAtlas;

[[vk::binding(24, 2)]]
Texture2D<float4> CardAlbedoAtlas;

[[vk::binding(25, 2)]]
Texture2D<float4> CardEmissiveAtlas;

[[vk::binding(26, 2)]]
Texture2D<float4> CardDirectLightingAtlas;

[[vk::binding(27, 2)]]
Texture2D<float4> CardIndirectLightingAtlas;

[[vk::binding(28, 2)]]
Texture2D<float> ScreenProbeWorldSpeed;

// M7.4: GenerateRays(M7.3)产出的重定向表, 见 unpackRayInfo 的调用点
[[vk::binding(29, 2)]]
Texture2D<uint> ScreenProbeRayInfos;

// M3c: per-mesh MDF 源数据, 跟 GlobalDistanceFieldVisualizer.slang 的 MeshDistanceFieldBrickAtlas
// 同一份引擎全局资源(RenderBackend::getMeshDistanceFieldBrickAtlas())
[[vk::binding(30, 2)]]
Texture3D<float> MeshDistanceFieldBrickAtlas;

[[vk::binding(31, 2)]]
StructuredBuffer<float4> SkyLightData;

// screenTraceEnabled/depthHistoryValid/gdfTraceEnabled 压进 traceFlags 的低 3 位——
// push constant 总大小卡在 rhi::kImmediateDataSize(64 字节硬上限), 新增 mdfAtlasBrickGridExtent
// 之后三个独立 uint 布尔字段挤不下, 跟 CardDirectLighting.slang/MeshSDFScene.slang 同一个
// "手动按标量摆放、把布尔位打包"的既有约定
struct ScreenProbeTracePushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    float2 hzbUvFactor;
    float2 hzbBaseTexelSize;
    uint atlasWidth;
    uint numUniformProbes;
    uint maxNumAdaptiveProbes;
    uint traceFlags;
    float maxWorldTraceDistance;
    int gdfAtlasBrickGridExtent;
    int mdfAtlasBrickGridExtent;
};
[[vk::push_constant]]
ScreenProbeTracePushConstants PC;

static const uint kScreenProbeTraceFlagScreenTraceEnabled = 1u << 0;
static const uint kScreenProbeTraceFlagDepthHistoryValid = 1u << 1;
static const uint kScreenProbeTraceFlagGdfTraceEnabled = 1u << 2;

// UE r.Lumen.ScreenProbeGather.ScreenTraces.HZBTraversal.HistoryDepthTestRelativeThickness 默认值
static const float kHistoryDepthTestRelativeThickness = 0.005;

// UE GNearClippingPlane 默认值 10cm(CoreGlobals.cpp:252), 换算成米; 历史深度测试的阈值是绝对 deviceZ,
// 而 deviceZ = 近平面 / 深度, 要按我们的近平面跟它的比例缩放才是同一个相对精度
static const float kUENearClippingPlane = 0.1;

// UE 在屏幕追踪未命中时给命中距离加的 2.0(cm), 换算成米
static const float kScreenTraceMissHitDistanceOffset = 0.02;

// UE 探针追踪的 MinTraceDistance = max(SurfaceBias 5cm, r.Lumen.DiffuseMinTraceDistance 0)(LumenDiffuseIndirect.cpp:24-25,355), 换算成米
static const float kMinTraceDistance = 0.05;

// UE r.Lumen.ScreenProbeGather.TraceMeshSDFs.TraceDistance 默认值(180cm), 换算成米——M3c
// 之后 GDF 的起点从 kMinTraceDistance 改成这个值, 见文件顶部注释
static const float kTraceMeshSDFsDistance = 1.8;

// UE ComputeHitVignetteFromScreenPos(SSRTRayCast.ush:674-681)
float computeHitVignette(float2 screenPosition)
{
    float2 vignette = saturate(abs(screenPosition) * 5.0 - 4.0);
    return saturate(1.0 - dot(vignette, vignette));
}

void writeTrace(uint2 traceBufferCoord, float3 radiance, float hitDistance, bool hit, bool moving)
{
    TraceRadiance[traceBufferCoord] = float4(radiance, 1.0);
    TraceHit[traceBufferCoord] = encodeProbeRayDistance(hitDistance, hit, moving);
}

// numthreads 要求字面量, 跟 kScreenProbeTracingOctahedronResolution 手动保持一致
[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    uint2 atlasCoord = groupId.xy;
    uint2 traceTexelCoord = groupThreadId.xy;
    uint probeIndex = atlasCoord.y * PC.atlasWidth + atlasCoord.x;

    uint numProbes = getNumScreenProbes(PC.numUniformProbes, loadNumAdaptiveScreenProbes(AdaptiveScreenProbeTotalAndTileCounts), PC.maxNumAdaptiveProbes);
    if (probeIndex >= numProbes || atlasCoord.x >= PC.atlasWidth)
        return;

    uint2 traceBufferCoord = atlasCoord * kScreenProbeTracingOctahedronResolution + traceTexelCoord;

    float probeDepth = ScreenProbeSceneDepth[atlasCoord];
    if (!(probeDepth > 0.0))
    {
        writeTrace(traceBufferCoord, float3(0.0), 0.0, false, false);
        return;
    }

    uint2 screenPosition = getScreenProbeScreenPosition(
        probeIndex, atlasCoord, PC.numUniformProbes, PC.viewRectMin, PC.viewExtent, screenProbeFrameIndex(), AdaptiveScreenProbePosAndTileIndices);
    uint2 screenTileCoord = getScreenTileCoord(screenPosition, PC.viewRectMin, screenProbeFrameIndex());
    float2 screenUV = (float2(screenPosition) + 0.5) * uViewData.resolution.zw;

    // M7.4: 追踪方向不再直接用原生纹素坐标——先查 GenerateRays(M7.3)产出的重定向表, 大部分槽位
    // 等级还是"未细分"(mipSize=8, 结果跟以前一样), 少数被判定为高 PDF 的方向会被重定向到细分
    // 后的子方向(mipSize=16), 见 ScreenProbeCommon.slang 的 unpackRayInfo 注释
    uint2 rayTexelCoord;
    uint rayLevel;
    unpackRayInfo(ScreenProbeRayInfos[traceBufferCoord], rayTexelCoord, rayLevel);
    uint mipSize = kScreenProbeMaxImportanceSamplingOctahedronResolution >> rayLevel;

    float3 rayDirWS = getScreenProbeTexelRayDirection(
        BlueNoiseVec2Texture,
        screenProbeFrameIndex() % kScreenProbeMaxRayDirections,
        screenTileCoord,
        rayTexelCoord,
        mipSize);

    float3 probeViewPosition = screenProbeViewPositionAtDepth(screenUV, probeDepth, uViewData);
    float noise = interleavedGradientNoise(float2(traceBufferCoord) + 0.5, float(screenProbeFrameIndex() % 8u));

    float screenTraceDistance = 0.0;
    if ((PC.traceFlags & kScreenProbeTraceFlagScreenTraceEnabled) != 0)
    {
        float3 normalWS = octDecode(ScreenProbeWorldNormal[atlasCoord]);
        float3 normalVS = mul(normalWS, uViewData.worldToViewRot);
        float3 rayDirVS = mul(rayDirWS, uViewData.worldToViewRot);

        float deviceZ = SceneDepthImage[screenPosition].r;
        float3 rayOriginVS = applyScreenSpaceRayBias(probeViewPosition, normalVS, screenUV, deviceZ, uViewData);

        float4 rayStartClip = mul(float4(rayOriginVS, 1.0), uViewData.viewToClip);
        bool rayStartOnScreen = all(rayStartClip.xy >= -rayStartClip.w) && all(rayStartClip.xy <= rayStartClip.w);

        if (rayStartOnScreen)
        {
            HZBTraceResult result = traceHZB(
                SceneDepthImage,
                SceneHZBImage,
                PointClampSampler,
                uViewData,
                PC.hzbUvFactor,
                PC.hzbBaseTexelSize,
                rayOriginVS,
                rayDirVS,
                PC.maxWorldTraceDistance,
                kHZBTraceNumThicknessStepsToDetermineCertainty);

            float hitDistanceOffset = (!result.hit && !result.uncertain) ? kScreenTraceMissHitDistanceOffset : 0.0;
            float3 hitViewPosition = screenProbeViewPositionAtDepth(
                result.hitSceneUV, screenProbeLinearDepth(result.rayDeviceZ, uViewData), uViewData);
            float hitDistance = min(length(hitViewPosition - probeViewPosition) + hitDistanceOffset, PC.maxWorldTraceDistance);
            screenTraceDistance = hitDistance;

            bool hit = result.hit;
            if (hit)
            {
                float2 hitScreenPosition = float2(result.hitSceneUV.x * 2.0 - 1.0, (1.0 - result.hitSceneUV.y) * 2.0 - 1.0);
                uint2 hitPixel = min(uint2(result.hitSceneUV * uViewData.resolution.xy), uint2(uViewData.resolution.xy) - 1u);
                float4 hitEncodedVelocity = VelocityImage[hitPixel];
                float3 hitHistoryPosition = float3(hitScreenPosition, result.rayDeviceZ) - decodeVelocity(hitEncodedVelocity);

                float vignette = min(computeHitVignette(hitScreenPosition), computeHitVignette(hitHistoryPosition.xy));
                hit = vignette >= noise;

                float2 hitHistoryUV = float2(hitHistoryPosition.x * 0.5 + 0.5, 0.5 - hitHistoryPosition.y * 0.5);
                if (hit)
                {
                    float historyDeviceZ = (PC.traceFlags & kScreenProbeTraceFlagDepthHistoryValid) != 0
                        ? SceneDepthHistory.SampleLevel(PointClampSampler, hitHistoryUV, 0.0)
                        : SceneDepthImage.SampleLevel(PointClampSampler, hitHistoryUV, 0.0);
                    float historyDepthTolerance = kHistoryDepthTestRelativeThickness
                        * (uViewData.viewToClip[3][2] / kUENearClippingPlane)
                        * lerp(0.5, 2.0, noise);
                    hit = abs(historyDeviceZ - hitHistoryPosition.z) < historyDepthTolerance;
                }

                if (hit)
                {
                    float2 halfTexel = 0.5 * uViewData.resolution.zw;
                    float2 historyColorUV = clamp(hitHistoryUV, halfTexel, 1.0 - halfTexel);
                    float preExposureCorrection = getPreExposure(uViewData) / max(uViewData.prevPreExposure, 0.0001);
                    float3 lighting = TAAHistoryColor.SampleLevel(PointClampSampler, historyColorUV, 0.0).rgb * preExposureCorrection;

                    // UE IsTraceMoving(LumenScreenProbeTracingCommon.ush:28-46): 命中点世界速度(靠历史重投影反推,
                    // 不是精确值)跟探针自己的世界速度比, 相对差异超过阈值就算这条光线的光照在动
                    float hitWorldSpeed = calculateWorldSpaceSpeed(hitViewPosition, hitEncodedVelocity, uViewData);
                    float probeWorldSpeed = ScreenProbeWorldSpeed[atlasCoord];
                    bool moving = abs(probeWorldSpeed - hitWorldSpeed) / max(probeDepth, 1.0) > kRelativeSpeedDifferenceToConsiderLightingMoving;

                    writeTrace(traceBufferCoord, quantizeFloatColor(lighting, kR11G11B10QuantizationError, noise), hitDistance, true, moving);
                    return;
                }
            }
        }
    }

    float volumeNoise = rand16ToFloat(rand3DPCG16(int3(int2(traceBufferCoord), int(screenProbeFrameIndex() % 8u))).x);

    if ((PC.traceFlags & kScreenProbeTraceFlagGdfTraceEnabled) != 0)
    {
        WorldCoord rayOrigin = uViewData.cameraOrigin;
        rayOrigin.offset += mul(uViewData.worldToViewRot, probeViewPosition);

        GlobalDistanceFieldCascadeInfo cascades[kGlobalDistanceFieldCascadeCount];
        for (uint i = 0; i < uint(kGlobalDistanceFieldCascadeCount); ++i)
            cascades[i] = globalDistanceFieldCascadeInfoAtIndex(uViewData.cameraOrigin, i);

        // M3c: Mesh SDF 近场, 覆盖 [屏幕追踪结束的地方, kTraceMeshSDFsDistance] 这一段, 排在
        // GDF 之前——GDF 起点相应从 kMinTraceDistance 改成 kTraceMeshSDFsDistance
        float meshSDFNearFieldStart = max(kMinTraceDistance, screenTraceDistance);
        if (meshSDFNearFieldStart < kTraceMeshSDFsDistance)
        {
            GlobalDistanceFieldResolveResult nearFieldResult = traceMeshSDFNearField(
                cascades, GdfIndirection, ObjectGrid, MeshInstanceDataBuffer, MeshShapeData,
                MeshDistanceFieldBrickAtlas, PC.mdfAtlasBrickGridExtent, LinearSampler,
                rayOrigin, rayDirWS, meshSDFNearFieldStart, kTraceMeshSDFsDistance,
                CardPages, CardFinalLightingAtlas, CardAlbedoAtlas, CardNormalAtlas,
                CardEmissiveAtlas, CardDirectLightingAtlas, CardIndirectLightingAtlas, CardDepthAtlas);

            if (nearFieldResult.valid)
            {
                // 精确命中, 不像 GDF 那样需要 smoothstep 外扩容差淡出——那是补偿 GDF 体素化
                // 表面本身的模糊, MDF 是精确几何求交, 不存在这个问题
                float3 lighting = (nearFieldResult.cardHit ? nearFieldResult.cardLighting : float3(0.0)) * getPreExposure(uViewData);
                writeTrace(traceBufferCoord, quantizeFloatColor(lighting, kR11G11B10QuantizationError, volumeNoise), nearFieldResult.hitDistance, true, false);
                return;
            }
        }

        GlobalDistanceFieldTraceInput traceInput = setupGlobalDistanceFieldTraceInput(
            rayOrigin, rayDirWS, max(kTraceMeshSDFsDistance, screenTraceDistance), PC.maxWorldTraceDistance, 1.0, 1.0);
        GlobalDistanceFieldTraceResult traceResult = traceGlobalDistanceField(
            cascades, GdfIndirection, GdfAtlas, LinearSampler, PC.gdfAtlasBrickGridExtent, traceInput);

        if (traceResult.hit)
        {
            WorldCoord hitPos = rayOrigin;
            hitPos.offset += rayDirWS * traceResult.hitDistance;

            GlobalDistanceFieldResolveResult resolveResult = resolveGlobalDistanceFieldHit(
                cascades[traceResult.hitCascadeIndex], traceResult.hitCascadeIndex, hitPos,
                GdfIndirection, GdfAtlas, PC.gdfAtlasBrickGridExtent, rayDirWS,
                ObjectGrid, MeshInstanceDataBuffer, MeshShapeData, LinearSampler,
                CardPages, CardFinalLightingAtlas, CardAlbedoAtlas, CardNormalAtlas,
                CardEmissiveAtlas, CardDirectLightingAtlas, CardIndirectLightingAtlas, CardDepthAtlas);

            float3 lighting = resolveResult.cardHit ? resolveResult.cardLighting : float3(0.0);
            float voxelExtent = globalDistanceFieldVoxelExtent(cascades[traceResult.hitCascadeIndex]);
            lighting *= smoothstep(1.5 * voxelExtent, 2.0 * voxelExtent, traceResult.hitDistance) * getPreExposure(uViewData);

            // UE USE_VOXEL_TRACE_HIT_DISTANCE 为 0: 体素命中写进 TraceHit 的是最大追踪距离, 不是实际命中距离
            // 体素命中不算 bMoving(UE 同样没有对这条路径算 WorldVelocity, 见计划文档 §7)
            writeTrace(traceBufferCoord, quantizeFloatColor(lighting, kR11G11B10QuantizationError, volumeNoise), PC.maxWorldTraceDistance, true, false);
            return;
        }
    }

    float3 skyRadiance = SkyLightPrefilter.SampleLevel(LinearSampler, rayDirWS, 0.0).rgb
        * SkyLightData[kSkyLightRadianceScaleIndex].xyz * getPreExposure(uViewData);
    writeTrace(traceBufferCoord, quantizeFloatColor(skyRadiance, kR11G11B10QuantizationError, volumeNoise), PC.maxWorldTraceDistance, false, false);
}
