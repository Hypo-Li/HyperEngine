// 屏幕空间 HZB 追踪(Screen Probe Gather 的第一级追踪, 见 Document/Lumen_ScreenProbeGather_Plan.md
// §4)——给一条 view space 射线, 在 HZBPass.cpp 构建的 Closest(max-Z)/Furthest(min-Z) 双通道
// mip 金字塔里做 stackless 遍历, 判断有没有撞上屏幕上已经光栅化出来的几何体。跟 UE
// HZBTracing.ush::TraceHZB(整份文件, 33-251 行)逐行对照, 只做这一件事: 不采颜色、不算光照,
// 命中/未命中之后要不要采历史帧颜色是调用方的事。除了薄片检查步数跟 UE 一样做成逐调用参数,
// 其余参数对齐 UE 出厂配置(HZBTraversal.FullResDepth=1、RelativeDepthThickness=.02、
// MaxIterations=50), 不暴露成可调项——这是"效果对齐 UE"这一版要的默认值。
//
// 坐标系注意: 本引擎 view space 是右手系 -Z 朝前(CameraProxy::makePerspectiveMatrix:
// clip.w = -z_view), UE 是左手系 +Z 朝前(跟 SSAOCommon.slang 顶部注释同一个问题)。这份移植里
// 唯一牵涉"裸 view space 方向 z 分量正负"判断的地方是 setupHZBTraceRay() 里的
// RayEndWorldDistance 那一段, 已经按这个符号翻转重新推导过(见该函数内注释), 不是照抄 UE 原始
// 符号。主循环全程在 (HZB UV, deviceZ) 空间里跑, deviceZ 的 reversed-Z 约定两边一致
// (HZBSeed.slang 的 Furthest=min 归约跟 UE 同一个约定), 不需要额外处理。
#pragma once
#include "Binding.slang"

struct HZBTraceResult
{
    bool hit;
    bool uncertain;
    float2 hitSceneUV;         // 全分辨率 SceneDepth/SceneColor UV(已经从 HZB UV 换算过)
    float hitDeviceZ;
    float rayDeviceZ;          // 射线在 hitSceneUV 处自己的 deviceZ, 对应 UE TraceHZB 的 OutScreenUV.z
    float2 lastVisibleSceneUV; // 最后一次确定未遮挡的位置, 命中判定失败时的回退点
};

// UE HZBTracing.ush::LineBoxIntersect 的标准 slab 法, 返回 (tEntry, tExit)。
// rayEnd==rayStart 某个分量相同时 1/0 = ±inf, min/max 在 IEEE754 下仍给出正确结果
float2 hzbTraceLineBoxIntersect(float3 rayStart, float3 rayEnd, float3 boxMin, float3 boxMax)
{
    float3 invDir = 1.0 / (rayEnd - rayStart);
    float3 t0 = (boxMin - rayStart) * invDir;
    float3 t1 = (boxMax - rayStart) * invDir;
    float3 tMinV = min(t0, t1);
    float3 tMaxV = max(t0, t1);
    return float2(max(max(tMinV.x, tMinV.y), tMinV.z), min(min(tMaxV.x, tMaxV.y), tMaxV.z));
}

// deviceZ -> 线性 view space 深度(正值)的闭式解, 跟 SSAOCommon.slang 里 linearDepthFromDeviceZ
// 同一个公式, UE 对应 ConvertFromDeviceZ——独立复制一份而不是共享, 跟这个引擎其它地方
// (SSAOSetup.slang/SSAOCommon.slang 各自一份)的既有做法一致
float hzbTraceLinearDepth(float deviceZ, in ViewData viewData)
{
    float m22 = viewData.viewToClip[2][2];
    float m32 = viewData.viewToClip[3][2];
    return max(m32 / max(deviceZ + m22, 1e-7), 1e-4);
}

// UE LumenScreenTracing.ush::ApplyScreenSpaceRayBias 逐行对照——沿法线把射线起点推出去,
// 推出距离是"像素对角texel 角点跟像素中心在同一深度平面下, 沿法线方向的投影量"乘 2, 而不是
// 固定 epsilon: HZB 遍历用点采样, 斜面在深度纹理里天然会有跟被表示平面之间的锯齿状凸起,
// 坡度越陡这个凸起越大, 固定 epsilon 在陡坡上会不够、平地上又太多。这里在 view space 里算
// (跟 UE 用 TranslatedWorldPosition 不是同一个空间, 但两者都是同一个仿射变换族里的相对量,
// 点积得到的投影量不受这个选择影响)
float3 applyScreenSpaceRayBias(float3 originVS, float3 normalVS, float2 uv, float deviceZ, in ViewData viewData)
{
    float2 cornerUV = uv + 0.5 * viewData.resolution.zw;
    float4 cornerClip = float4(cornerUV.x * 2.0 - 1.0, (1.0 - cornerUV.y) * 2.0 - 1.0, deviceZ, 1.0);
    float4 cornerPosVS = mul(cornerClip, viewData.clipToView);
    cornerPosVS *= 1.0 / cornerPosVS.w;

    float normalBias = abs(dot(cornerPosVS.xyz - originVS, normalVS)) * 2.0;
    return originVS + normalBias * normalVS;
}

// view space 位置/方向 -> (HZB UV, deviceZ) 起点/终点, 对应 TraceHZB 开头到
// "Recalculate end point where it leaves the screen" 为止(HZBTracing.ush:52-75)
void setupHZBTraceRay(
    float3 rayOriginVS,
    float3 rayDirectionVS,
    float maxWorldTraceDistance,
    float2 hzbUvFactor,
    in ViewData viewData,
    out float3 outRayStartUV,
    out float3 outRayEndUV)
{
    float4 startClip = mul(float4(rayOriginVS, 1.0), viewData.viewToClip);
    // UE 这里的下限是 1.0(cm), 换算成米
    float3 startScreenPos = startClip.xyz / max(startClip.w, 0.01);
    outRayStartUV = float3((startScreenPos.xy * float2(0.5, -0.5) + 0.5) * hzbUvFactor, startScreenPos.z);

    // UE(+Z 朝前): RayEndWorldDistance = ViewRayDirection.z<0 ? min(-.99*SceneDepth/ViewRayDirection.z, Max) : Max,
    // SceneDepth>0 是朝前距离, z<0 表示射线朝相机方向拐回去、会在有限距离内穿过 Z=0 平面。
    // 本引擎(-Z 朝前): rayOriginVS.z 本身就是负的"朝前距离"取反, 直接解 Z(t) = rayOriginVS.z +
    // t*rayDirectionVS.z = 0 得 t = -rayOriginVS.z/rayDirectionVS.z, 这个交点在射线前方
    // (t>0)当且仅当 rayDirectionVS.z>0(Z 从负值往 0 增长, 即朝相机方向拐)
    float rayEndWorldDistance = rayDirectionVS.z > 0.0
        ? min(-0.99 * rayOriginVS.z / rayDirectionVS.z, maxWorldTraceDistance)
        : maxWorldTraceDistance;

    float3 rayWorldEndVS = rayOriginVS + rayDirectionVS * rayEndWorldDistance;
    float4 endClip = mul(float4(rayWorldEndVS, 1.0), viewData.viewToClip);
    // 跟起点的下限保护对称: rayDirectionVS.z>0 分支把终点钉在离相机 Z=0 平面很近的地方,
    // endClip.w 可以很小, 不加下限的话透视除法会把浮点噪声放大进 outRayEndUV, 进而污染
    // rayDirectionUV(主循环全程用它选边界/算相交时间)
    float3 endScreenPos = endClip.xyz / max(endClip.w, 0.01);
    outRayEndUV = float3((endScreenPos.xy * float2(0.5, -0.5) + 0.5) * hzbUvFactor, endScreenPos.z);

    float2 screenEdgeIntersections = hzbTraceLineBoxIntersect(
        outRayStartUV, outRayEndUV, float3(0.0, 0.0, 0.0), float3(hzbUvFactor, 1.0));
    outRayEndUV = outRayStartUV + (outRayEndUV - outRayStartUV) * screenEdgeIntersections.y;
}

// UE 默认出厂值, 见 Document/Lumen_ScreenProbeGather_Plan.md §4
static const float kHZBTraceRelativeDepthThickness = 0.02;
static const float kHZBTraceMaxIterations = 50.0;
// 漫反射探针追踪传这个值(UE LumenScreenProbeTracing.usf); 镜面反射追踪传 0 关掉薄片检查
// (UE LumenReflectionTracing.usf:117)
static const float kHZBTraceNumThicknessStepsToDetermineCertainty = 4.0;
// HZBTraversal.FullResDepth=1: 钻到比 mip0 更细一级时直接采全分辨率 SceneDepth, 不停在 mip0
static const float kHZBTraceBaseMipLevel = -1.0;

// 核心遍历, 逐段对应 UE HZBTracing.ush:77-251(TraceHZB 去掉射线起点/终点计算之后的部分)。
// hzbUvFactor: screenUV[0,1] -> HZB 贴图 UV[0,hzbUvFactor] 的缩放(AOPass::screenToHZBUVScale
// 同一个换算); hzbBaseTexelSize: mip0 一个 texel 占多少 UV(= 1/baseExtent, 用于逐级算 tile 尺寸)
HZBTraceResult traceHZB(
    DepthTexture2D sceneDepth,
    Texture2D<float2> sceneHZB,
    SamplerState pointClampSampler,
    in ViewData viewData,
    float2 hzbUvFactor,
    float2 hzbBaseTexelSize,
    float3 rayOriginVS,
    float3 rayDirectionVS,
    float maxWorldTraceDistance,
    float numThicknessStepsToDetermineCertainty)
{
    float3 rayStartUV, rayEndUV;
    setupHZBTraceRay(rayOriginVS, rayDirectionVS, maxWorldTraceDistance, hzbUvFactor, viewData, rayStartUV, rayEndUV);

    const float baseMipLevel = kHZBTraceBaseMipLevel;
    float mipLevel = baseMipLevel;
    float3 rayDirectionUV = rayEndUV - rayStartUV;
    float currentIntersectionTime = 0.0;
    float lastAboveSurfaceTime = 0.0;

    // 每一步选哪一侧的 XY 边界跟射线方向同号: 正向走下一个整数边界(+1), 负向走当前边界(+0)
    float2 floorOffset = float2(
        rayDirectionUV.x < 0.0 ? 0.0 : 1.0,
        rayDirectionUV.y < 0.0 ? 0.0 : 1.0);

    float3 rayScreenUV = rayStartUV;

    // 先把射线推出当前 texel 自己的格子, 避免第一步就跟起点所在 texel 自相交(HZBTracing.ush:92-108)
    {
        float2 currentMipTexelSize = exp2(mipLevel) * hzbBaseTexelSize;
        float2 currentMipResolution = 1.0 / currentMipTexelSize;

        float2 uvOffset = 0.005 * currentMipTexelSize;
        uvOffset = float2(
            rayDirectionUV.x < 0.0 ? -uvOffset.x : uvOffset.x,
            rayDirectionUV.y < 0.0 ? -uvOffset.y : uvOffset.y);

        float2 xyPlane = floor(rayScreenUV.xy * currentMipResolution) + floorOffset;
        xyPlane = xyPlane * currentMipTexelSize + uvOffset;

        float2 planeIntersectionTimes = (xyPlane - rayStartUV.xy) / rayDirectionUV.xy;
        currentIntersectionTime = min(planeIntersectionTimes.x, planeIntersectionTimes.y);
        rayScreenUV = rayStartUV + currentIntersectionTime * rayDirectionUV;
    }

    if (currentIntersectionTime >= 1.0)
        rayScreenUV = rayStartUV;

    float numIterations = 0.0;
    bool hit = false;
    bool uncertain = false;
    float hitDeviceZ = 0.0;

    // Stackless HZB 遍历: 当前 tile 空(还在表面前方)就退到 tile 边界、mip+1(变粗, 一步跳过更大
    // 范围); 不空(已经钻进表面里)就原地 mip-1(变细复核), 直到细过 baseMipLevel 为止
    while (mipLevel >= baseMipLevel && numIterations < kHZBTraceMaxIterations && currentIntersectionTime < 1.0)
    {
        float2 currentMipTexelSize = exp2(mipLevel) * hzbBaseTexelSize;
        float2 currentMipResolution = 1.0 / currentMipTexelSize;

        float2 uvOffset = 0.005 * currentMipTexelSize;
        uvOffset = float2(
            rayDirectionUV.x < 0.0 ? -uvOffset.x : uvOffset.x,
            rayDirectionUV.y < 0.0 ? -uvOffset.y : uvOffset.y);

        float2 xyPlane = floor(rayScreenUV.xy * currentMipResolution) + floorOffset;
        xyPlane = xyPlane * currentMipTexelSize + uvOffset;

        // mip<0: 直接采全分辨率 SceneDepth(HZB UV -> 全分辨率 UV = 除以 hzbUvFactor);
        // mip>=0: 采 Closest(max-Z, G 通道)HZB, 保守判定"这个 tile 里有没有可能挡着东西"
        float tileZ;
        if (mipLevel < 0.0)
            tileZ = sceneDepth.SampleLevel(pointClampSampler, rayScreenUV.xy / hzbUvFactor, 0.0);
        else
            tileZ = sceneHZB.SampleLevel(pointClampSampler, rayScreenUV.xy, mipLevel).g;

        float3 boundaryPlanes = float3(xyPlane, tileZ);
        float3 planeIntersectionTimes3 = (boundaryPlanes - rayStartUV) / rayDirectionUV;
        planeIntersectionTimes3.z = rayDirectionUV.z < 0.0 ? planeIntersectionTimes3.z : 1.0;
        float intersectionTime = min(planeIntersectionTimes3.x, planeIntersectionTimes3.y);

        bool aboveSurface = rayScreenUV.z > tileZ;
        bool skippedTile = aboveSurface;

        intersectionTime = min(intersectionTime, planeIntersectionTimes3.z);
        skippedTile = skippedTile && (intersectionTime != planeIntersectionTimes3.z);

        if (skippedTile)
            lastAboveSurfaceTime = intersectionTime;

        currentIntersectionTime = aboveSurface ? intersectionTime : currentIntersectionTime;
        rayScreenUV = rayStartUV + min(currentIntersectionTime, 1.0) * rayDirectionUV;
        mipLevel += skippedTile ? 1.0 : -1.0;

        numIterations += 1.0;
    }

    if (mipLevel < baseMipLevel && currentIntersectionTime < 1.0)
    {
        float tileZ = sceneDepth.SampleLevel(pointClampSampler, rayScreenUV.xy / hzbUvFactor, 0.0);
        hitDeviceZ = tileZ;

        float hitSceneDepth = hzbTraceLinearDepth(tileZ, viewData);
        float raySceneDepth = hzbTraceLinearDepth(rayScreenUV.z, viewData);
        hit = (raySceneDepth - hitSceneDepth) < kHZBTraceRelativeDepthThickness * max(hitSceneDepth, 1e-5);

        if (!hit)
            rayScreenUV = rayStartUV + lastAboveSurfaceTime * rayDirectionUV;
    }

    // 命中之后再往前线性探几个点, 发现探到的还是"薄片"(草/毛发这类)就标记 uncertain——调用方
    // 把 uncertain 当"没有确定命中"处理, 见 HZBTracing.ush:215-238
    if (hit && numThicknessStepsToDetermineCertainty > 0.0)
    {
        const float thicknessSearchMipLevel = 0.0;
        float mipNumTexels = exp2(thicknessSearchMipLevel);
        float2 hzbTileSize = mipNumTexels * hzbBaseTexelSize;
        float numSteps = numThicknessStepsToDetermineCertainty / mipNumTexels;
        float thicknessSearchEndTime = min(
            length(rayDirectionUV.xy * hzbTileSize * numSteps) / length(rayEndUV.xy - rayScreenUV.xy), 1.0);

        for (float i = 0.0; i < numSteps; i += 1.0)
        {
            float3 sampleUV = rayScreenUV + (i / numSteps) * thicknessSearchEndTime * (rayEndUV - rayScreenUV);
            if (all(sampleUV.xy > float2(0.0, 0.0)) && all(sampleUV.xy < hzbUvFactor))
            {
                float sampleTileZ = sceneHZB.SampleLevel(pointClampSampler, sampleUV.xy, thicknessSearchMipLevel).g;
                if (sampleUV.z > sampleTileZ)
                    uncertain = true;
            }
        }
    }

    HZBTraceResult result;
    result.hit = hit && !uncertain;
    result.uncertain = uncertain;
    result.hitSceneUV = rayScreenUV.xy / hzbUvFactor;
    result.hitDeviceZ = hitDeviceZ;
    result.rayDeviceZ = rayScreenUV.z;
    result.lastVisibleSceneUV = (rayStartUV.xy + lastAboveSurfaceTime * rayDirectionUV.xy) / hzbUvFactor;
    return result;
}
