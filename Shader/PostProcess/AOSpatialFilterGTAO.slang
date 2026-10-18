#include "../Common/Binding.slang"

[[vk::binding(0, 2)]]
DepthTexture2D SceneDepth;

[[vk::binding(1, 2)]]
Texture2D<float> AORawImage;

[[vk::binding(2, 2)]]
[format("r8")]
WTexture2D<float> AOFilteredImage;

struct AOSpatialFilterConstants
{
    float intensity;
    float power;
};
[[vk::push_constant]]
AOSpatialFilterConstants PC;

static constexpr float kHalfPI = 1.57079633;

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    AOFilteredImage.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
        return;

    int2 coord = int2(dispatchThreadID.xy);
    int2 maxCoord = int2(width - 1, height - 1);
    float centerDepth = SceneDepth[coord];

    // 天空/背景不需要滤波, 直接透传(AOGenerate 那边已经写了 1.0, 这里避免深度差比较在
    // 背景上产生没有意义的权重)
    if (centerDepth <= 0.0)
    {
        AOFilteredImage.Store(coord, AORawImage[coord]);
        return;
    }

    // UE GTAO 的结构化采样 pattern 需要配套的 5x5 filter；原来的十字 5 taps 无法覆盖完整
    // pattern。先从中心两侧选择更可信的深度梯度, 建立局部平面, 再按样本偏离该平面的程度
    // 加权；相比直接比较 sampleDepth-centerDepth, 这不会错误排斥同一个倾斜平面上的邻居。
    // UE 当前 GTAO spatial filter 虽然计算过一个 linear depth 临时值，但后续实际使用的
    // 梯度、平面拟合和权重仍然全部基于 device depth；这里保持同样的约定。
    float xM2 = SceneDepth[clamp(coord + int2(-2, 0), int2(0, 0), maxCoord)];
    float xM1 = SceneDepth[clamp(coord + int2(-1, 0), int2(0, 0), maxCoord)];
    float xP1 = SceneDepth[clamp(coord + int2( 1, 0), int2(0, 0), maxCoord)];
    float xP2 = SceneDepth[clamp(coord + int2( 2, 0), int2(0, 0), maxCoord)];
    float yM2 = SceneDepth[clamp(coord + int2(0, -2), int2(0, 0), maxCoord)];
    float yM1 = SceneDepth[clamp(coord + int2(0, -1), int2(0, 0), maxCoord)];
    float yP1 = SceneDepth[clamp(coord + int2(0,  1), int2(0, 0), maxCoord)];
    float yP2 = SceneDepth[clamp(coord + int2(0,  2), int2(0, 0), maxCoord)];

    float leftError = abs((xM1 + (xM1 - xM2)) - centerDepth);
    float rightError = abs((xP1 + (xP1 - xP2)) - centerDepth);
    float topError = abs((yM1 + (yM1 - yM2)) - centerDepth);
    float bottomError = abs((yP1 + (yP1 - yP2)) - centerDepth);
    float2 depthGradient = float2(
        leftError < rightError ? xM1 - xM2 : xP2 - xP1,
        topError < bottomError ? yM1 - yM2 : yP2 - yP1);

    float sum = 0.0;
    float totalWeight = 0.0;
    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            int2 sampleCoord = clamp(coord + int2(x, y), int2(0, 0), maxCoord);
            float sampleDepth = SceneDepth[sampleCoord];
            if (sampleDepth <= 0.0)
                continue;

            float planeDepth = centerDepth + depthGradient.x * float(x) + depthGradient.y * float(y);
            float weight = 1.0 - saturate(abs(sampleDepth - planeDepth) * 20000.0);
            sum += AORawImage[sampleCoord] * weight;
            totalWeight += weight;
        }
    }

    float filtered = totalWeight > 0.0 ? (sum / totalWeight) : AORawImage[coord];
    // AOGenerate 按 UE 约定乘了 2/PI；空间滤波完成后乘回 PI/2, 恢复供 lighting 使用的 [0,1]
    // visibility。
    float ao = saturate(filtered * kHalfPI);
    float exponent = max(PC.power * 0.5, 0.0);
    ao = 1.0 - (1.0 - pow(abs(ao), exponent)) * PC.intensity;
    AOFilteredImage.Store(coord, saturate(ao));
}
