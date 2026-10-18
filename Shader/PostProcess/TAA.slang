#include "../Common/Binding.slang"
#include "../Common/BindingAttributes.slang"
#include "../Common/GBufferEncoding.slang"

[[vk::binding(0, 2)]]
SamplerState LinearClampSampler;

[[vk::binding(1, 2)]]
Texture2D<float4> SceneColor;

[[vk::binding(2, 2)]]
[UnfilterableFloat]
Texture2D<float4> Velocity;

[[vk::binding(3, 2)]]
DepthTexture2D SceneDepth;

[[vk::binding(4, 2)]]
// [format("unknown")]: 这个 view 是 D32_SFLOAT_S8_UINT 的 stencil-only 切面, 不是真的 R32ui。
// Slang 直接编译到 SPIR-V 时, 标量 Texture2D<uint> 默认会把 image format 打成 R32ui, 但 Vulkan
// 对 stencil-only 视图要求这里必须是 Unknown, 否则 texel 读取结果未定义。显式指定这个属性能让
// SPIR-V 输出 OpTypeImage ... Unknown, 不再需要借道 GLSL 中间路径重新编译(那条路径会导致同一个
// entry point 里 DepthTexture2D 的下标运算符退化成返回 vec4 而不是 scalar, 见 SceneDepth 那个坑)。
// WGSL 后端忽略这个属性, 照常编译成 texture_2d<u32>, 不受影响。
[format("unknown")]
Texture2D<uint> SceneStencil;

[[vk::binding(5, 2)]]
Texture2D<float4> History;

static constexpr uint kTranslucentStencilBit = 0x01;

// UE TemporalAA.usf Main pass(非升采样, r.AntiAliasingMethod=TAA 的默认路径)的邻域框/重建
// 滤波形状默认是十字 5-tap(N/W/C/E/S, UE 的 kPlusIndexes3x3, TAA_PASS_CONFIG==0 时 AA_SAMPLES
// 继承默认值 5)。下面同时提供 3x3(8 邻居)版本, 改成 0 切换过去——3x3 邻域更宽松, 抑制鬼影更
// 强但更容易糊, 十字版本是跟 UE 默认行为对齐的选择。
#define TAA_NEIGHBORHOOD_CROSS 1

struct TAAConstants
{
    uint historyValid;
};
[[vk::push_constant]]
TAAConstants PC;

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

//------------------------------------------------------- 颜色空间

// UE TemporalAA.usf 的 RGBToYCoCg/YCoCgToRGB: Y 通道自带 4 倍缩放(所以下面 luma/HDR 权重
// 公式里直接用 y 分量, 不需要再额外乘系数), YCoCgToRGB 对称地除回 4。YCoCg 把亮度和色度解耦,
// 邻域包围盒能收得更紧而不产生色偏(RGB 空间三通道各自 min/max 在色彩变化大的边缘容易出现
// 色散), 这是 UE 默认在 YCoCg 空间做裁剪/混合而不是直接用 RGB 的原因。
float3 rgbToYCoCg(float3 rgb)
{
    float y  = dot(rgb, float3(1, 2, 1));
    float co = dot(rgb, float3(2, 0, -2));
    float cg = dot(rgb, float3(-1, 2, -1));
    return float3(y, co, cg);
}

float3 yCoCgToRGB(float3 yCoCg)
{
    float y  = yCoCg.x * 0.25;
    float co = yCoCg.y * 0.25;
    float cg = yCoCg.z * 0.25;
    return float3(y + co - cg, y + cg, y - co - cg);
}

// UE HdrWeightY: 亮部权重更低, 用于压制 HDR 高光区域的萤火虫闪烁。exposureScale 用来抵消
// SceneColor 里已经烘焙进去的 preExposure(跟 UE View.OneOverPreExposure 是同一个目的)——
// 不这么做的话 "+4.0" 这个常数在不同曝光的场景下代表的相对亮度会漂移, 权重就不稳定。
float hdrWeight(float lumaY, float exposureScale)
{
    return 1.0 / (lumaY * exposureScale + 4.0);
}

// UE SetupSampleWeightParameters() 默认(r.TemporalAACatmullRom=0)用的高斯重建核:
// sigma = 0.47 * FilterSize, FilterSize 固定按 UE 默认值 1.0, 对应 exp(-0.5/sigma^2 * x^2)。
float gaussianSampleWeight(float2 pixelOffset)
{
    return exp(-2.29 * dot(pixelOffset, pixelOffset));
}

//------------------------------------------------------- 当前帧重建滤波

// jitter 让每一帧实际渲染的连续场景采样点相对像素中心有一个亚像素偏移, 单独看当前帧颜色仍然
// 是普通分辨率下的走样图像——只有配合这一步, 把 jitter 落点周围的像素按"离真实像素中心的距离"
// 加权平均, 重建出去抖动后的颜色, jitter 才真正贡献超采样质量, 而不只是让 history 有东西可比
// 对。等价于 UE TemporalAA.usf 的 FilterCurrentFrameInputSamples()。

static constexpr int2 kCrossOffsets[5] = { int2(0, -1), int2(-1, 0), int2(0, 0), int2(1, 0), int2(0, 1) };
static constexpr int2 kSquareOffsets[9] =
{
    int2(-1, -1), int2(0, -1), int2(1, -1),
    int2(-1,  0), int2(0,  0), int2(1,  0),
    int2(-1,  1), int2(0,  1), int2(1,  1),
};

float3 filterCurrentFrame5Cross(int2 coord, int2 maxCoord, float2 jitterPixels, float exposureScale)
{
    float3 accum = float3(0, 0, 0);
    float totalWeight = 0.0;
    for (uint i = 0; i < 5; ++i)
    {
        int2 sampleCoord = clamp(coord + kCrossOffsets[i], int2(0, 0), maxCoord);
        float3 sampleYCoCg = rgbToYCoCg(SceneColor[sampleCoord].rgb);
        float2 pixelOffset = float2(kCrossOffsets[i]) + jitterPixels;
        float weight = gaussianSampleWeight(pixelOffset) * hdrWeight(sampleYCoCg.x, exposureScale);
        accum += sampleYCoCg * weight;
        totalWeight += weight;
    }
    return accum / max(totalWeight, 1e-6);
}

float3 filterCurrentFrame3x3(int2 coord, int2 maxCoord, float2 jitterPixels, float exposureScale)
{
    float3 accum = float3(0, 0, 0);
    float totalWeight = 0.0;
    for (uint i = 0; i < 9; ++i)
    {
        int2 sampleCoord = clamp(coord + kSquareOffsets[i], int2(0, 0), maxCoord);
        float3 sampleYCoCg = rgbToYCoCg(SceneColor[sampleCoord].rgb);
        float2 pixelOffset = float2(kSquareOffsets[i]) + jitterPixels;
        float weight = gaussianSampleWeight(pixelOffset) * hdrWeight(sampleYCoCg.x, exposureScale);
        accum += sampleYCoCg * weight;
        totalWeight += weight;
    }
    return accum / max(totalWeight, 1e-6);
}

//------------------------------------------------------- 邻域颜色包围盒

// YCoCg 空间逐通道 min/max, 等价于 UE ComputeNeighborhoodBoundingbox() 在
// HISTORY_CLAMPING_BOX_MIN_MAX(Main pass 默认配置)下的行为。

void computeNeighborhoodBox5Cross(int2 coord, int2 maxCoord, out float3 boxMin, out float3 boxMax)
{
    boxMin = rgbToYCoCg(SceneColor[coord].rgb);
    boxMax = boxMin;
    for (uint i = 0; i < 5; ++i)
    {
        if (kCrossOffsets[i].x == 0 && kCrossOffsets[i].y == 0)
            continue;
        int2 sampleCoord = clamp(coord + kCrossOffsets[i], int2(0, 0), maxCoord);
        float3 sampleYCoCg = rgbToYCoCg(SceneColor[sampleCoord].rgb);
        boxMin = min(boxMin, sampleYCoCg);
        boxMax = max(boxMax, sampleYCoCg);
    }
}

void computeNeighborhoodBox3x3(int2 coord, int2 maxCoord, out float3 boxMin, out float3 boxMax)
{
    boxMin = rgbToYCoCg(SceneColor[coord].rgb);
    boxMax = boxMin;
    for (uint i = 0; i < 9; ++i)
    {
        if (kSquareOffsets[i].x == 0 && kSquareOffsets[i].y == 0)
            continue;
        int2 sampleCoord = clamp(coord + kSquareOffsets[i], int2(0, 0), maxCoord);
        float3 sampleYCoCg = rgbToYCoCg(SceneColor[sampleCoord].rgb);
        boxMin = min(boxMin, sampleYCoCg);
        boxMax = max(boxMax, sampleYCoCg);
    }
}

//------------------------------------------------------- 历史采样

// 经典 Catmull-Rom bicubic 的 5-tap 优化版(4x4=16 次纹理采样通过双线性插值合并成 5 次, 丢弃
// 权重通常很小的四个对角样本)。用来重建 history, 比双线性采样更锐利, 是 UE TAA 相比简单双线性
// history 明显更不容易糊的主要原因之一。
float3 sampleHistoryCatmullRom(float2 uv, float2 textureSize)
{
    float2 invTextureSize = 1.0 / textureSize;
    float2 samplePos = uv * textureSize;
    float2 texPos1 = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - texPos1;
    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float2 w0 = f2 - 0.5 * (f3 + f);
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    float2 w3 = 0.5 * (f3 - f2);
    float2 w2 = 1.0 - w0 - w1 - w3;

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;

    float2 texPos0  = (texPos1 - 1.0) * invTextureSize;
    float2 texPos3  = (texPos1 + 2.0) * invTextureSize;
    float2 texPos12 = (texPos1 + offset12) * invTextureSize;

    float3 result = float3(0, 0, 0);
    float totalWeight = 0.0;

    float weightN = w12.x * w0.y;
    result += History.SampleLevel(LinearClampSampler, float2(texPos12.x, texPos0.y), 0.0).rgb * weightN;
    totalWeight += weightN;

    float weightW = w0.x * w12.y;
    result += History.SampleLevel(LinearClampSampler, float2(texPos0.x, texPos12.y), 0.0).rgb * weightW;
    totalWeight += weightW;

    float weightC = w12.x * w12.y;
    result += History.SampleLevel(LinearClampSampler, float2(texPos12.x, texPos12.y), 0.0).rgb * weightC;
    totalWeight += weightC;

    float weightE = w3.x * w12.y;
    result += History.SampleLevel(LinearClampSampler, float2(texPos3.x, texPos12.y), 0.0).rgb * weightE;
    totalWeight += weightE;

    float weightS = w12.x * w3.y;
    result += History.SampleLevel(LinearClampSampler, float2(texPos12.x, texPos3.y), 0.0).rgb * weightS;
    totalWeight += weightS;

    return result / totalWeight;
}

// 无穷远处无视差, 只需重投影方向: 任意 depth 求出的射线方向相同, 转到上一帧视空间后以
// w=0(方向而非点)乘投影矩阵得到消失点, ndcZ 自动为 0, 与 reversed-Z 远平面约定一致。
float2 computeSkyVelocityNDC(float2 currentNDC, ViewData viewData)
{
    float4 posVS = mul(float4(currentNDC, 0.5, 1.0), viewData.clipToViewUnjittered);
    float3 dirVS = posVS.xyz / posVS.w;
    float3 dirWorld = mul(dirVS, transpose(viewData.worldToViewRot));
    float3 prevDirVS = mul(dirWorld, viewData.prevWorldToViewRot);
    float4 prevClip = mul(float4(prevDirVS, 0.0), viewData.prevViewToClipUnjittered);
    float2 prevNDC = prevClip.xy / prevClip.w;
    return currentNDC - prevNDC;
}

[shader("fragment")]
float4 FragmentMain(in VertexOutput input) : SV_Target
{
    uint width;
    uint height;
    SceneColor.GetDimensions(width, height);
    int2 maxCoord = int2(width - 1, height - 1);
    uint2 coord = min(uint2(input.texCoord * float2(width, height)), uint2(maxCoord));

    float3 currentColor = SceneColor[coord].rgb;

    // 第一次用 / 刚 resize / 刚被 CameraProxy::resetTAAHistory() 作废过, history 内容跟这一帧
    // 对不上, 直接输出当前帧
    if (PC.historyValid == 0)
        return float4(currentColor, 1.0);

    uint stencil = SceneStencil[coord];
    bool responsiveAA = (stencil & kTranslucentStencilBit) != 0;

    // velocity 是 Opaque.slang 算好的、已经去掉 jitter 的"真实"NDC 运动量(currentNDC 减
    // prevNDC, 两边都不带 jitter)——这里要找的是"这一帧的像素格子, 挪到上一帧的位置"对应的
    // history UV, 只需要把这个 NDC 增量按线性比例缩放成 UV 增量(NDC 跨度 2 对应 UV 跨度 1,
    // 所以缩放系数是 0.5; y 方向额外取负号, 跟这个引擎 NDC.y 和屏幕行方向相反的约定一致,
    // 见 Opaque.slang 里 uv->ndc 的写法), 直接从当前像素自己的 UV 上减掉——不能先把 velocity
    // 加回 currentNDC 拼出一个"绝对 NDC 坐标"再整体套用 uv<->ndc 的仿射变换, 那样会把
    // uv<->ndc 变换里的平移项也带上, 相当于把这一帧的 jitter 又反向叠了一次到重投影结果里,
    // 每帧抖动都不一样, 历史采样点跟着以亚像素幅度来回抖, 表现出来就是画面发糊 + 轻微抖动。
    // (currentPixelUV 本身是这个 pass 自己的输出网格坐标, 从头到尾没经过任何 jitter, 不需要,
    // 也不能再去减 jitterOffset。)
    float2 currentPixelUV = (float2(coord) + 0.5) / float2(width, height);

    // UE TemporalAA 的 AA_CROSS 路径：在当前深度邻域中寻找 reversed-Z 最大值，也就是离相机
    // 最近的表面，并使用该位置的 Velocity。这样前景轮廓附近不会因为中心采样落到背景而错误地
    // 重投影到背景历史。半透明本身不写深度/Velocity，也会使用其后方最近的不透明表面作为保守回退。
    int2 velocityCoord = int2(coord);
    float closestDepth = SceneDepth[coord];
    static constexpr int2 kDepthOffsets[4] = {
        int2(-2, -2),
        int2( 2, -2),
        int2(-2,  2),
        int2( 2,  2)
    };
    for (uint i = 0; i < 4; ++i)
    {
        int2 candidateCoord = clamp(int2(coord) + kDepthOffsets[i], int2(0, 0), maxCoord);
        float candidateDepth = SceneDepth[candidateCoord];
        if (candidateDepth > closestDepth)
        {
            closestDepth = candidateDepth;
            velocityCoord = candidateCoord;
        }
    }

    // closestDepth<=0 是 reversed-Z 远平面清屏值, 说明这块没有 Opaque.slang 写过 Velocity(天空/背景)。
    float2 currentPixelNDC = float2(currentPixelUV.x * 2.0 - 1.0, (1.0 - currentPixelUV.y) * 2.0 - 1.0);
    float2 velocityNDC = closestDepth > 0.0
        ? decodeVelocity(Velocity[velocityCoord]).xy
        : computeSkyVelocityNDC(currentPixelNDC, uViewData);
    // NaN 的比较结果为 false，Inf/异常大速度也会被这里拒绝，避免污染 historyUV 和 history。
    if (!all(abs(velocityNDC) < float2(4.0)))
        return float4(currentColor, 1.0);

    float2 velocityUV = velocityNDC * float2(0.5, -0.5);
    float2 historyUV = currentPixelUV - velocityUV;

    if (any(historyUV < 0.0) || any(historyUV > 1.0))
        return float4(currentColor, 1.0);

    // exposureScale 抵消 SceneColor/History 里已经烘焙进去的 preExposure, 让下面 HDR 权重公式
    // 里的 "+4.0" 常数在不同曝光场景下代表的相对亮度保持稳定, 等价于 UE 的 View.OneOverPreExposure。
    float exposureScale = uViewData.oneOverPreExposure;

    // 重建滤波要的是"这个像素实际采到的连续场景点, 相对像素中心的偏移", 不是"画面本身挪动了
    // 多少"——这两者正好差一个负号: uViewData.jitterOffset 让 clip.xy 加了 jitterOffsetNDC*clip.w,
    // 等价于整幅画面在 NDC 里挪动了 +jitterOffsetNDC, 挪动方向复用上面 velocity 那一套 NDC->UV
    // 的 (0.5,-0.5) 约定换算成像素单位就是画面挪动了 +jitterOffsetNDC*(0.5,-0.5)*(width,height)。
    // 但固定像素格子看到的是"挪动前处在(当前像素 - 这个挪动量)位置"的内容, 所以这个像素真正采样
    // 到的连续场景点相对它自己中心的偏移是这个挪动量的相反数, 下面要取负——不取负的话重建滤波会
    // 一直从抖动落点的镜像对侧邻居去拼颜色, 每帧都用错方向, 长期累积到 history 里, 静止画面也
    // 收敛不到正确结果。
    float2 jitterPixels = -uViewData.jitterOffset.xy * float2(0.5, -0.5) * float2(width, height);

    float3 historyColor = sampleHistoryCatmullRom(historyUV, float2(width, height));
    historyColor *= uViewData.preExposure / max(uViewData.prevPreExposure, 0.0001);
    float3 historyYCoCg = rgbToYCoCg(historyColor);

#if TAA_NEIGHBORHOOD_CROSS
    float3 filteredYCoCg = filterCurrentFrame5Cross(int2(coord), maxCoord, jitterPixels, exposureScale);
    float3 neighborMin, neighborMax;
    computeNeighborhoodBox5Cross(int2(coord), maxCoord, neighborMin, neighborMax);
#else
    float3 filteredYCoCg = filterCurrentFrame3x3(int2(coord), maxCoord, jitterPixels, exposureScale);
    float3 neighborMin, neighborMax;
    computeNeighborhoodBox3x3(int2(coord), maxCoord, neighborMin, neighborMax);
#endif

    // Save off luma of history before the clamp——UE 用裁剪前的 history 亮度算收敛保底项(见下面
    // blendFinal 的计算), 裁剪后的亮度已经被强行拉回邻域范围, 不能反映这个像素实际有多不稳定。
    float lumaHistoryPreClamp = historyYCoCg.x;

    // UE ClampHistory() 在 Main pass 默认配置(AA_CLIP=0)下用的是逐通道 clamp, 不是
    // clip-to-AABB(clip-to-AABB 是 TSR/TAACommon.ush 的做法)。
    float3 clampedHistoryYCoCg = clamp(historyYCoCg, neighborMin, neighborMax);

    float lumaFiltered = filteredYCoCg.x;

    // COMPUTE BLEND AMOUNT: UE 默认 r.TemporalAACurrentFrameWeight = 0.04, 随屏幕速度提高到 0.2
    // 上限, responsive 像素固定 1/4, 外加一个"至少给一点贡献"的收敛保底项, 防止低对比度区域因为
    // history/filtered 几乎相等而彻底停止收敛。
    // 阈值取 20 而非 UE 的 40: UE 的 Velocity 是 NDC 增量乘视口宽, 等于真实像素位移的两倍
    // (TemporalAA.usf:2139 "BackN is in units of 2pixels/viewportWidthInPixels"), 这里是真实像素。
    float velocityPixels = length(velocityUV * float2(width, height));
    float blendFinal = lerp(0.04, 0.2, saturate(velocityPixels / 20.0));
    blendFinal = max(blendFinal, saturate(0.01 * lumaHistoryPreClamp / (abs(lumaFiltered - lumaHistoryPreClamp) + 1e-6)));
    if (responsiveAA)
        blendFinal = 0.25;

    // DO FINAL BLEND: UE 的 luma-weighted blend, 不是普通线性 lerp——用 HDR 权重(亮部权重更低)
    // 分别加权 history 和 filtered 再混合, 抑制高光区域的闪烁/萤火虫。
    float historyWeight = hdrWeight(clampedHistoryYCoCg.x, exposureScale);
    float filterWeight = hdrWeight(lumaFiltered, exposureScale);
    float blendA = (1.0 - blendFinal) * historyWeight;
    float blendB = blendFinal * filterWeight;
    float invBlend = 1.0 / (blendA + blendB);
    blendA *= invBlend;
    blendB *= invBlend;

    float3 resolvedYCoCg = clampedHistoryYCoCg * blendA + filteredYCoCg * blendB;
    float3 resolved = yCoCgToRGB(resolvedYCoCg);

    // 把 NaN 和负色转黑, 沿用 UE AA_NAN 的写法: -min(-x, 0) 在 NaN 上比 max(x, 0) 更可靠。
    resolved = -min(-resolved, 0.0);

    return float4(resolved, 1.0);
}
