// Screen Probe Gather 探针网格的共享数学, 跟 UE LumenScreenProbeCommon.ush/LumenScreenProbeGather.usf
// 逐值核对。见 Document/Lumen_ScreenProbeGather_Plan.md M2。
//
// 跟 UE 的四处刻意偏差:
// 1. 位置相关的计算全程在 view space 做, 不是 UE 的 (Translated)WorldPosition——探针
//    和它采样的像素永远都在这一帧相机视锥内, view space 已经是一个精度安全的公共参照系, 不需要
//    引入 WorldCoord 的 tile+offset 大世界方案。PlaneDistance = |dot(Q-P, N)| 这个化简(见
//    calculatePlaneDistance 的注释)只依赖两点的相对向量, 换到 view space 算数值上完全等价,
//    换算成本地相对量还额外规避了大坐标点积相减的精度损失。探针图集存的法线是世界空间, 跟 UE 一致,
//    平面距离测试时再就地转到 view space。
// 2. 不区分 Foliage 材质的双面自遮挡, 少一套 ScreenProbeInterpolationDepthWeightFoliage 常量——
//    这个引擎现在没有对应的材质标记, 等真的需要时再补, 不影响这套插值权重公式本身。
// 3. 统一探针的屏幕坐标现算(getUniformScreenProbeScreenPosition), 只有自适应探针的屏幕坐标
//    存进 AdaptiveScreenProbePosAndTileIndices buffer, 跟 UE 一致。自适应探针跟统一探针共用同一张深度/法线
//    图集(接在统一探针后面的行), 跟 UE ScreenProbeAtlasBufferSize 同一个做法, 见
//    ScreenProbeAdaptivePlacement.slang。
// 4. 世界空间速度对所有像素统一走 Velocity 纹理(3D velocity), 没有 UE 里静态像素用 ClipToPrevClip
//    精确重投影的分支, 也不带双面植被/头发标志位——这个引擎没有对应的材质标记。
#pragma once
#include "../Common/Binding.slang"
#include "../Common/GBufferEncoding.slang"
#include "../Common/MonteCarlo.slang"
#include "../Common/FloatQuantization.slang"
#include "../Common/Math.slang"
#include "../Common/RadiosityProbeSampling.slang"

// UE r.Lumen.ScreenProbeGather.DownsampleFactor 默认值——探针网格间距, 固定像素数, 不随分辨率缩放
static const uint kScreenProbeDownsampleFactor = 16;

// UE 里按质量档位在 4/8/16 之间选(shader permutation), 这个引擎暂时没有质量档位, 固定取中间值
static const uint kScreenProbeNumAdaptiveSamplesPerTile = 8;

// UE r.LumenScreenProbeGather.AdaptiveProbeAllocationFraction 默认值——自适应探针总预算是统一
// 探针总数的这个比例, 全局共享, 不是按 tile 分配
static const float kScreenProbeAdaptiveProbeAllocationFraction = 0.5;

// UE r.Lumen.ScreenProbeGather.TracingOctahedronResolution 默认值, ScreenProbeTrace.slang 的 numthreads 字面量必须跟它一致
static const uint kScreenProbeTracingOctahedronResolution = 8;

// UE r.Lumen.ScreenProbeGather.GatherOctahedronResolutionScale 默认值 1.0, 即跟追踪分辨率相同
static const uint kScreenProbeGatherOctahedronResolution = kScreenProbeTracingOctahedronResolution;

// UE r.Lumen.ScreenProbeGather.Temporal.MaxRayDirections 默认值
static const uint kScreenProbeMaxRayDirections = 8;

// UE r.Lumen.ScreenProbeGather.Temporal.RelativeSpeedDifferenceToConsiderLightingMoving 默认值
static const float kRelativeSpeedDifferenceToConsiderLightingMoving = 0.005;

uint screenProbeFrameIndex()
{
    return uFrameData.frameIndex;
}

uint screenProbePrevFrameIndex()
{
    return uFrameData.frameIndex - 1u;
}

// UE SpatialFilterPositionWeightScale/SpatialFilterMaxRadianceHitAngle/SpatialFilterNumPasses 默认值
static const float kSpatialFilterPositionWeightScale = 1000.0;
static const float kSpatialFilterMaxRadianceHitAngle = 10.0 * PI / 180.0;
static const uint kSpatialFilterNumPasses = 3;

// UE IRRADIANCE_PROBE_RES / IRRADIANCE_PROBE_WITH_BORDER_RES(LumenScreenProbeCommon.ush:17-18)
static const uint kScreenProbeIrradianceResolution = 6;
static const uint kScreenProbeIrradianceWithBorderResolution = kScreenProbeIrradianceResolution + 2;

// UE 带边框辐射度图集的单侧边框宽度 exp2(ScreenProbeGatherMaxMip)(LumenScreenProbeGather.cpp:2259)
static const uint kScreenProbeRadianceBorderSize = 1;
static const uint kScreenProbeRadianceWithBorderResolution = kScreenProbeGatherOctahedronResolution + 2 * kScreenProbeRadianceBorderSize;

// UE MIN_PROBE_INTERPOLATION_WEIGHT——插值权重总和低于这个值, 就判定"统一网格在这个采样点上
// 没有可用的探针", 触发自适应探针分配
static const float kMinProbeInterpolationWeight = 0.01;

// UE r.Lumen.ScreenProbeGather.ImportanceSample.NumLevels 默认值——只做一轮细分, 不递归, 见
// Document/Lumen_ScreenProbeGather_Plan.md §9、M7.3
static const uint kScreenProbeImportanceSamplingUniformLevel = 1;
static const uint kScreenProbeMaxImportanceSamplingOctahedronResolution = kScreenProbeTracingOctahedronResolution * 2u;

// UE r.Lumen.ScreenProbeGather.ImportanceSample.MinPDFToTrace 默认值
static const float kScreenProbeImportanceSamplingMinPDFToTrace = 0.1;

// M7.3(GenerateRays)重定向表用: 6+6+4 位打包(纹素坐标 x/y + 细分等级), 跟 UE PackRayInfo/
// UnpackRayInfo(LumenScreenProbeTracingCommon.ush:67-78)位布局一致
uint packRayInfo(uint2 texelCoord, uint level)
{
    return (texelCoord.x & 0x3Fu) | ((texelCoord.y & 0x3Fu) << 6u) | ((level & 0xFu) << 12u);
}

void unpackRayInfo(uint packed, out uint2 texelCoord, out uint level)
{
    texelCoord.x = packed & 0x3Fu;
    texelCoord.y = (packed >> 6u) & 0x3Fu;
    level = (packed >> 12u) & 0xFu;
}

// UE DepthWeightScale(-10000) * CVarLumenScreenProbeInterpolationDepthWeight(1.0)
static const float kScreenProbeInterpolationDepthWeight = -10000.0;
// UE DepthWeightScale(-10000) * CVarLumenScreenProbeInterpolationDepthWeightFallback(0.1)
static const float kScreenProbeInterpolationDepthWeightFallback = -1000.0;

// deviceZ -> 线性 view space 深度(正值)
float screenProbeLinearDepth(float deviceZ, in ViewData viewData)
{
    float m22 = viewData.viewToClip[2][2];
    float m32 = viewData.viewToClip[3][2];
    return max(m32 / max(deviceZ + m22, 1e-7), 1e-4);
}

// screenUV 对应的相机射线, 缩放到 z=-1(view space -Z 朝前)——针孔相机模型下, 固定屏幕 UV 对应
// 一条从相机原点出发的射线, 射线上任意一点的 xyz 全部跟深度成正比, 缩放到 z=-1 之后乘任意线性
// 深度就是那个深度上的 view space 位置。用这个代替"deviceZ->view space"的常规重建, 是因为这个
// 文件里到处传的是线性深度(ScreenProbeSceneDepth 图集存的也是线性深度, 不是 deviceZ), 没有
// deviceZ 可用
float3 screenProbeViewRayAtUnitDepth(float2 screenUV, in ViewData viewData)
{
    float4 posCS = float4(screenUV.x * 2.0 - 1.0, (1.0 - screenUV.y) * 2.0 - 1.0, 0.5, 1.0);
    float4 posVS = mul(posCS, viewData.clipToView);
    float3 ray = posVS.xyz / posVS.w;
    return ray / max(-ray.z, 1e-6);
}

float3 screenProbeViewPositionAtDepth(float2 screenUV, float linearDepth, in ViewData viewData)
{
    return screenProbeViewRayAtUnitDepth(screenUV, viewData) * linearDepth;
}

// M7.2(Lighting PDF)用: 跟 screenProbeViewRayAtUnitDepth 同一个套路, 但用上一帧的(unjittered)
// clip->view 矩阵——把持久化的上一帧探针线性深度重建成"上一帧自己 view space 里"的位置。
// 不持久化世界位置图集(跟 UE HistoryScreenProbeTranslatedWorldPosition 不同), 原因见
// reprojectPrevViewPositionToCurrent 的注释
float3 prevScreenProbeViewRayAtUnitDepth(float2 screenUV, in ViewData viewData)
{
    float4 posCS = float4(screenUV.x * 2.0 - 1.0, (1.0 - screenUV.y) * 2.0 - 1.0, 0.5, 1.0);
    float4 posVS = mul(posCS, viewData.prevClipToViewUnjittered);
    float3 ray = posVS.xyz / posVS.w;
    return ray / max(-ray.z, 1e-6);
}

float3 prevScreenProbeViewPositionAtDepth(float2 screenUV, float linearDepth, in ViewData viewData)
{
    return prevScreenProbeViewRayAtUnitDepth(screenUV, viewData) * linearDepth;
}

// 把一个"上一帧 view space"里的位置换算成这一帧的 view space——跟 calculateWorldSpaceSpeed 同一套
// "相对世界"换算(view->world 旋转 + 相机原点差 + world->view 旋转)。UE 的等价比较
// (LumenScreenProbeImportanceSampling.usf:100-101)直接拿持久化的 HistoryScreenProbeTranslatedWorldPosition
// 跟这一帧的 TranslatedWorldPosition 平面点积, 没有减掉两帧的 PreViewTranslation 差——两者参照系
// 原点不一样(每帧都以当前相机为原点), 直接比较会多出相机帧间位移在法线方向的投影这个偏差项
// (对照 LumenScreenProbeFiltering.usf:250 那处做了 PrevPreViewTranslation 修正的姊妹函数可以看出来)。
// 这里改成现算 + 显式换算, 不持久化位置图集, 从结构上就不会漏掉这一步
float3 reprojectPrevViewPositionToCurrent(float3 prevViewPosition, in ViewData viewData)
{
    float3 relativeWorld = mul(viewData.prevWorldToViewRot, prevViewPosition)
        + calculateRelativeWorldPosition(viewData.prevCameraOrigin, viewData.cameraOrigin);
    return mul(relativeWorld, viewData.worldToViewRot);
}

// 跟 UE LumenScreenProbeGather.usf:97-109 的 ProbeWorldVelocity 长度对应: 这个像素的表面点从上一帧
// 到这一帧在世界空间移动的距离。viewPosition 是当前帧的 view space 位置, encodedVelocity 是它所在
// 像素的 Velocity 纹理原始值
float calculateWorldSpaceSpeed(float3 viewPosition, float4 encodedVelocity, in ViewData viewData)
{
    float4 currClip = mul(float4(viewPosition, 1.0), viewData.viewToClipUnjittered);
    float3 prevNDC = currClip.xyz / currClip.w - decodeVelocity(encodedVelocity);

    float4 prevView = mul(float4(prevNDC, 1.0), viewData.prevClipToViewUnjittered);
    float3 prevViewPosition = prevView.xyz / prevView.w;

    float3 currRelativePosition = mul(viewData.worldToViewRot, viewPosition);
    float3 prevRelativePosition = mul(viewData.prevWorldToViewRot, prevViewPosition)
        + calculateRelativeWorldPosition(viewData.prevCameraOrigin, viewData.cameraOrigin);
    return length(currRelativePosition - prevRelativePosition);
}

// 8 帧一循环的 Hammersley 时域抖动, 整个探针网格这一帧共享同一个偏移量——不是逐探针独立抖动。
// UE GetScreenTileJitter(LumenScreenProbeCommon.ush:71-76): Hammersley16(TemporalIndex, 8, 0)
uint2 getScreenTileJitter(uint frameIndex)
{
    float2 e = hammersley16(frameIndex % 8u, 8u, uint2(0, 0));
    return uint2(e * float(kScreenProbeDownsampleFactor));
}

// M7.2(Lighting PDF)历史重投影用: 跟 getScreenTileCoord 同一个换算, 但保留小数部分不取整——
// UE GetScreenTileCoordFromScreenUV(LumenScreenProbeCommon.ush:147-150), 调用方在此基础上
// floor + 0/1 偏移取 2x2 相邻 tile, 不是取整后单点采样
float2 getScreenTileCoordF(float2 screenPositionPixels, uint2 viewRectMin, uint frameIndex)
{
    return (screenPositionPixels - float2(viewRectMin) - float2(getScreenTileJitter(frameIndex))) / float(kScreenProbeDownsampleFactor);
}

// 屏幕坐标(uint, 16+16 位打包)编解码, 用于 AdaptiveScreenProbePosAndTileIndices 的屏幕坐标区段
uint packScreenPosition(uint2 screenPosition)
{
    return (screenPosition.x << 16) | (screenPosition.y & 0xFFFFu);
}
uint2 unpackScreenPosition(uint packed)
{
    return uint2(packed >> 16, packed & 0xFFFFu);
}

// 探针线性索引 <-> 图集坐标, 跟 UE 完全一致的行主序打包(ScreenProbeIndex = y*width + x)
uint2 screenProbeAtlasCoordFromIndex(uint screenProbeIndex, uint2 atlasViewSize)
{
    return uint2(screenProbeIndex % atlasViewSize.x, screenProbeIndex / atlasViewSize.x);
}

// 计数器超过预算时(放置 pass 预算用完后仍会自增)要封顶, 跟 UE GetNumScreenProbes
// (LumenScreenProbeCommon.ush:94-109)一致
uint getNumScreenProbes(uint numUniformProbes, uint adaptiveCounter, uint maxNumAdaptiveProbes)
{
    return numUniformProbes + min(adaptiveCounter, maxNumAdaptiveProbes);
}

// AdaptiveScreenProbeTotalAndTileCounts 布局: [0] 全局自适应探针计数, [1 + tile] 该 tile 的自适应探针数
uint getTileAdaptiveProbeCountOffset(uint tileLinearIndex)
{
    return 1u + tileLinearIndex;
}

// AdaptiveScreenProbePosAndTileIndices 布局: [adaptiveIndex] 打包屏幕坐标, [maxNumAdaptiveProbes + tile * 8 + slot] 该 tile 第 slot 个自适应探针的全局探针编号
uint getTileAdaptiveProbeIndexOffset(uint maxNumAdaptiveProbes, uint tileLinearIndex, uint slot)
{
    return maxNumAdaptiveProbes + tileLinearIndex * kScreenProbeNumAdaptiveSamplesPerTile + slot;
}

uint loadNumAdaptiveScreenProbes(StructuredBuffer<uint> totalAndTileCounts)
{
    return totalAndTileCounts[0];
}

uint loadNumTileAdaptiveProbes(StructuredBuffer<uint> totalAndTileCounts, uint tileLinearIndex)
{
    return min(totalAndTileCounts[getTileAdaptiveProbeCountOffset(tileLinearIndex)], kScreenProbeNumAdaptiveSamplesPerTile);
}

uint2 loadAdaptiveProbeScreenPosition(StructuredBuffer<uint> posAndTileIndices, uint adaptiveIndex)
{
    return unpackScreenPosition(posAndTileIndices[adaptiveIndex]);
}

uint loadTileAdaptiveProbeIndex(StructuredBuffer<uint> posAndTileIndices, uint maxNumAdaptiveProbes, uint tileLinearIndex, uint slot)
{
    return posAndTileIndices[getTileAdaptiveProbeIndexOffset(maxNumAdaptiveProbes, tileLinearIndex, slot)];
}

// tile 网格坐标 -> 这一帧统一探针实际落在的屏幕像素坐标(含共享时域抖动), 跟 UE
// ScreenProbeScreenPosition 计算式(LumenScreenProbeCommon.ush:124-125)一致
uint2 getUniformScreenProbeScreenPosition(uint2 tileCoord, uint2 viewRectMin, uint frameIndex)
{
    return tileCoord * kScreenProbeDownsampleFactor + getScreenTileJitter(frameIndex) + viewRectMin;
}

uint2 getClampedUniformScreenProbeScreenPosition(uint2 tileCoord, uint2 viewRectMin, uint2 viewExtent, uint frameIndex)
{
    return clamp(
        getUniformScreenProbeScreenPosition(tileCoord, viewRectMin, frameIndex),
        viewRectMin,
        viewRectMin + viewExtent - 1u);
}

uint2 getScreenProbeScreenPosition(
    uint probeIndex,
    uint2 atlasCoord,
    uint numUniformProbes,
    uint2 viewRectMin,
    uint2 viewExtent,
    uint frameIndex,
    StructuredBuffer<uint> posAndTileIndices)
{
    if (probeIndex < numUniformProbes)
        return getClampedUniformScreenProbeScreenPosition(atlasCoord, viewRectMin, viewExtent, frameIndex);
    return loadAdaptiveProbeScreenPosition(posAndTileIndices, probeIndex - numUniformProbes);
}

// 跟 UE GetScreenTileCoord(LumenScreenProbeCommon.ush:135-138)一致, 包括 uint 减法的回绕
uint2 getScreenTileCoord(uint2 screenPosition, uint2 viewRectMin, uint frameIndex)
{
    return (screenPosition - getScreenTileJitter(frameIndex) - viewRectMin) / kScreenProbeDownsampleFactor;
}

// UE GetAdaptiveSampleCoord: tile 内一组固定的随机候选子偏移(同一 tile 内每个候选用不同
// sampleIndex 区分), 不是"精确定位到突变边界", 是"预先撒候选点, 不合格的才留下"
uint2 getAdaptiveSampleScreenPosition(uint2 tileCoord, uint sampleIndex, uint numSamples, uint2 viewRectMin, uint frameIndex)
{
    uint2 tileOrigin = tileCoord * kScreenProbeDownsampleFactor + getScreenTileJitter(frameIndex);
    float2 e = hammersley16(sampleIndex, numSamples, uint2(0, 0));
    uint2 subOffset = clamp(uint2(e * float(kScreenProbeDownsampleFactor)), uint2(0, 0), uint2(kScreenProbeDownsampleFactor - 1));
    return tileOrigin + subOffset + viewRectMin;
}

// 平面距离测试: P 是采样点自己的 view space 位置/法线, Q 是候选探针的 view space 位置。
// 代数化简: dot(Q,N) - dot(P,N) == dot(Q-P, N), 只依赖相对向量, 这也是文件头注释第 1 条
// "换到 view space 算完全等价"的数学依据
float calculatePlaneDistance(float3 sampleViewPosition, float3 sampleViewNormal, float3 probeViewPosition)
{
    return abs(dot(probeViewPosition - sampleViewPosition, sampleViewNormal));
}

// UE CalculateUpsampleInterpolationWeights 的 PLANE_WEIGHTING 分支(LumenScreenProbeGather.usf:179-199)——
// 深度/法线相似度融合成同一个 exp2(负大数 * 相对平面距离²) 软阈值, primaryWeight 用严格系数,
// fallbackWeight 用更宽容的系数, 由调用方在总权重不够时才退而求其次
void calculateProbeDepthWeights(
    float planeDistance,
    float sampleSceneDepth,
    out float primaryWeight,
    out float fallbackWeight)
{
    float relativeDepthDifference = planeDistance / max(sampleSceneDepth, 1e-4);
    relativeDepthDifference *= relativeDepthDifference;
    primaryWeight = exp2(kScreenProbeInterpolationDepthWeight * relativeDepthDifference);
    fallbackWeight = exp2(kScreenProbeInterpolationDepthWeightFallback * relativeDepthDifference);
}

struct ScreenProbeUpsampleWeights
{
    uint2 atlasCoord[4];
    float4 weights;
    float4 fallbackWeights;
};

struct UniformProbeCorners
{
    uint2 tileCoord00;
    float4 bilinearBase;
};

UniformProbeCorners getUniformProbeCorners(
    float2 screenPixelCoord,
    float2 noiseOffset,
    uint2 viewRectMin,
    uint2 viewExtent,
    uint2 tileGridSize,
    uint frameIndex)
{
    float2 fullResCoord = float2(uint2(clamp(
        screenPixelCoord - float2(viewRectMin) - float2(getScreenTileJitter(frameIndex)) + noiseOffset,
        float2(0, 0),
        float2(viewExtent) - 1.0)));

    UniformProbeCorners corners;
    corners.tileCoord00 = min(uint2(fullResCoord) / kScreenProbeDownsampleFactor, tileGridSize - 2);

    const float bilinearExpand = 1.0;
    float2 bilinearWeights = (fullResCoord - float2(corners.tileCoord00 * kScreenProbeDownsampleFactor) + bilinearExpand)
        / (float(kScreenProbeDownsampleFactor) + 2.0 * bilinearExpand);

    corners.bilinearBase = float4(
        (1.0 - bilinearWeights.y) * (1.0 - bilinearWeights.x),
        (1.0 - bilinearWeights.y) * bilinearWeights.x,
        bilinearWeights.y * (1.0 - bilinearWeights.x),
        bilinearWeights.y * bilinearWeights.x);
    return corners;
}

uint2 getUniformProbeCornerTileCoord(UniformProbeCorners corners, uint cornerIndex)
{
    return corners.tileCoord00 + uint2(cornerIndex % 2, cornerIndex / 2);
}

// UE CalculateUniformUpsampleInterpolationWeights(LumenScreenProbeGather.usf:147-210)——统一
// 探针 2x2 双线性四角, 每一角再乘上 calculateProbeDepthWeights 算出的平面/深度项。
// cornerProbeDepths 里 <=0 表示无效探针(天空/Unlit, 见 ScreenProbeDownsampleUniform.slang 的符号
// 编码约定), 无效的角直接给零权重, 不参与插值。
ScreenProbeUpsampleWeights calculateUniformUpsampleWeights(
    UniformProbeCorners corners,
    float4 cornerProbeDepths,
    uint2 viewRectMin,
    uint frameIndex,
    float3 sampleViewPosition,
    float3 sampleViewNormal,
    float sampleSceneDepth,
    in ViewData viewData)
{
    ScreenProbeUpsampleWeights result;
    for (uint cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
    {
        uint2 tileCoord = getUniformProbeCornerTileCoord(corners, cornerIndex);
        result.atlasCoord[cornerIndex] = tileCoord;

        float probeDepth = cornerProbeDepths[cornerIndex];
        if (probeDepth <= 0.0)
        {
            result.weights[cornerIndex] = 0.0;
            result.fallbackWeights[cornerIndex] = 0.0;
            continue;
        }

        uint2 probeScreenPosition = getUniformScreenProbeScreenPosition(tileCoord, viewRectMin, frameIndex);
        float2 probeUV = (float2(probeScreenPosition) + 0.5) * viewData.resolution.zw;
        float3 probeViewPosition = screenProbeViewPositionAtDepth(probeUV, probeDepth, viewData);

        float planeDistance = calculatePlaneDistance(sampleViewPosition, sampleViewNormal, probeViewPosition);
        float primaryWeight, fallbackWeight;
        calculateProbeDepthWeights(planeDistance, sampleSceneDepth, primaryWeight, fallbackWeight);

        result.weights[cornerIndex] = corners.bilinearBase[cornerIndex] * primaryWeight;
        result.fallbackWeights[cornerIndex] = corners.bilinearBase[cornerIndex] * fallbackWeight;
    }

    return result;
}

// UE GetAdaptiveProbeInterpolationWeight(LumenScreenProbeGather.usf:216-242): x 是主权重, y 是备用权重
float2 getAdaptiveProbeInterpolationWeight(
    float2 screenPixelCoord,
    float3 sampleViewPosition,
    float3 sampleViewNormal,
    float sampleSceneDepth,
    uint2 probeScreenPosition,
    float3 probeViewPosition)
{
    float planeDistance = calculatePlaneDistance(sampleViewPosition, sampleViewNormal, probeViewPosition);
    float primaryWeight, fallbackWeight;
    calculateProbeDepthWeights(planeDistance, sampleSceneDepth, primaryWeight, fallbackWeight);

    float2 distanceToProbe = abs(float2(probeScreenPosition) - screenPixelCoord);
    float cornerWeight = 1.0 - saturate(min(distanceToProbe.x, distanceToProbe.y) / float(kScreenProbeDownsampleFactor));
    return float2(primaryWeight, fallbackWeight) * cornerWeight;
}

// UE CalculateUpsampleInterpolationWeights(LumenScreenProbeGather.usf:244-303), bUseAdaptiveProbes = true 的路径
ScreenProbeUpsampleWeights calculateUpsampleWeights(
    Texture2D<float> probeSceneDepth,
    StructuredBuffer<uint> posAndTileIndices,
    StructuredBuffer<uint> totalAndTileCounts,
    float2 screenPixelCoord,
    float2 noiseOffset,
    uint2 viewRectMin,
    uint2 viewExtent,
    uint2 tileGridSize,
    uint2 atlasViewSize,
    uint maxNumAdaptiveProbes,
    uint frameIndex,
    float3 sampleViewPosition,
    float3 sampleViewNormal,
    float sampleSceneDepth,
    in ViewData viewData)
{
    UniformProbeCorners corners = getUniformProbeCorners(screenPixelCoord, noiseOffset, viewRectMin, viewExtent, tileGridSize, frameIndex);
    float4 cornerProbeDepths;
    for (uint cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
        cornerProbeDepths[cornerIndex] = probeSceneDepth[getUniformProbeCornerTileCoord(corners, cornerIndex)];

    ScreenProbeUpsampleWeights result = calculateUniformUpsampleWeights(
        corners, cornerProbeDepths, viewRectMin, frameIndex,
        sampleViewPosition, sampleViewNormal, sampleSceneDepth, viewData);

    uint numUniformProbes = tileGridSize.x * tileGridSize.y;
    for (uint cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
    {
        if (result.weights[cornerIndex] >= kMinProbeInterpolationWeight)
            continue;

        uint2 tileCoord = result.atlasCoord[cornerIndex];
        uint tileLinearIndex = tileCoord.y * tileGridSize.x + tileCoord.x;
        uint numAdaptiveProbes = loadNumTileAdaptiveProbes(totalAndTileCounts, tileLinearIndex);

        for (uint listIndex = 0; listIndex < numAdaptiveProbes; ++listIndex)
        {
            uint probeIndex = loadTileAdaptiveProbeIndex(posAndTileIndices, maxNumAdaptiveProbes, tileLinearIndex, listIndex);
            uint2 probeAtlasCoord = screenProbeAtlasCoordFromIndex(probeIndex, atlasViewSize);
            uint2 probeScreenPosition = loadAdaptiveProbeScreenPosition(posAndTileIndices, probeIndex - numUniformProbes);
            float probeDepth = probeSceneDepth[probeAtlasCoord];

            float2 clampedProbePosition = min(float2(probeScreenPosition), float2(viewRectMin + viewExtent) - 1.0);
            float2 probeUV = (clampedProbePosition + 0.5) * viewData.resolution.zw;
            float3 probeViewPosition = screenProbeViewPositionAtDepth(probeUV, probeDepth, viewData);

            float2 newWeight = getAdaptiveProbeInterpolationWeight(
                screenPixelCoord, sampleViewPosition, sampleViewNormal, sampleSceneDepth, probeScreenPosition, probeViewPosition);

            if (newWeight.x > result.weights[cornerIndex])
            {
                result.weights[cornerIndex] = newWeight.x;
                result.fallbackWeights[cornerIndex] = newWeight.y;
                result.atlasCoord[cornerIndex] = probeAtlasCoord;
            }
        }
    }

    return result;
}

// [Clarberg 2008, "Fast Equal-Area Mapping of the (Hemi)Sphere using SIMD"]——全球面等面积
// 八面体映射, UE MonteCarlo.ush:159-173 逐行对照。输出是一个固定参照系(不跟随探针法线朝向)
// 下的单位方向, 覆盖整个球面(含背面), 不是半球——UE 自己也是全球面均匀采样, 靠后续重要性
// 重采样(M7)/双面材质背面跳过(M3 才有的概念)甩掉没用的方向, 不是在这一步就按法线裁剪掉半球。
// 见 Document/Lumen_ScreenProbeGather_Plan.md §2"光线方向映射"那条已拍板决定
float3 equiAreaSphericalMapping(float2 uv)
{
    uv = 2.0 * uv - 1.0;
    float d = 1.0 - (abs(uv.x) + abs(uv.y));
    float r = 1.0 - abs(d);
    float phi = (r == 0.0) ? 0.0 : (PI / 4.0) * ((abs(uv.y) - abs(uv.x)) / r + 1.0);
    float f = r * sqrt(2.0 - r * r);
    return float3(
        f * sign(uv.x) * abs(cos(phi)),
        f * sign(uv.y) * abs(sin(phi)),
        sign(d) * (1.0 - r * r)
    );
}

// UE InverseEquiAreaSphericalMapping(MonteCarlo.ush:178-210), equiAreaSphericalMapping 的逆
float2 inverseEquiAreaSphericalMapping(float3 direction)
{
    direction = normalize(direction);

    float3 absDir = abs(direction);
    float r = sqrt(1.0 - absDir.z);
    float epsilon = 5.42101086243e-20;
    float x = min(absDir.x, absDir.y) / (max(absDir.x, absDir.y) + epsilon);

    const float t1 = 0.406758566246788489601959989e-5;
    const float t2 = 0.636226545274016134946890922156;
    const float t3 = 0.61572017898280213493197203466e-2;
    const float t4 = -0.247333733281268944196501420480;
    const float t5 = 0.881770664775316294736387951347e-1;
    const float t6 = 0.419038818029165735901852432784e-1;
    const float t7 = -0.251390972343483509333252996350e-1;

    float phi = t6 + t7 * x;
    phi = t5 + phi * x;
    phi = t4 + phi * x;
    phi = t3 + phi * x;
    phi = t2 + phi * x;
    phi = t1 + phi * x;

    phi = (absDir.x < absDir.y) ? 1.0 - phi : phi;
    float2 uv = float2(r - phi * r, phi * r);
    uv = (direction.z < 0.0) ? 1.0 - uv.yx : uv;
    uv = asfloat(asuint(uv) ^ (asuint(direction.xy) & 0x80000000u));
    return uv * 0.5 + 0.5;
}

// 探针八面体图集里, 某个追踪 texel 对应的射线方向(世界空间, 未定向到任何视角/法线参照系,
// 见 equiAreaSphericalMapping 的注释)。跟 UE GetScreenProbeTexelRay
// (LumenScreenProbeTracingCommon.ush:122-135)对应, JITTER_RAY_DIRECTION 分支直接复用已有的
// getRadiosityProbeTexelCenter()(RadiosityProbeSampling.slang)——同一套"整个探针共享一个
// 蓝噪声子 texel 偏移"的抖动机制, 数学上完全一样, 不需要另起一份。probeGlobalCoord 是
// getScreenTileCoord() 算出的 tile 坐标, frameIndex 是 screenProbeFrameIndex() % kScreenProbeMaxRayDirections
float3 getScreenProbeTexelRayDirection(
    Texture2D<float4> blueNoiseVec2Texture,
    uint frameIndex,
    uint2 probeGlobalCoord,
    uint2 tracingTexelCoord,
    uint tracingOctahedronResolution)
{
    float2 texelCenter = getRadiosityProbeTexelCenter(blueNoiseVec2Texture, frameIndex, probeGlobalCoord);
    float2 probeUV = (float2(tracingTexelCoord) + texelCenter) / float(tracingOctahedronResolution);
    return equiAreaSphericalMapping(probeUV);
}

// UE OctahedralMapWrapBorder(OctahedralCommon.ush:50-74)
uint2 octahedralMapWrapBorder(uint2 texelCoord, uint resolution, uint borderSize)
{
    if (texelCoord.x < borderSize)
    {
        texelCoord.x = borderSize - 1 + borderSize - texelCoord.x;
        texelCoord.y = resolution - 1 - texelCoord.y;
    }
    if (texelCoord.x >= resolution - borderSize)
    {
        texelCoord.x = (resolution - borderSize) - (texelCoord.x - (resolution - borderSize - 1));
        texelCoord.y = resolution - 1 - texelCoord.y;
    }
    if (texelCoord.y < borderSize)
    {
        texelCoord.y = borderSize - 1 + borderSize - texelCoord.y;
        texelCoord.x = resolution - 1 - texelCoord.x;
    }
    if (texelCoord.y >= resolution - borderSize)
    {
        texelCoord.y = (resolution - borderSize) - (texelCoord.y - (resolution - borderSize - 1));
        texelCoord.x = resolution - 1 - texelCoord.x;
    }
    return texelCoord - borderSize;
}

// 跟 UE EncodeProbeRayDistance(LumenScreenProbeCommon.ush:239-252)的位布局一致, 没有 Radiance Cache 所以不含 bit31
uint encodeProbeRayDistance(float hitDistance, bool hit, bool moving)
{
    uint encoded = asuint(max(hitDistance, 0.0)) & 0x7FFFFFFCu;
    encoded |= hit ? 1u : 0u;
    encoded |= moving ? 2u : 0u;
    return encoded;
}

struct ProbeRayDistance
{
    float hitDistance;
    bool hit;
    bool moving;
};

// 跟 UE DecodeProbeRayDistance(LumenScreenProbeCommon.ush:262-270)的位布局一致, 没有 Radiance Cache 所以不含 bit31
ProbeRayDistance decodeProbeRayDistance(uint encoded)
{
    ProbeRayDistance result;
    result.hit = (encoded & 1u) != 0u;
    result.moving = (encoded & 2u) != 0u;
    result.hitDistance = asfloat(encoded & 0x7FFFFFFCu);
    return result;
}

// 8 位 UNORM 编码, 精度按 sqrt 分配给近距离命中(UE EncodeProbeHitDistanceForFiltering 的公式,
// 但分母用我们自己的最大追踪距离, 不是 UE 给 Radiance Cache 远场探针共用的 MaxHalfFloat——
// 那个分母对我们 200m 的追踪范围只用得到编码范围的一小段, 8 位精度基本浪费掉)
float encodeProbeHitDistanceForFiltering(float hitDistance, float maxWorldTraceDistance)
{
    return sqrt(saturate(hitDistance / maxWorldTraceDistance)) * (254.0 / 255.0) + (1.0 / 255.0);
}

// 无效值统一编码成 0(UE 靠 UNORM 写入时把 sqrt(负数) 产生的 NaN 隐式钳到 0 实现同样效果,
// 这里直接显式判断, 不依赖 NaN 在存储目标上的钳位行为), 解码后必然是负数, 消费方按
// "非负才是有效值" 判断
float decodeProbeHitDistanceForFiltering(float encoded, float maxWorldTraceDistance)
{
    float linear = encoded * encoded * maxWorldTraceDistance;
    return (linear - 1.0 / 255.0) * (255.0 / 254.0);
}
