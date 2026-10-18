#include <Common/Binding.slang>
#include <Common/GBufferEncoding.slang>

// C++ 侧的 ShadowMaskPass.cpp 按同样的字段顺序/宽度填这个结构体上传, 两边各自定义、不共享头文件
// (和这个 pass 里其它资源绑定的做法一致)。级联标量用 float4 而不是 float[4], 是为了避开
// 常量缓冲区里标量数组元素默认按 16 字节对齐/padding 的坑。8 级标量按两组 float4 打包。
static const uint kMaxCascadeCount = 8;
static const uint kCascadeVectorCount = (kMaxCascadeCount + 3) / 4;
static const uint kCascadeAtlasColumnCount = 4;

struct ShadowMaskParameters
{
    float4x4 viewToLightClip[kMaxCascadeCount];
    float4 renderSplitFar[kCascadeVectorCount];
    float4 depthBias[kCascadeVectorCount]; // 每级 cascade 的常数 bias, 复用作软过渡宽度的倒数基准
    // 非末级在 [fadeStart, fadeEnd] 混合到下一张有效的 cascade；末级在同一区间淡到全亮。
    float4 fadeStart[kCascadeVectorCount];
    float4 fadeEnd[kCascadeVectorCount];
    float3 lightDirectionWS;// 光线传播方向(不是"指向光源"的方向, 用的时候要取负, 和 SceneProxy.cpp
                             // 喂给大气的太阳方向是同一个约定)
    float receiverBias;     // 已经是 "1 - 原始 ReceiverBias" 的形式(照 UE GetShaderReceiverDepthBias()
                             // 的语义), 不是原始的用户可调常数
    uint cascadeCount;
    uint cascadeResolution;
    uint shadowFilterQuality; // 对应 ShadowFilterQuality: 0=None, 1=PCF_1x1, 2=PCF_3x3, 3=PCF_5x5
    uint padding0;
};

[[vk::binding(0, 2)]]
DepthTexture2D SceneDepthImage;

[[vk::binding(1, 2)]]
DepthTexture2D ShadowDepthImage;

[[vk::binding(2, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(3, 2)]]
ConstantBuffer<ShadowMaskParameters> Parameters;

[[vk::binding(4, 2)]]
SamplerState ShadowPointClampSampler;

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

[shader("vertex")]
float4 VertexMain(in uint vertexID : SV_VertexID) : SV_Position
{
    return float4(kPositions[vertexID], 0, 1);
}

float getRenderSplitFar(uint cascadeIndex)
{
    return Parameters.renderSplitFar[cascadeIndex / 4][cascadeIndex % 4];
}

float getDepthBias(uint cascadeIndex)
{
    return Parameters.depthBias[cascadeIndex / 4][cascadeIndex % 4];
}

float getFadeStart(uint cascadeIndex)
{
    return Parameters.fadeStart[cascadeIndex / 4][cascadeIndex % 4];
}

float getFadeEnd(uint cascadeIndex)
{
    return Parameters.fadeEnd[cascadeIndex / 4][cascadeIndex % 4];
}

// Each gather is aligned to a non-overlapping 2x2 texel block. Clamp the block base inside the
// cascade tile so the hardware gather can never read a neighbouring cascade in the horizontal atlas.
float4 gatherShadowDepth(uint cascadeIndex, int2 localBlockBase)
{
    int maxBlockBase = max(int(Parameters.cascadeResolution) - 2, 0);
    localBlockBase = clamp(localBlockBase, int2(0, 0), int2(maxBlockBase, maxBlockBase));

    uint atlasColumnCount = min(Parameters.cascadeCount, kCascadeAtlasColumnCount);
    uint atlasRowCount = (Parameters.cascadeCount + kCascadeAtlasColumnCount - 1) / kCascadeAtlasColumnCount;
    uint2 atlasExtent = Parameters.cascadeResolution * uint2(atlasColumnCount, atlasRowCount);
    uint2 atlasBlockBase = uint2(
        (cascadeIndex % kCascadeAtlasColumnCount) * Parameters.cascadeResolution + uint(localBlockBase.x),
        (cascadeIndex / kCascadeAtlasColumnCount) * Parameters.cascadeResolution + uint(localBlockBase.y)
    );
    // An integer texel boundary lies at (base + 1) / extent and gathers exactly base/base+1.
    float2 gatherUV = (float2(atlasBlockBase) + 1.0) / float2(atlasExtent);
    return ShadowDepthImage.Gather(ShadowPointClampSampler, gatherUV);
}

// 单个 tap 的软比较(不是硬 0/1 二值): 照 UE ShadowProjectionPixelShader.usf 的 TransitionScale
// 用法, 差值超过一个 bias 宽度才算彻底遮挡, 中间自然形成一条渐变。PCF 多 tap 平均之后, 空间上的
// 软化(减少锯齿)和这条深度渐变(减少 acne/阶梯感)是两件独立生效的事, 叠加起来效果更好。
float compareShadowDepth(float shadowDepth, float compareDepth, float transitionScale)
{
    float shadowFactor = saturate((compareDepth - shadowDepth) * transitionScale);
    return 1.0 - shadowFactor;
}

float4 compareShadowDepth(float4 shadowDepth, float compareDepth, float transitionScale)
{
    return 1.0 - saturate((compareDepth - shadowDepth) * transitionScale);
}

// Gather the (2*radius+2)^2 footprint as non-overlapping 2x2 blocks. This preserves the previous
// continuous tent weights while reducing 4/16/36/64 scalar texture reads to 1/4/9/16 gathers.
float samplePCFLinear(uint cascadeIndex, float2 cascadeUV, float compareDepth, float transitionScale, int radius)
{
    float2 texelPos = cascadeUV * float(Parameters.cascadeResolution) - 0.5;
    float2 fraction = frac(texelPos);
    int2 baseCoord = int2(floor(texelPos));

    float sum = 0.0;
    float weightSum = 0.0;
    int minOffset = -radius;
    int maxOffset = radius + 1;
    for (int blockY = 0; blockY <= radius; ++blockY)
    {
        int y0 = minOffset + blockY * 2;
        int y1 = y0 + 1;
        float wy0 = (y0 == minOffset) ? (1.0 - fraction.y) : 1.0;
        float wy1 = (y1 == maxOffset) ? fraction.y : 1.0;

        for (int blockX = 0; blockX <= radius; ++blockX)
        {
            int x0 = minOffset + blockX * 2;
            int x1 = x0 + 1;
            float wx0 = (x0 == minOffset) ? (1.0 - fraction.x) : 1.0;
            float wx1 = (x1 == maxOffset) ? fraction.x : 1.0;

            float4 visibility = compareShadowDepth(
                gatherShadowDepth(cascadeIndex, baseCoord + int2(x0, y0)),
                compareDepth,
                transitionScale
            );
            // HLSL/Slang gather order is x=lower-left, y=lower-right,
            // z=upper-right, w=upper-left for this texel-space convention.
            float4 weights = float4(
                wx0 * wy1,
                wx1 * wy1,
                wx1 * wy0,
                wx0 * wy0
            );
            sum += dot(visibility, weights);
            weightSum += dot(weights, 1.0);
        }
    }
    return sum / weightSum;
}

float sampleShadowPoint(uint cascadeIndex, float2 cascadeUV, float compareDepth, float transitionScale)
{
    int maxCoord = max(int(Parameters.cascadeResolution) - 1, 0);
    int2 localCoord = clamp(
        int2(floor(cascadeUV * float(Parameters.cascadeResolution))),
        int2(0, 0),
        int2(maxCoord, maxCoord)
    );
    uint2 atlasCoord = uint2(
        (cascadeIndex % kCascadeAtlasColumnCount) * Parameters.cascadeResolution + uint(localCoord.x),
        (cascadeIndex / kCascadeAtlasColumnCount) * Parameters.cascadeResolution + uint(localCoord.y)
    );
    return compareShadowDepth(ShadowDepthImage[atlasCoord].r, compareDepth, transitionScale);
}

// Parameters.shadowFilterQuality 是整个 draw call 统一的一致值(不是逐像素变化的数据), 这里的
// 分支是一致性分支, 不会有 warp/wave 内的发散开销。半径按 ShadowFilterQuality 命名对应的
// 滤波核宽度换算: PCF_1x1(半径0, 2x2 双线性)/PCF_3x3(半径1)/PCF_5x5(半径2)。
float sampleShadowFiltered(uint cascadeIndex, float2 cascadeUV, float compareDepth, float transitionScale)
{
    if (Parameters.shadowFilterQuality == 0u)
        return sampleShadowPoint(cascadeIndex, cascadeUV, compareDepth, transitionScale);

    int radius = 0;
    switch (Parameters.shadowFilterQuality)
    {
    case 2u: radius = 1; break;
    case 3u: radius = 2; break;
    default: radius = 0; break;
    }
    return samplePCFLinear(cascadeIndex, cascadeUV, compareDepth, transitionScale, radius);
}

// 把主相机 view space 坐标投影进某一级 cascade 的阴影深度 atlas 采样出可见度。落在这一级阴影
// 贴图范围外时按"全亮"处理, 不直接让调用方退出整个 shader——级联过渡/距离淡出还需要拿这个结果
// 去和另一侧混合, 不能在这里就短路掉。
float sampleCascadeVisibility(uint cascadeIndex, float4 fragPosVS, float NoL)
{
    float4 lightClipPos = mul(fragPosVS, Parameters.viewToLightClip[cascadeIndex]);
    lightClipPos.xyz /= lightClipPos.w;

    float2 cascadeUV = float2(lightClipPos.x * 0.5 + 0.5, 1.0 - (lightClipPos.y * 0.5 + 0.5));
    if (any(cascadeUV < 0.0) || any(cascadeUV > 1.0))
    {
        return 1.0;
    }

    // receiver 掠射角越大(NoL 越接近 0)过渡带按 receiverBias 放得越宽, 掩盖掠射角本身深度
    // 噪声更大的问题。
    float transitionScale = (1.0 / max(getDepthBias(cascadeIndex), 1e-6)) * lerp(Parameters.receiverBias, 1.0, NoL);
    return sampleShadowFiltered(cascadeIndex, cascadeUV, lightClipPos.z, transitionScale);
}

[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    float sceneDepth = SceneDepthImage[uint2(svPosition.xy)].r;
    if (sceneDepth == 0)
    {
        return float4(1, 1, 1, 1);
    }

    float2 uv = svPosition.xy / uViewData.resolution.xy;
    float4 fragPosCS = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, sceneDepth, 1);
    float4 fragPosVS = mul(fragPosCS, uViewData.clipToView);
    fragPosVS *= 1.0 / fragPosVS.w;
    float viewSpaceDepth = -fragPosVS.z;

    int cascadeIndex = -1;
    for (uint i = 0; i < Parameters.cascadeCount; ++i)
    {
        if (viewSpaceDepth <= getRenderSplitFar(i))
        {
            cascadeIndex = int(i);
            break;
        }
    }
    if (cascadeIndex < 0)
    {
        return float4(1, 1, 1, 1);
    }

    float3 normalWS = decodeGBufferNormal(GbufferAImage[uint2(svPosition.xy)].x);
    float NoL = saturate(dot(normalWS, -Parameters.lightDirectionWS));

    float visibility = sampleCascadeVisibility(uint(cascadeIndex), fragPosVS, NoL);

    // 过渡/淡出区间: [fadeStart, fadeEnd]。非末级在两张都有效的 overlap 内混到下一级；
    // 末级则在阴影距离末端混到全亮。
    float fadeStart = getFadeStart(uint(cascadeIndex));
    float fadeEnd = getFadeEnd(uint(cascadeIndex));
    if (viewSpaceDepth > fadeStart)
    {
        float blend = saturate((viewSpaceDepth - fadeStart) / max(fadeEnd - fadeStart, 1e-6));
        uint nextCascadeIndex = uint(cascadeIndex) + 1u;
        if (nextCascadeIndex < Parameters.cascadeCount)
        {
            float nextVisibility = sampleCascadeVisibility(nextCascadeIndex, fragPosVS, NoL);
            visibility = lerp(visibility, nextVisibility, blend);
        }
        else
        {
            visibility = lerp(visibility, 1.0, blend);
        }
    }

    return float4(visibility, visibility, visibility, 1);
}
