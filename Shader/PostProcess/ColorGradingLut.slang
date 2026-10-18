#include "ColorGradingCommon.slang"

[[vk::binding(0, 2)]]
[format("rgba8")]
WTexture3D<float4> ColorGradingLutImage;

struct ColorGradingLutPushConstants
{
    uint toneMapper; // ToneMapper: 0 = Linear, 1 = ACES, 2 = UE_Filmic
};
[[vk::push_constant]]
ColorGradingLutPushConstants PC;

static constexpr uint kLutSize = 32;

[shader("compute")]
[numthreads(4, 4, 4)]
void ComputeMain(uint3 globalID : SV_DispatchThreadID)
{
    if (any(globalID >= kLutSize))
        return;

    // LUT 的三个坐标轴直接就是 LogC 编码值域 [0, 1], 反解出这个 texel 对应的线性场景颜色
    float3 logC = (float3(globalID) + 0.5) / float(kLutSize);
    float3 sceneLinear = logCToLinear(logC);

    // TODO: Color Grading(white balance/channel mixer/tonal ranges/CDL/adjustments)尚未实现,
    // 参数定下来后在这里对 sceneLinear 应用调色, 结果再送入下面的 tonemap
    float3 graded = sceneLinear;

    float3 toneMapped = PC.toneMapper == 2u ? UEFilmic(graded)
        : (PC.toneMapper == 1u ? ACES(graded) : saturate(graded));
    float3 outColor = linearToSRGB(toneMapped);

    ColorGradingLutImage.Store(globalID, float4(outColor, 1.0));
}
