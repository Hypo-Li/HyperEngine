#include "ColorGradingCommon.slang"
#include "../Common/Binding.slang"
#include "../Common/Math.slang"

[[vk::binding(0, 2)]]
SamplerState LutSampler;

[[vk::binding(1, 2)]]
Texture3D<float4> ColorGradingLutTexture;

[[vk::binding(2, 2)]]
Texture2D<float4> SceneColor;

[[vk::binding(3, 2)]]
StructuredBuffer<float4> AutoExposureData;

[[vk::binding(4, 2)]]
Texture2D<float4> BloomTexture;

struct ColorGradingConstants
{
    float bloomIntensity;
};
[[vk::push_constant]]
ColorGradingConstants PC;

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

static constexpr float2 kTexCoords[3] = {
    float2(0, 2),
    float2(2, 0),
    float2(0, 0)
};

struct VertexOutput
{
    float4 svPosition : SV_Position;
    float2 texCoord : TEXCOORD0;
};

[shader("vertex")]
VertexOutput VertexMain(in uint vertexID : SV_VertexID)
{
    VertexOutput output;
    output.svPosition = float4(kPositions[vertexID], 0, 1);
    output.texCoord = kTexCoords[vertexID];
    return output;
}

[shader("fragment")]
float4 FragmentMain(in VertexOutput input) : SV_Target
{
    uint width;
    uint height;
    SceneColor.GetDimensions(width, height);
    uint2 coord = min(
        uint2(input.texCoord * float2(width, height)),
        uint2(width - 1, height - 1)
    );
    float exposureCorrection = AutoExposureData[0].x / max(uViewData.preExposure, 0.0001);
    float3 sceneLinear = SceneColor[coord].rgb * exposureCorrection;

    // BloomTexture 是半分辨率的, 这里用双线性把它放大到全分辨率——已经是相邻 mip 之间做过 tent
    // 滤波的平滑结果, 单次双线性采样足够, 不需要再做一次 tent。BloomPass 关闭/没跑过时,
    // ColorGradingPass.cpp 会把这个槽位绑成 DefaultResources 的黑纹理, PC.bloomIntensity
    // 乘多少都还是零, 不需要额外的开关分支
    // BloomTexture 是同一帧 SceneColor 降采样出来的, 跟 SceneColor[coord] 一样还停留在
    // "上一帧 preExposure 预曝光过"的空间, 要乘同一个 exposureCorrection 才能跟已经校正过的
    // sceneLinear 处在同一套曝光单位下, 否则曝光变化时 bloom 亮度会跟主图不同步
    float3 bloomSample = BloomTexture.SampleLevel(LutSampler, input.texCoord, 0.0).rgb;
    sceneLinear += bloomSample * exposureCorrection * PC.bloomIntensity;

    // 用生成 LUT 时同一套 LogC 编码把线性场景颜色映射到 LUT 采样坐标, 三线性采样后直接得到
    // 已经调色 + tonemap + gamma 编码好的最终颜色
    float3 uvw = saturate(linearToLogC(max(sceneLinear, 0.0)));
    float3 graded = ColorGradingLutTexture.SampleLevel(LutSampler, uvw, 0.0).rgb;

    // 输出最终要量化到 8-bit swapchain, 量化前加一个 1 LSB 幅度的三角分布抖动打散台阶(TAA 会在
    // 时间上把这层噪声抹平), 避免大色域渐变(比如天空)出现可见的 banding
    float ditherNoise = interleavedGradientNoise(input.svPosition.xy, uFrameData.frameIndex % 8);
    graded += uniformNoiseToTriangular(ditherNoise) * (1.0 / 255.0);

    return float4(graded, 1.0);
}
