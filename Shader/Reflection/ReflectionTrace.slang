// 对应 UE ReflectionTraceScreenTexturesCS + ReflectionTraceMeshSDFsCS/ReflectionTraceVoxelsCS
// (LRT.usf),UE 每一级拆成独立 pass 加 compaction,这里单 pass 逐光线走完整条链,跟
// ScreenProbeTrace.slang 同一个理由(见该文件顶部注释)。光线数据来自 R1(ReflectionGenerateRays)
// 产出的 RayBuffer,不是探针八面体纹素;不写 bMoving,UE 反射追踪本身也不算这个。见
// Document/Lumen_Reflections_Plan.md §3.2
#include "../Common/Binding.slang"
#include "../Common/BindingAttributes.slang"
#include "../Common/HZBTrace.slang"
#include "../Common/GBufferEncoding.slang"
#include "../Common/MonteCarlo.slang"
#include "../Common/SkyLightData.slang"
#include "../GlobalDistanceField/GlobalDistanceFieldQuery.slang"
#include "ReflectionCommon.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

[[vk::binding(0, 2)]]
Texture2D<float4> RayBuffer;

[[vk::binding(1, 2)]]
Texture2D<float> RayDepth;

[[vk::binding(2, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(3, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(4, 2)]]
Texture2D<float2> SceneHZBImage;

[[vk::binding(5, 2)]]
[UnfilterableFloat]
Texture2D<float4> VelocityImage;

[[vk::binding(6, 2)]]
Texture2D<float4> TAAHistoryColor;

[[vk::binding(7, 2)]]
Texture2D<float> SceneDepthHistory;

// mip 0 是接近零粗糙度的 GGX 预滤波, 近似原始天空辐射度
[[vk::binding(8, 2)]]
TextureCube<float4> SkyLightPrefilter;

[[vk::binding(9, 2)]]
SamplerState PointClampSampler;

[[vk::binding(10, 2)]]
SamplerState LinearSampler;

[[vk::binding(11, 2)]]
[format("r11f_g11f_b10f")]
RWTexture2D<float4> TraceRadiance;

[[vk::binding(12, 2)]]
[format("r16f")]
RWTexture2D<float> TraceHit;

[[vk::binding(13, 2)]]
StructuredBuffer<uint> MeshShapeData;

[[vk::binding(14, 2)]]
StructuredBuffer<MeshInstanceData> MeshInstanceDataBuffer;

[[vk::binding(15, 2)]]
StructuredBuffer<uint> GdfIndirection;

[[vk::binding(16, 2)]]
Texture3D<float> GdfAtlas;

[[vk::binding(17, 2)]]
Texture2D<float4> CardNormalAtlas;

[[vk::binding(18, 2)]]
Texture2D<float> CardDepthAtlas;

[[vk::binding(19, 2)]]
StructuredBuffer<uint> ObjectGrid;

[[vk::binding(20, 2)]]
StructuredBuffer<uint> CardPages;

[[vk::binding(21, 2)]]
Texture2D<float4> CardFinalLightingAtlas;

[[vk::binding(22, 2)]]
Texture2D<float4> CardAlbedoAtlas;

[[vk::binding(23, 2)]]
Texture2D<float4> CardEmissiveAtlas;

[[vk::binding(24, 2)]]
Texture2D<float4> CardDirectLightingAtlas;

[[vk::binding(25, 2)]]
Texture2D<float4> CardIndirectLightingAtlas;

// 跟 GlobalDistanceFieldVisualizer.slang 的 MeshDistanceFieldBrickAtlas 同一份引擎全局资源
[[vk::binding(26, 2)]]
Texture3D<float> MeshDistanceFieldBrickAtlas;

[[vk::binding(27, 2)]]
StructuredBuffer<float4> SkyLightData;

struct ReflectionTracePushConstants
{
    uint2 viewRectMin;
    uint2 viewExtent;
    float2 hzbUvFactor;
    float2 hzbBaseTexelSize;
    uint traceFlags;
    float maxWorldTraceDistance;
    int gdfAtlasBrickGridExtent;
    int mdfAtlasBrickGridExtent;
};
[[vk::push_constant]]
ReflectionTracePushConstants PC;

static const uint kReflectionTraceFlagScreenTraceEnabled = 1u << 0;
static const uint kReflectionTraceFlagDepthHistoryValid = 1u << 1;
static const uint kReflectionTraceFlagGdfTraceEnabled = 1u << 2;

// UE r.Lumen.ScreenProbeGather.ScreenTraces.HZBTraversal.HistoryDepthTestRelativeThickness 默认值
static const float kHistoryDepthTestRelativeThickness = 0.005;

// UE GNearClippingPlane 默认值 10cm, 换算成米
static const float kUENearClippingPlane = 0.1;

// UE 在屏幕追踪未命中时给命中距离加的 2.0(cm), 换算成米
static const float kScreenTraceMissHitDistanceOffset = 0.02;

// UE r.Lumen.DiffuseIndirect.SurfaceBias 默认值(5cm), 换算成米——反射和漫反射 GI 共用同一个 CVar
static const float kMeshSDFSurfaceBias = 0.05;

// UE r.Lumen.TraceMeshSDFs.TraceDistance 默认值(180cm), 换算成米
static const float kTraceMeshSDFsDistance = 1.8;

// UE r.Lumen.Reflections.SampleSceneColorRelativeDepthThickness 默认值
static const float kSampleSceneColorRelativeDepthThreshold = 0.01;

// UE r.Lumen.Reflections.SampleSceneColorNormalTreshold 默认值(85°), 转成 cos 阈值
static const float kSampleSceneColorNormalThreshold = 0.0871557;

// UE r.Lumen.Reflections.DistantScreenTraces.DepthThreshold 默认值(名字是历史遗留, 实际是斜率容差)
static const float kDistantScreenTraceSlopeCompareTolerance = 2.0;

// UE r.Lumen.Reflections.DistantScreenTraces.MaxTraceDistance 默认值(200000cm), 换算成米
static const float kDistantScreenTraceMaxDistance = 2000.0;

// UE DistantScreenTrace 固定步数(LST.ush:154)
static const uint kDistantScreenTraceNumSteps = 16u;

// UE ComputeHitVignetteFromScreenPos(SSRTRayCast.ush:674-681)
float computeHitVignette(float2 screenPosition)
{
    float2 vignette = saturate(abs(screenPosition) * 5.0 - 4.0);
    return saturate(1.0 - dot(vignette, vignette));
}

void writeReflectionTrace(uint2 pixel, float3 radiance, float hitDistance, bool hit)
{
    TraceRadiance[pixel] = float4(radiance, 1.0);
    TraceHit[pixel] = encodeRayDistance(hitDistance, hit);
}

// UE SampleSceneColorAtHit(LST.ush:78-137)——GDF/体素命中之后, 尝试用上一帧场景颜色直接替换
// 表面缓存光照(不再加雾, 这个引擎本来就没有雾), 用来遮盖屏幕追踪在前景物体后面放弃的区域。
// 命中点重投影到的屏幕位置本来就不是当前像素自己的位置, 所以用该位置自己的 Velocity(跟屏幕
// 追踪命中重投影同一个写法), 不是 UE 那套"纯相机速度 + 动态像素覆盖"两段式——我们的 Velocity
// 纹理本身就是完整的相机+物体运动, 不需要那套区分(计划文档 §3.2/§4.4 已有的既定简化)
bool sampleSceneColorAtHit(float3 hitViewPosition, float3 hitNormalWS, float noise, in ViewData viewData, out float3 lighting)
{
    lighting = float3(0.0);

    float4 hitClip = mul(float4(hitViewPosition, 1.0), viewData.viewToClip);
    if (!(hitClip.w > 0.0))
        return false;

    float2 hitScreenPosition = hitClip.xy / hitClip.w;
    if (any(abs(hitScreenPosition) >= 1.0))
        return false;

    float2 hitScreenUV = float2(hitScreenPosition.x * 0.5 + 0.5, 0.5 - hitScreenPosition.y * 0.5);
    float hitDeviceZ = SceneDepthImage.SampleLevel(PointClampSampler, hitScreenUV, 0.0);
    float hitSceneDepth = reflectionLinearDepth(hitDeviceZ, viewData);
    float rayHitSceneDepth = -hitViewPosition.z;

    float3 pixelToCameraDirWS = mul(viewData.worldToViewRot, normalize(-hitViewPosition));
    if (abs(rayHitSceneDepth - hitSceneDepth) >= kSampleSceneColorRelativeDepthThreshold * max(hitSceneDepth, 1e-5)
        || dot(pixelToCameraDirWS, hitNormalWS) < kSampleSceneColorNormalThreshold)
    {
        return false;
    }

    float4 hitEncodedVelocity = VelocityImage.SampleLevel(PointClampSampler, hitScreenUV, 0.0);
    float3 hitHistoryPosition = float3(hitScreenPosition, hitDeviceZ) - decodeVelocity(hitEncodedVelocity);

    float vignette = min(computeHitVignette(hitScreenPosition), computeHitVignette(hitHistoryPosition.xy));
    if (vignette < noise)
        return false;

    float2 hitHistoryUV = float2(hitHistoryPosition.x * 0.5 + 0.5, 0.5 - hitHistoryPosition.y * 0.5);
    float historyDeviceZ = SceneDepthHistory.SampleLevel(PointClampSampler, hitHistoryUV, 0.0);
    // UE 这里直接用 SampleSceneColor 那个 0.01 阈值(不搭配近平面缩放项), 跟屏幕追踪命中
    // 重投影用的 kHistoryDepthTestRelativeThickness 是两套独立的容差, 不能混用
    float historyDepthTolerance = kSampleSceneColorRelativeDepthThreshold * lerp(0.5, 2.0, noise);
    if (abs(historyDeviceZ - hitHistoryPosition.z) >= historyDepthTolerance)
        return false;

    float2 halfTexel = 0.5 * viewData.resolution.zw;
    float2 historyColorUV = clamp(hitHistoryUV, halfTexel, 1.0 - halfTexel);
    float preExposureCorrection = getPreExposure(viewData) / max(viewData.prevPreExposure, 0.0001);
    lighting = TAAHistoryColor.SampleLevel(PointClampSampler, historyColorUV, 0.0).rgb * preExposureCorrection;
    return true;
}

struct DistantScreenTraceResult
{
    bool hit;
    float2 hitSceneUV;
    float hitDeviceZ;
};

// UE CastScreenSpaceRay(SSRTRayCast.ush:254-479)的简化版——固定 16 步定长线性步进, 只采样
// Furthest(min-Z, R 通道)HZB mip0, 不做批处理/二分/多级 mip/瓦片加速: 这条路径本身是"其它
// 追踪都放弃之后"的粗糙兜底, 大容差(斜率容差默认 2.0)换覆盖率而不是精度。射线起点/终点的
// UV 换算和屏幕边界裁剪复用 setupHZBTraceRay(跟主追踪同一套推导, 无需重写)
DistantScreenTraceResult distantScreenTraceHZB(
    Texture2D<float2> sceneHZB,
    SamplerState pointClampSampler,
    in ViewData viewData,
    float2 hzbUvFactor,
    float3 rayOriginVS,
    float3 rayDirectionVS,
    float maxWorldTraceDistance,
    float slopeCompareToleranceScale,
    float stepOffset)
{
    DistantScreenTraceResult result;
    result.hit = false;
    result.hitSceneUV = float2(0.0, 0.0);
    result.hitDeviceZ = 0.0;

    float3 startUV, endUV;
    setupHZBTraceRay(rayOriginVS, rayDirectionVS, maxWorldTraceDistance, hzbUvFactor, viewData, startUV, endUV);

    // 只让 view space Z 前进 maxWorldTraceDistance、屏幕 XY 不动时的 deviceZ, 用来估计这段
    // 追踪距离对应多大的 deviceZ 变化量, 给命中容差定标(UE InitScreenSpaceRayFromWorldSpace)
    float4 startClip = mul(float4(rayOriginVS, 1.0), viewData.viewToClip);
    float4 depthClip = startClip + maxWorldTraceDistance * viewData.viewToClip[2];
    float depthScreenZ = depthClip.z / max(depthClip.w, 0.01);

    float3 stepUV = endUV - startUV;
    float compareTolerance = max(abs(stepUV.z), (startUV.z - depthScreenZ) * slopeCompareToleranceScale)
        / float(kDistantScreenTraceNumSteps);
    float3 stepUVPerStep = stepUV / float(kDistantScreenTraceNumSteps);
    float3 rayUV = startUV + stepUVPerStep * stepOffset;

    for (uint i = 0; i < kDistantScreenTraceNumSteps; ++i)
    {
        rayUV += stepUVPerStep;

        float sampleDepth = sceneHZB.SampleLevel(pointClampSampler, rayUV.xy, 0.0).r;
        if (!(sampleDepth > 0.0))
            continue;

        float depthDiff = rayUV.z - sampleDepth;
        if (abs(depthDiff + compareTolerance) < compareTolerance)
        {
            result.hit = true;
            result.hitSceneUV = rayUV.xy / hzbUvFactor;
            result.hitDeviceZ = rayUV.z;
            break;
        }
    }

    return result;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 localCoord = dispatchThreadID.xy;
    if (any(localCoord >= PC.viewExtent))
        return;

    uint2 pixel = localCoord + PC.viewRectMin;
    float rayDepth = RayDepth[pixel];
    if (!(rayDepth > 0.0))
    {
        writeReflectionTrace(pixel, float3(0.0), 0.0, false);
        return;
    }

    float3 rayDirWS = RayBuffer[pixel].xyz;
    float2 screenUV = (float2(pixel) + 0.5) * uViewData.resolution.zw;
    float3 pixelViewPosition = reflectionViewPositionAtDepth(screenUV, rayDepth, uViewData);
    float noise = interleavedGradientNoise(float2(pixel) + 0.5, float(uFrameData.frameIndex % 8u));

    float screenTraceDistance = 0.0;
    if ((PC.traceFlags & kReflectionTraceFlagScreenTraceEnabled) != 0)
    {
        float3 normalWS = decodeGBufferNormal(GbufferAImage[pixel].x);
        float3 normalVS = mul(normalWS, uViewData.worldToViewRot);
        float3 rayDirVS = mul(rayDirWS, uViewData.worldToViewRot);

        float deviceZ = SceneDepthImage[pixel].r;
        float3 rayOriginVS = applyScreenSpaceRayBias(pixelViewPosition, normalVS, screenUV, deviceZ, uViewData);

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
                0.0);

            float hitDistanceOffset = (!result.hit && !result.uncertain) ? kScreenTraceMissHitDistanceOffset : 0.0;
            float3 hitViewPosition = reflectionViewPositionAtDepth(
                result.hitSceneUV, reflectionLinearDepth(result.rayDeviceZ, uViewData), uViewData);
            float hitDistance = min(length(hitViewPosition - pixelViewPosition) + hitDistanceOffset, PC.maxWorldTraceDistance);
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
                    float historyDeviceZ = (PC.traceFlags & kReflectionTraceFlagDepthHistoryValid) != 0
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
                    writeReflectionTrace(pixel, clampReflectionRayIntensity(lighting), hitDistance, true);
                    return;
                }
            }
        }
    }

    float volumeNoise = rand16ToFloat(rand3DPCG16(int3(int2(pixel), int(uFrameData.frameIndex % 8u))).x);

    if ((PC.traceFlags & kReflectionTraceFlagGdfTraceEnabled) != 0)
    {
        WorldCoord rayOrigin = uViewData.cameraOrigin;
        rayOrigin.offset += mul(uViewData.worldToViewRot, pixelViewPosition);

        GlobalDistanceFieldCascadeInfo cascades[kGlobalDistanceFieldCascadeCount];
        for (uint i = 0; i < uint(kGlobalDistanceFieldCascadeCount); ++i)
            cascades[i] = globalDistanceFieldCascadeInfoAtIndex(uViewData.cameraOrigin, i);

        float meshSDFNearFieldStart = max(kMeshSDFSurfaceBias, screenTraceDistance);
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
                float3 lighting = (nearFieldResult.cardHit ? nearFieldResult.cardLighting : float3(0.0)) * getPreExposure(uViewData);
                writeReflectionTrace(pixel, clampReflectionRayIntensity(lighting), nearFieldResult.hitDistance, true);
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

            if ((PC.traceFlags & kReflectionTraceFlagScreenTraceEnabled) != 0)
            {
                float3 hitViewPositionVS = pixelViewPosition + mul(rayDirWS, uViewData.worldToViewRot) * traceResult.hitDistance;
                float3 sampledLighting;
                if (sampleSceneColorAtHit(hitViewPositionVS, resolveResult.normalWS, noise, uViewData, sampledLighting))
                    lighting = sampledLighting;
            }

            // 体素命中写实际命中距离, 不是探针路径那种最大距离占位——反射的 Resolve/时域命中点
            // 重投影需要真实距离, 见计划文档 §3.2 差别说明
            writeReflectionTrace(pixel, clampReflectionRayIntensity(lighting), traceResult.hitDistance, true);
            return;
        }

        // R7 远距离屏幕追踪: GDF 也没命中时, 从 GDF 最外层边缘往外做一次线性屏幕追踪当兜底
        if ((PC.traceFlags & kReflectionTraceFlagScreenTraceEnabled) != 0)
        {
            WorldCoord distantRayOrigin = rayOrigin;
            float distantMaxDistance = kDistantScreenTraceMaxDistance;
            clipRayToStartOutsideGlobalDistanceField(
                cascades[kGlobalDistanceFieldCascadeCount - 1], distantRayOrigin, rayDirWS, distantMaxDistance);

            if (distantMaxDistance > 0.0)
            {
                float3 distantOriginVS = mul(
                    calculateRelativeWorldPosition(distantRayOrigin, uViewData.cameraOrigin), uViewData.worldToViewRot);
                float4 distantStartClip = mul(float4(distantOriginVS, 1.0), uViewData.viewToClip);

                if (distantStartClip.w >= 0.0
                    && all(distantStartClip.xy >= -distantStartClip.ww) && all(distantStartClip.xy <= distantStartClip.ww))
                {
                    float3 rayDirVS = mul(rayDirWS, uViewData.worldToViewRot);
                    DistantScreenTraceResult distantResult = distantScreenTraceHZB(
                        SceneHZBImage, PointClampSampler, uViewData, PC.hzbUvFactor,
                        distantOriginVS, rayDirVS, distantMaxDistance,
                        kDistantScreenTraceSlopeCompareTolerance, noise);

                    if (distantResult.hit)
                    {
                        float2 distantHitScreenPosition = float2(
                            distantResult.hitSceneUV.x * 2.0 - 1.0, 1.0 - distantResult.hitSceneUV.y * 2.0);
                        float4 distantHitEncodedVelocity = VelocityImage.SampleLevel(PointClampSampler, distantResult.hitSceneUV, 0.0);
                        float3 distantHitHistoryPosition = float3(distantHitScreenPosition, distantResult.hitDeviceZ)
                            - decodeVelocity(distantHitEncodedVelocity);

                        float distantVignette = min(
                            computeHitVignette(distantHitScreenPosition), computeHitVignette(distantHitHistoryPosition.xy));
                        if (distantVignette >= noise)
                        {
                            float2 distantHitHistoryUV = float2(
                                distantHitHistoryPosition.x * 0.5 + 0.5, 0.5 - distantHitHistoryPosition.y * 0.5);
                            float2 halfTexel = 0.5 * uViewData.resolution.zw;
                            distantHitHistoryUV = clamp(distantHitHistoryUV, halfTexel, 1.0 - halfTexel);
                            float preExposureCorrection = getPreExposure(uViewData) / max(uViewData.prevPreExposure, 0.0001);
                            float3 lighting = TAAHistoryColor.SampleLevel(PointClampSampler, distantHitHistoryUV, 0.0).rgb
                                * preExposureCorrection;

                            // UE 这条路径不知道真实命中距离, 沿用最大追踪距离当占位(跟 GDF miss 时的写法一致)
                            writeReflectionTrace(pixel, clampReflectionRayIntensity(lighting), PC.maxWorldTraceDistance, true);
                            return;
                        }
                    }
                }
            }
        }
    }

    float3 skyRadiance = SkyLightPrefilter.SampleLevel(LinearSampler, rayDirWS, 0.0).rgb
        * SkyLightData[kSkyLightRadianceScaleIndex].xyz * getPreExposure(uViewData);
    writeReflectionTrace(pixel, skyRadiance, PC.maxWorldTraceDistance, false);
}
