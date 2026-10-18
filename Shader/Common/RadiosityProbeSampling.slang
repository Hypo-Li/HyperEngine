// Radiosity 探针位置/光线方向的抖动公式——[A] Trace、[C] ConvertToSH、[D] Integrate 三个
// pass 必须用完全相同的公式(同一帧内), 否则 ConvertToSH 读到的 TraceRadiance 跟它自己重新
// 生成的光线方向对不上、Integrate 反解出来的探针坐标跟 Trace 当初放置探针的位置对不上,
// 单独拆出来只在这一处定义, 见 Document/Lumen_Radiosity_Plan.md §3。
#pragma once
#include "MonteCarlo.slang"

// 跟 UE MaxFramesAccumulated(LumenRadiosity.cpp)一致——探针位置/光线方向抖动按这个周期
// 循环。M3 阶段还没有时域累积消费它, 但抖动公式先按 M4 需要的样子写对, 省得以后回头改
// Trace/ConvertToSH/Integrate 的数学
static const uint kRadiosityMaxFramesAccumulated = 4;

// 跟 UE LumenRadiosity.ush::GetProbeJitter() 一致——固定种子, 同一页内所有探针共用
// 同一个抖动偏移(不是逐探针的), 单位是 texel, 范围 [0, 4)
uint2 getRadiosityProbeJitter(uint temporalIndex)
{
    return uint2(hammersley16(temporalIndex % kRadiosityMaxFramesAccumulated, kRadiosityMaxFramesAccumulated, uint2(0x4ae4, 0x9bdb)) * 4.0);
}

// Resource/FastBlueNoise_vec2_128x128x64.dds 的固定尺寸, 跟 DefaultResources::
// createBlueNoiseVec2Texture() 是同一份约定
static const uint3 kBlueNoiseVec2Dimensions = uint3(128, 128, 64);

// 跟 UE LumenRadiosity.ush::GetProbeTexelCenter() 的 BLUE_NOISE 分支一致——用真实帧号(不取模
// kRadiosityMaxFramesAccumulated)索引蓝噪声第三维, 每一帧的图案都不一样, 64 帧后才重复,
// 不会像 探针位置抖动(kRadiosityMaxFramesAccumulated 周期)那样肉眼可见地循环。
// probeGlobalCoord 是全局探针坐标(页原点/4 + 页内本地探针坐标), 对应 UE 的 ProbeTileCoord
float2 getRadiosityProbeTexelCenter(Texture2D<float4> blueNoiseVec2Texture, uint temporalIndex, uint2 probeGlobalCoord)
{
    uint3 wrapped = uint3(probeGlobalCoord, temporalIndex) % kBlueNoiseVec2Dimensions;
    int2 texelCoord = int2(wrapped.x, wrapped.z * kBlueNoiseVec2Dimensions.y + wrapped.y);
    return blueNoiseVec2Texture.Load(int3(texelCoord, 0)).rg;
}

// 跟 UE LumenRadiosity.usf::CalculatePlaneWeight() 一致(ProbePlaneWeightingDepthScale 默认值
// -100, LR.cpp:84)——[B] SpatialFilter、[D] Integrate 共用同一份公式, 不允许各自漂移
static const float kProbePlaneWeightingDepthScale = -100.0;

bool calculatePlaneWeight(float3 worldPosition, float3 worldNormal, float3 probeWorldPosition)
{
    float4 texelPlane = float4(worldNormal, dot(probeWorldPosition, worldNormal));
    float planeDistance = abs(dot(float4(worldPosition, -1.0), texelPlane));
    float relativeDistance = max(planeDistance / (length(probeWorldPosition - worldPosition) + 0.01), 0.1);
    float depthWeight = exp2(kProbePlaneWeightingDepthScale * (relativeDistance * relativeDistance));
    return depthWeight > 0.01;
}
