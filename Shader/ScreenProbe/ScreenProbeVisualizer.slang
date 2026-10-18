// 屏幕探针布点的临时调试可视化: 每个探针在它实际落脚的像素上画一个 3x3 的十字标记(四边形
// 覆盖整个 3x3 包围盒, 片元阶段丢弃四角落的像素只留中间行/列, 拼出镂空的十字形状), 统一
// 探针绿色、自适应探针红色, 叠在正常光照结果上——验证自适应探针是不是贴着深度/法线突变边缘
// 分布, 见 Document/Lumen_ScreenProbeGather_Plan.md M2、
// Source/Runtime/Renderer/RenderPass/ScreenProbeVisualizerPass.cpp。一次 instanced draw,
// SV_InstanceID 对应探针全局索引(0..numUniformProbes 是统一探针, 之后是自适应探针),
// instanceCount 由 ScreenProbeBuildDrawIndirect.slang 按实际分配出的自适应探针数算出,
// 不画没分配到的槽位(那些是瞬态图集复用的脏数据, 不是"没有探针")。
#include "../Common/Binding.slang"
#include "../Common/SHCommon.slang"
#include "ScreenProbeCommon.slang"
#include "ScreenProbeIrradianceFormat.slang"
#include "ScreenProbeIrradianceSH.slang"

[[vk::binding(0, 2)]]
Texture2D<float> ScreenProbeSceneDepth;

[[vk::binding(1, 2)]]
StructuredBuffer<uint> AdaptiveScreenProbePosAndTileIndices;

[[vk::binding(2, 2)]]
Texture2D<float4> ScreenProbeRadiance;

#if SCREEN_PROBE_IRRADIANCE_FORMAT == SCREEN_PROBE_IRRADIANCE_FORMAT_SH3

[[vk::binding(3, 2)]]
Texture2D<float4> ScreenProbeRadianceSHAmbient;

[[vk::binding(5, 2)]]
Texture2D<float4> ScreenProbeRadianceSHDirectional;

#else

[[vk::binding(3, 2)]]
Texture2D<float4> ScreenProbeIrradianceWithBorder;

#endif

[[vk::binding(4, 2)]]
StructuredBuffer<float> ScreenProbeBRDFPDFSH;

static const uint kColorModeRadiance = 1;
static const uint kColorModeIrradiance = 2;
static const uint kColorModeBRDFPDF = 3;
static const uint kNumSHCoefficients = 9;
static const float3 kBRDFPDFDebugDirection = float3(0.0, 0.0, 1.0);

// buffer 的平铺布局原因见 ScreenProbeBRDFPDF.slang 里同名 buffer 的绑定注释
ThreeBandSH readBRDFPDFSH(uint probeIndex)
{
    uint baseOffset = probeIndex * kNumSHCoefficients;
    ThreeBandSH sh;
    sh.v0 = float4(
        ScreenProbeBRDFPDFSH[baseOffset + 0], ScreenProbeBRDFPDFSH[baseOffset + 1],
        ScreenProbeBRDFPDFSH[baseOffset + 2], ScreenProbeBRDFPDFSH[baseOffset + 3]);
    sh.v1 = float4(
        ScreenProbeBRDFPDFSH[baseOffset + 4], ScreenProbeBRDFPDFSH[baseOffset + 5],
        ScreenProbeBRDFPDFSH[baseOffset + 6], ScreenProbeBRDFPDFSH[baseOffset + 7]);
    sh.v2 = ScreenProbeBRDFPDFSH[baseOffset + 8];
    return sh;
}

struct ScreenProbeVisualizerPushConstants
{
    uint2 atlasViewSize;
    uint numUniformProbes;
    float markerHalfSizePixels;
    uint2 viewRectMin;
    uint2 viewExtent;
    uint colorMode;
    uint padding;
};
[[vk::push_constant]]
ScreenProbeVisualizerPushConstants PC;

// Triangle_Strip 的 "Z" 顶点顺序(左下, 左上, 右下, 右上)——跟 GSplat.slang 同一个理由:
// 新 RHI 没有 Triangle_Fan(WebGPU 不支持), 顺序错了只会画出四边形里的一个三角形
static constexpr float2 kCornerOffsets[4] = {
    float2(-1, -1),
    float2(-1,  1),
    float2( 1, -1),
    float2( 1,  1)
};

float linearToSRGB(float color)
{
    color = max(color, 0.0);
    return color <= 0.0031308
        ? 12.92 * color
        : 1.055 * pow(color, 1.0 / 2.4) - 0.055;
}

struct V2F
{
    float4 svPosition : SV_Position;
    nointerpolation uint instanceID : TEXCOORD0;
    // 像素空间下、相对探针中心的偏移, 范围 [-markerHalfSizePixels, markerHalfSizePixels]——
    // 片元阶段拿它判断是不是十字的四个角落, 是就丢弃
    float2 localOffsetPixels : TEXCOORD1;
    nointerpolation float3 radianceColor : TEXCOORD2;
};

[shader("vertex")]
V2F VertexMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    V2F v2f;
    v2f.instanceID = instanceID;
    v2f.radianceColor = float3(0.0, 0.0, 0.0);

    uint2 atlasCoord = screenProbeAtlasCoordFromIndex(instanceID, PC.atlasViewSize);
    float probeDepth = ScreenProbeSceneDepth[atlasCoord];

    // 无效探针(天空/Unlit, 或者自适应槽位真没分配到——虽然按 instanceCount 已经不会画到这里,
    // 双重保险)折叠成四个顶点重合的退化四边形, 不产生任何像素覆盖, 不用另外裁剪/discard
    if (probeDepth <= 0.0)
    {
        v2f.svPosition = float4(-2.0, -2.0, 0.0, 1.0);
        v2f.localOffsetPixels = float2(0.0, 0.0);
        return v2f;
    }

    uint2 screenPosition = getScreenProbeScreenPosition(
        instanceID, atlasCoord, PC.numUniformProbes, PC.viewRectMin, PC.viewExtent, screenProbeFrameIndex(), AdaptiveScreenProbePosAndTileIndices);
    float2 pixelCenter = float2(screenPosition) + 0.5;
    float2 cornerOffsetPixels = kCornerOffsets[vertexID] * PC.markerHalfSizePixels;
    float2 pixelPos = pixelCenter + cornerOffsetPixels;

    float2 uv = pixelPos * uViewData.resolution.zw;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

    v2f.svPosition = float4(ndc, 0.0, 1.0);
    v2f.localOffsetPixels = cornerOffsetPixels;

    if (PC.colorMode == kColorModeBRDFPDF)
    {
        // 对自身法线方向求值必然接近峰值、所有探针都会顶成白色看不出差异, 所以固定统一求值方向
        ThreeBandSH brdfSH = readBRDFPDFSH(instanceID);
        float pdf = max(dotSH3(brdfSH, shBasisFunction3(kBRDFPDFDebugDirection)), 0.0);
        v2f.radianceColor = float3(saturate(pdf), saturate(pdf), saturate(pdf));
    }
    else if (PC.colorMode != 0)
    {
        float3 averageRadiance = float3(0.0, 0.0, 0.0);
        if (PC.colorMode == kColorModeIrradiance)
        {
#if SCREEN_PROBE_IRRADIANCE_FORMAT == SCREEN_PROBE_IRRADIANCE_FORMAT_SH3
            ThreeBandSHRGB radianceSH = readScreenProbeRadianceSH(
                ScreenProbeRadianceSHAmbient, ScreenProbeRadianceSHDirectional, atlasCoord, PC.atlasViewSize.x);
            // 高阶基函数在整个球面上积分为零, 所以球面平均辐照度只剩 L0 项
            float3 meanIrradiance = 4.0 * PI * screenProbeRadianceSHAmbient(radianceSH)
                * calcDiffuseTransferSH3(float3(0.0, 0.0, 1.0)).v0.x;
            averageRadiance = meanIrradiance / PI;
#else
            for (uint y = 0; y < kScreenProbeIrradianceResolution; ++y)
            {
                for (uint x = 0; x < kScreenProbeIrradianceResolution; ++x)
                {
                    uint2 irradianceTexelCoord = atlasCoord * kScreenProbeIrradianceWithBorderResolution + uint2(x + 1, y + 1);
                    averageRadiance += ScreenProbeIrradianceWithBorder[irradianceTexelCoord].rgb;
                }
            }
            averageRadiance /= float(kScreenProbeIrradianceResolution * kScreenProbeIrradianceResolution) * PI;
#endif
        }
        else
        {
            for (uint y = 0; y < kScreenProbeGatherOctahedronResolution; ++y)
            {
                for (uint x = 0; x < kScreenProbeGatherOctahedronResolution; ++x)
                {
                    uint2 gatherTexelCoord = atlasCoord * kScreenProbeGatherOctahedronResolution + uint2(x, y);
                    averageRadiance += ScreenProbeRadiance[gatherTexelCoord].rgb;
                }
            }
            averageRadiance /= float(kScreenProbeGatherOctahedronResolution * kScreenProbeGatherOctahedronResolution);
        }
        float3 compressedRadiance = averageRadiance / (1.0 + max(averageRadiance.r, max(averageRadiance.g, averageRadiance.b)));
        v2f.radianceColor = float3(linearToSRGB(compressedRadiance.r), linearToSRGB(compressedRadiance.g), linearToSRGB(compressedRadiance.b));
    }
    return v2f;
}

[shader("fragment")]
float4 FragmentMain(in V2F v2f) : SV_Target
{
    // 十字的臂宽固定 1 像素(中间那一行/列), 四个角落的像素丢弃——插值出来的
    // localOffsetPixels 天然是这个 3x3 包围盒内的双线性坐标, 直接拿来判断落在哪个格子
    if (abs(v2f.localOffsetPixels.x) > 0.5 && abs(v2f.localOffsetPixels.y) > 0.5)
        discard;

    if (PC.colorMode != 0)
        return float4(v2f.radianceColor, 1.0);

    bool isAdaptive = v2f.instanceID >= PC.numUniformProbes;
    return isAdaptive ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0);
}
