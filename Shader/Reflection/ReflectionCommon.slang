#pragma once
#include "../Common/Binding.slang"
#include "../Common/Math.slang"

// UE 默认值,不加后处理设置项——LumenMaxRoughnessToTraceReflections 后处理默认 0.4(Scene.cpp:658),
// RoughnessFadeLength 0.1(LR.cpp:129),GGXSamplingBias 0.1(LR.cpp:137)
static const float kReflectionMaxRoughnessToTrace = 0.4;
static const float kReflectionRoughnessFadeLength = 0.1;
static const float kReflectionGGXSamplingBias = 0.1;

// UE MinReflectionConeAngle,必须大于半精度浮点最小正规数,LRC.ush:119
static const float kReflectionMinConeAngle = 0.00001;

// 追踪专属反射光线的权重,0 表示完全交给探针粗糙镜面,1 表示完全交给专属追踪,UE LRCB.ush:12-16
float reflectionCombineAlpha(float roughness)
{
    return saturate((kReflectionMaxRoughnessToTrace - roughness) * (1.0 / kReflectionRoughnessFadeLength));
}

// UE NeedsLumenReflections()(不含 Substrate 分支),LRCB.ush:18-25
bool needsReflectionRay(float roughness, bool isClearCoat)
{
    return reflectionCombineAlpha(roughness) > 0.0 || isClearCoat;
}

// 跟 ScreenProbeCommon.slang::screenProbeLinearDepth 同一个公式,这里单独放一份是因为那个文件
// 属于探针专属模块,不该被反射的 shader 整个 include 进来
float reflectionLinearDepth(float deviceZ, in ViewData viewData)
{
    float m22 = viewData.viewToClip[2][2];
    float m32 = viewData.viewToClip[3][2];
    return max(m32 / max(deviceZ + m22, 1e-7), 1e-4);
}

// 跟 ScreenProbeCommon.slang::screenProbeViewPositionAtDepth 同一个公式,理由同上
float3 reflectionViewPositionAtDepth(float2 screenUV, float linearDepth, in ViewData viewData)
{
    float4 posCS = float4(screenUV.x * 2.0 - 1.0, (1.0 - screenUV.y) * 2.0 - 1.0, 0.5, 1.0);
    float4 posVS = mul(posCS, viewData.clipToView);
    float3 ray = posVS.xyz / posVS.w;
    return ray / max(-ray.z, 1e-6) * linearDepth;
}

// UE EncodeRayDistance/DecodeRayDistance(LRC.ush:89-98)——符号位就是命中标志,不含 UE 探针路径
// 那种 bMoving 位, 反射不追踪光照是否在动
float encodeRayDistance(float hitDistance, bool hit)
{
    return max(hitDistance, 0.0) * (hit ? -1.0 : 1.0);
}

float decodeRayDistance(float encoded, out bool hit)
{
    hit = encoded < 0.0;
    return abs(encoded);
}

// UE r.Lumen.Reflections.MaxRayIntensity 默认值(40, 带预曝光), LR.cpp:180-181
static const float kReflectionMaxRayIntensity = 40.0;

float3 clampReflectionRayIntensity(float3 lighting)
{
    float maxLighting = max(max(lighting.x, lighting.y), lighting.z);
    return maxLighting > kReflectionMaxRayIntensity ? lighting * (kReflectionMaxRayIntensity / maxLighting) : lighting;
}

// UE INVALID_LIGHTING(StochasticLightingCommon.ush:189)——R11G11B10 能表示的最大值, 借用它当
// "这个像素不需要反射"的哨兵, 不用额外开一个 bool 纹理
static const float3 kReflectionInvalidLighting = float3(65024.0, 65024.0, 64512.0);

bool isReflectionLightingValid(float3 v)
{
    return !all(v == kReflectionInvalidLighting);
}

float3 makeFinite(float3 v)
{
    return select(isnan(v) | isinf(v), float3(0.0), v);
}

// 跟 ScreenProbeCommon.slang::reprojectPrevViewPositionToCurrent 反方向的镜像版本——纯相机
// 重投影(不含任何逐像素测量速度), 给任意当前帧 view space 位置(包括虚构的命中点深度)算出
// "如果这个点是静止的, 上一帧会在 view space 哪里"。R4 命中点历史重投影要用, 见计划文档 §3.4
float3 reprojectCurrentViewPositionToPrev(float3 currentViewPosition, in ViewData viewData)
{
    float3 relativeWorld = mul(viewData.worldToViewRot, currentViewPosition)
        + calculateRelativeWorldPosition(viewData.cameraOrigin, viewData.prevCameraOrigin);
    return mul(relativeWorld, viewData.prevWorldToViewRot);
}

// 标准 YCoCg(不带 TAA.slang 那份的 4 倍 Y 缩放), 跟 UE LumenRGBToYCoCg/LumenYCoCgToRGB
// (StochasticLightingCommon.ush)系数一致
float3 reflectionRGBToYCoCg(float3 rgb)
{
    float y = dot(rgb, float3(0.25, 0.5, 0.25));
    float co = dot(rgb, float3(0.5, 0.0, -0.5));
    float cg = dot(rgb, float3(-0.25, 0.5, -0.25));
    return float3(y, co, cg);
}

float3 reflectionYCoCgToRGB(float3 yCoCg)
{
    return float3(
        yCoCg.x + yCoCg.y - yCoCg.z,
        yCoCg.x + yCoCg.z,
        yCoCg.x - yCoCg.y - yCoCg.z);
}
