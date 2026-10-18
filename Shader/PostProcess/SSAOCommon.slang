#include "../Common/Binding.slang"
#include "../Common/GBufferEncoding.slang"

// 半分辨率/全分辨率 SSAO pass 的共享实现, 由外层薄壳文件(SSAOHalfRes.slang / SSAOFullRes.slang)
// 通过 #define USE_AO_SETUP_AS_INPUT / USE_UPSAMPLE 注入宏后 #include 这份文件——对应 UE
// PostProcessAmbientOcclusion.usf 的 MainPSandCS 在 AmbientOcclusionQuality=50(ShaderQuality=2,
// Levels=2)下实际展开出来的两个 permutation。所有行号引用均指 UnrealEngine 仓库的
// Engine/Shaders/Private/PostProcessAmbientOcclusion.usf 与 PostProcessAmbientOcclusionCommon.ush。
//
// 本引擎 view space 是右手系 -Z 朝前(CameraProxy::makePerspectiveMatrix: clip.w = -z_view),
// UE 是左手系 +Z 朝前。凡是从 GBuffer 解出来的 view space 法线, 在跟"伪 view space"里的量做点积
// 之前都要把 z 分量取反(见 viewSpaceNormal 的构造), 否则正对相机的表面自遮蔽判据会整个反号。

#ifndef USE_AO_SETUP_AS_INPUT
#define USE_AO_SETUP_AS_INPUT 1
#endif

#ifndef USE_UPSAMPLE
#define USE_UPSAMPLE 0
#endif

#if USE_AO_SETUP_AS_INPUT
[[vk::binding(0, 2)]]
Texture2D<float4> SSAOSetupTexture;

[[vk::binding(1, 2)]]
SamplerState PointClampSampler;

[[vk::binding(2, 2)]]
Texture2D<float2> SceneHZB;
#else
[[vk::binding(0, 2)]]
DepthTexture2D SceneDepth;

[[vk::binding(1, 2)]]
Texture2D<uint2> GBufferA;

[[vk::binding(2, 2)]]
SamplerState PointClampSampler;

[[vk::binding(3, 2)]]
Texture2D<float2> SceneHZB;

[[vk::binding(4, 2)]]
Texture2D<float4> SSAOSetupTexture;

[[vk::binding(5, 2)]]
Texture2D<float> SSAOHalfResAO;
#endif

struct SSAOConstants
{
    float radius;         // aoRadiusInShader, 已在 C++ 侧按 ScaleToFullRes/MipScale 算好
    float scaleToFullRes; // 半分辨率 pass = 2.0, 全分辨率 pass = 1.0
    uint hzbMipCount;
    uint padding0;
    float2 screenToHZBUVScale;
#if !USE_AO_SETUP_AS_INPUT
    float2 downsampledAOInvSize;
    float2 downsampledAOUVMin;
    float2 downsampledAOUVMax;
    float intensity;
    float power;
    float fadeDistance;
    float fadeRadius;
#endif
};
[[vk::push_constant]]
SSAOConstants PC;

static constexpr float kMaxSceneDepth = 1.0e4;
static constexpr uint kSampleSteps = 2;
static constexpr float kHzbStepMipLevelFactor = 0.5;

#if USE_AO_SETUP_AS_INPUT
// UE USE_SAMPLESET==3(usf:139-150), 半分辨率 pass 用的 6 点采样集
static constexpr uint kSampleCount = 6;
static constexpr float2 kOcclusionSampleOffsets[6] = {
    float2( 0.000,  0.200),
    float2( 0.325,  0.101),
    float2( 0.272, -0.396),
    float2(-0.385, -0.488),
    float2(-0.711,  0.274),
    float2( 0.060,  0.900)
};
#else
// UE USE_SAMPLESET==1(usf:119-127), 全分辨率 pass 用的 3 点采样集
static constexpr uint kSampleCount = 3;
static constexpr float2 kOcclusionSampleOffsets[3] = {
    float2( 0.000, -0.430),
    float2( 0.406,  0.5698),
    float2(-0.580,  0.814)
};

// UE ComputeUpsampleContribution 的 AO_UPSAMPLE_QUALITY==0 分支(ush:141-148), 4 抽头
static constexpr float2 kUpsampleOffsets[4] = {
    float2(-0.5,  0.5), float2(0.5,  0.5),
    float2(-0.5, -0.5), float2(0.5, -0.5)
};
#endif

// UE SSAORandomization(SystemTextures.cpp:794-850): 64x64 R8G8, 按 4x4 平铺, 只有 16 个离散
// 基向量。Reorder[16]={0,11,7,3,10,4,15,12,6,8,1,14,13,2,9,5}, w=Reorder[Pos], ww=w/16*PI,
// lenm=1-(sin(1.98*w)*0.5+0.5)*0.23, (c,s)=(cos,sin)(ww)*lenm, 再经 Quantize8SignedByte
// 8bit 量化往返(UnrealMathUtility.h:2415-2434)。这里直接摆成量化后的常量表。
static constexpr float2 kRotationBases[16] = {
    float2( 0.88235294,  0.00392157),
    float2(-0.47450980,  0.71764706),
    float2( 0.15294118,  0.75686275),
    float2( 0.76470588,  0.51372549),
    float2(-0.30196078,  0.73333333),
    float2( 0.54509804,  0.54509804),
    float2(-0.97647059,  0.19215686),
    float2(-0.70196078,  0.70196078),
    float2( 0.36470588,  0.88235294),
    float2( 0.00392157,  0.89803922),
    float2( 0.76470588,  0.15294118),
    float2(-0.76470588,  0.31764706),
    float2(-0.67843137,  0.45882353),
    float2( 0.89803922,  0.37254902),
    float2(-0.19215686,  0.96078431),
    float2( 0.52156863,  0.78039216)
};

// UE TemporalOffset = (SampleIndex % 8) * float2(2.48, 7.52) / 64(RandomizationSize),
// 点采样 + wrap 后落在整数 texel 偏移上, 再 %4 决定用 kRotationBases 的哪一项——离线算出
// frameIndex%8 对应的 8 个整数偏移, 避免运行时算 UV 再采样一整张贴图。
static constexpr int2 kTemporalTexelOffset[8] = {
    int2(0, 0), int2(2, 0), int2(1, 3), int2(3, 3),
    int2(2, 2), int2(0, 2), int2(3, 1), int2(1, 1)
};

// deviceZ -> 线性 view space 深度的闭式解, 见 SSAOSetup.slang 里同一个函数的说明。
float linearDepthFromDeviceZ(float deviceZ, in ViewData viewData)
{
    float m22 = viewData.viewToClip[2][2];
    float m32 = viewData.viewToClip[3][2];
    return min(m32 / max(deviceZ + m22, 1e-7), kMaxSceneDepth);
}

// UE ReconstructCSPos(ush:55-58): 返回的是被 FovFix 预先放大过的"伪 view space"
float3 reconstructCSPos(float sceneDepth, float2 screenPos)
{
    return float3(screenPos * sceneDepth, sceneDepth);
}

float computeDepthSimilarity(float depthA, float depthB, float tweakScale)
{
    return saturate(1.0 - abs(depthA - depthB) * tweakScale);
}

float square(float x)
{
    return x * x;
}

// UE GetHZBDepth(ush:33-38)。SceneHZB.r 存的是 Furthest(min-Z)通道的 device Z(HZBSeed.slang),
// 这里转成线性深度。SSAO 只用 Furthest, 不碰 .g(Closest)通道。
float getHZBLinearDepth(float2 screenPos, float mipLevel, in ViewData viewData)
{
    float2 uv = float2(screenPos.x * 0.5 + 0.5, 0.5 - screenPos.y * 0.5);
    float2 hzbUV = uv * PC.screenToHZBUVScale;
    float deviceZ = SceneHZB.SampleLevel(PointClampSampler, hzbUV, mipLevel).r;
    return linearDepthFromDeviceZ(deviceZ, viewData);
}

// UE WedgeWithNormal 在 OPTIMIZATION_O1(=1, 整份 usf 无条件写死)下实际编译出来的分支
// (ush:243-271): InvNormAngle 用 dot/dot, Weight 恒为 1——halo 拒绝那套完全在 #else 分支,
// 不实现。不对采样有效性做任何提前返回: 天空 tap 的深度被 clamp 到 kMaxSceneDepth 后,
// dot(delta,delta) 随之增大, dot/dot 的比值自然趋近 0, 和 UE 的无分支行为一致。
float3 wedgeWithNormal(
    float2 screenPosCenter, float2 localRandom, float3 invFovFix,
    float3 viewSpacePosition, float3 scaledViewSpaceNormal, float mipLevel, in ViewData viewData)
{
    float2 sL = screenPosCenter + localRandom;
    float2 sR = screenPosCenter - localRandom;

    float absL = getHZBLinearDepth(sL, mipLevel, viewData);
    float absR = getHZBLinearDepth(sR, mipLevel, viewData);

    float3 deltaL = (reconstructCSPos(absL, sL) - viewSpacePosition) * invFovFix;
    float3 deltaR = (reconstructCSPos(absR, sR) - viewSpacePosition) * invFovFix;

    float invNormAngleL = saturate(dot(deltaL, scaledViewSpaceNormal) / max(dot(deltaL, deltaL), 1e-12));
    float invNormAngleR = saturate(dot(deltaR, scaledViewSpaceNormal) / max(dot(deltaR, deltaR), 1e-12));
    return float3(invNormAngleL, invNormAngleR, 1.0);
}

// UE ComputeMipLevel(ush:372-385)。这套参数下(kHzbStepMipLevelFactor=0.5, scale<=1,
// samplePos<=(kSampleCount-0.5)/kSampleCount<1)结果恒为负, clamp 到 0——HZB 永远只采 mip 0,
// 是这套质量档位下的真实行为, 不是近似。
float computeMipLevel(uint sampleId, uint step)
{
    float samplePos = (float(sampleId) + 0.5) / float(kSampleCount);
    float scale = float(step + 1) / float(kSampleSteps);
    float mip = log2(kHzbStepMipLevelFactor * scale * samplePos);
    return clamp(mip, 0.0, float(PC.hzbMipCount - 1u));
}

// UE ReconstructNormal(ush:344-347): 从 xy 分量重建单位法线的 z
float3 reconstructNormalXY(float2 xy)
{
    return float3(xy, sqrt(saturate(1.0 - dot(xy, xy))));
}

// UE QUAD_MESSAGE_PASSING_BLUR==2(usf:452-497): 法线感知的 quad 内 3 抽头加权平均去抖动,
// ShaderQuality==2 下两个 AO pass 都要跑(QUAD_MESSAGE_PASSING_NORMAL=1, _DEPTH=0)。UE 用
// ddx_fine/ddy_fine, 这里改用 ddx/ddy(coarse)——这个引擎的 Slang 版本的 WGSL 后端不支持 fine
// 导数(error[E36107]), coarse 导数在 SPIRV/WGSL 两个 target 上都可用。
float applyQuadMessagePassingBlur(float ao, float sceneDepth, float3 viewSpaceNormal, float2 svPositionXY)
{
    float4 centerPixel = float4(ao, sceneDepth, normalize(viewSpaceNormal).xy);
    float4 dX = ddx(centerPixel);
    float4 dY = ddy(centerPixel);
    int2 quadMod = int2(uint2(svPositionXY) % 2u);

    float4 pixA = centerPixel;
    float4 pixB = centerPixel - dX * float(quadMod.x * 2 - 1);
    float4 pixC = centerPixel - dY * float(quadMod.y * 2 - 1);

    const float kNormalTweak = 4.0;
    float3 nA = reconstructNormalXY(pixA.zw);
    float3 nB = reconstructNormalXY(pixB.zw);
    float3 nC = reconstructNormalXY(pixC.zw);
    float weightA = 1.0;
    float weightB = saturate(pow(saturate(dot(nA, nB)), kNormalTweak));
    float weightC = saturate(pow(saturate(dot(nA, nC)), kNormalTweak));

    float invWeight = 1.0 / (weightA + weightB + weightC);
    return (weightA * pixA.x + weightB * pixB.x + weightC * pixC.x) * invWeight;
}

#if !USE_AO_SETUP_AS_INPUT
// UE ComputeUpsampleContribution(ush:138-206), AO_UPSAMPLE_QUALITY==0 分支(4 抽头)。
// Ret 只有 .r 被下游读取, 这里直接用标量累加, 不搬 UE 那个未使用的 .gb 通道。
float computeUpsampleContribution(float sceneDepth, float2 uv, float3 centerWorldNormal)
{
    float weightSum = 0.0001;
    float valueSum = 0.0001;
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        float2 tapUV = clamp(
            uv + kUpsampleOffsets[i] * PC.downsampledAOInvSize,
            PC.downsampledAOUVMin, PC.downsampledAOUVMax);
        float sampleValue = SSAOHalfResAO.SampleLevel(PointClampSampler, tapUV, 0.0);
        float4 normalAndDepth = SSAOSetupTexture.SampleLevel(PointClampSampler, tapUV, 0.0);

        float weight = computeDepthSimilarity(normalAndDepth.a, sceneDepth, 0.3);
        weight *= saturate(dot(normalAndDepth.xyz * 2.0 - 1.0, centerWorldNormal));

        valueSum += sampleValue * weight;
        weightSum += weight;
    }
    return valueSum / weightSum;
}
#endif

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

// CompilePassShader.cmake 按 [shader("...")] 字面文本(未展开 #include 前)判断一个 .slang 文件
// 声明了哪些 stage, 所以真正的 [shader(...)] 入口点必须落在 SSAOHalfRes.slang/SSAOFullRes.slang
// 自己的源码文本里, 不能藏在被它们 #include 的这份共享文件里——这里只提供不带 [shader(...)] 的
// 普通实现函数, 由两个薄壳文件各自的 VertexMain/FragmentMain 转调。
float4 vertexMainImpl(uint vertexID)
{
    return float4(kPositions[vertexID], 0, 1);
}

// 对应 UE MainPSandCS(usf:252-502)在 AO_SAMPLE_QUALITY==3、USE_NORMALS==1 分支下的实际逻辑。
float4 fragmentMainImpl(float4 svPosition)
{
    uint2 coord = uint2(svPosition.xy);

#if USE_AO_SETUP_AS_INPUT
    float4 setupSample = SSAOSetupTexture[coord];
    float sceneDepth = setupSample.a;
    float3 worldNormal = setupSample.xyz * 2.0 - 1.0;

    uint outputWidth, outputHeight;
    SSAOSetupTexture.GetDimensions(outputWidth, outputHeight);
    float2 uv = (float2(coord) + 0.5) / float2(outputWidth, outputHeight);
    bool isSky = false;
#else
    float deviceZ = SceneDepth[coord];
    // reversed-Z, 0 = far/天空, 没有几何体可供遮蔽——不能在这里 return: applyQuadMessagePassingBlur
    // 用 ddx/ddy 读相邻像素, WGSL 要求导数调用点必须在 uniform 控制流里, 数据相关(逐像素不同)的
    // 提前 return 会让它落进 non-uniform 分支, Dawn 编译期直接拒绝。改成打标记、把 deviceZ 钳到
    // 安全正值避免 linearDepthFromDeviceZ 出 Inf/NaN 污染到相邻像素的导数, 函数末尾统一处理
    bool isSky = deviceZ <= 0.0;
    float sceneDepth = linearDepthFromDeviceZ(max(deviceZ, 1e-6), uViewData);
    float3 worldNormal = decodeGBufferNormal(GBufferA[coord].x);

    float2 uv = (float2(coord) + 0.5) * uViewData.resolution.zw;
#endif

    if (PC.radius <= 0.0)
        return float4(1.0, 1.0, 1.0, 1.0);

    float3 n = mul(worldNormal, uViewData.worldToViewRot);
    float3 viewSpaceNormal = normalize(float3(n.x, n.y, -n.z)); // 手性修正, 见文件顶部说明

    float2 screenPos = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 viewSpacePosition = reconstructCSPos(sceneDepth, screenPos);

    // UE FovFix = (InvTanHalfFov, Ratio*InvTanHalfFov, 1)(usf:271), 就是投影矩阵对角线本身
    float3 fovFix = float3(uViewData.viewToClip[0][0], uViewData.viewToClip[1][1], 1.0);
    float3 invFovFix = 1.0 / fovFix;

    // UE ViewSpacePosition += (AmbientOcclusionBias/1000) * SceneDepth * ScaleFactor *
    // (ViewSpaceNormal * FovFix)(usf:286-289)。AmbientOcclusionBias/1000 是无量纲比值,
    // 跟厘米/米单位差无关; ScaleFactor 是 AO 工作分辨率相对全分辨率的降采样倍数。
    viewSpacePosition += (3.0 / 1000.0) * sceneDepth * PC.scaleToFullRes * (viewSpaceNormal * fovFix);

    // UE ScaledViewSpaceNormal, OPTIMIZATION_O1 + AmbientOcclusionRadiusInWS=false 分支
    // (usf:338-340): ViewSpaceNormal *= 0.08 * lerp(SceneDepth, 1000, 0) = 0.08 * SceneDepth
    float3 scaledViewSpaceNormal = viewSpaceNormal * (0.08 * sceneDepth);

    // AORadiusInShader 的 view-space 相对半径模式(usf:283): ActualAORadius = radius * SceneDepth
    float actualAORadius = PC.radius * sceneDepth;

    int2 tileCoord = int2(coord) + kTemporalTexelOffset[uFrameData.frameIndex % 8u];
    uint rotationIndex = uint(tileCoord.x & 3) + uint(tileCoord.y & 3) * 4u;
    float2 randomVec = kRotationBases[rotationIndex] * actualAORadius;

    // UE: FovFixXY = FovFix.xy * (1/ViewSpacePosition.z), 用加完 bias 之后的 z(usf:323);
    // screenSpacePos 也是加完 bias 后重新投影出来的, 不是入口的 screenPos(usf:326)。
    float2 fovFixXY = fovFix.xy * (1.0 / viewSpacePosition.z);
    float4 randomBase = float4(randomVec, -randomVec.y, randomVec.x) * float4(fovFixXY, fovFixXY);
    float2 screenSpacePos = viewSpacePosition.xy / viewSpacePosition.z;

    float2 weightAccumulator = float2(0.0001, 0.0001);
    [unroll]
    for (uint i = 0; i < kSampleCount; ++i)
    {
        float2 unrotatedRandom = kOcclusionSampleOffsets[i];
        float2 localRandom = unrotatedRandom.x * randomBase.xy + unrotatedRandom.y * randomBase.zw;

        float3 localAccumulator = float3(0.0, 0.0, 0.0);
        [unroll]
        for (uint step = 0; step < kSampleSteps; ++step)
        {
            float scale = float(step + 1) / float(kSampleSteps);
            float mipLevel = computeMipLevel(i, step);
            float3 stepSample = wedgeWithNormal(
                screenSpacePos, scale * localRandom, invFovFix,
                viewSpacePosition, scaledViewSpaceNormal, mipLevel, uViewData);
            localAccumulator = lerp(
                localAccumulator, float3(max(localAccumulator.xy, stepSample.xy), 1.0), stepSample.z);
        }
        weightAccumulator += float2(square(1.0 - localAccumulator.x) * localAccumulator.z, localAccumulator.z);
        weightAccumulator += float2(square(1.0 - localAccumulator.y) * localAccumulator.z, localAccumulator.z);
    }

    float ao = weightAccumulator.x / weightAccumulator.y;

#if USE_UPSAMPLE
    // UE ComputeLerpFactor(ush:209-227): AO_SAMPLE_QUALITY!=0 且 USE_UPSAMPLE==1 时恒为
    // AmbientOcclusionMipBlend=0.6
    float filtered = computeUpsampleContribution(sceneDepth, uv, worldNormal);
    ao = lerp(ao, filtered, 0.6);
#endif

#if !USE_AO_SETUP_AS_INPUT
    // 仅全分辨率 pass 做(usf:426-440): 先按距离淡出到 1, 再套用户的 Power/Intensity 曲线
    if (PC.fadeDistance > 0.0)
    {
        float fadeRadius = max(PC.fadeRadius, 0.01);
        float mul = 1.0 / fadeRadius;
        float add = -(PC.fadeDistance - fadeRadius) * mul;
        ao = lerp(ao, 1.0, saturate(sceneDepth * mul + add));
    }
    ao = 1.0 - (1.0 - pow(abs(ao), PC.power)) * PC.intensity;
#endif

    ao = applyQuadMessagePassingBlur(ao, sceneDepth, viewSpaceNormal, svPosition.xy);

    // isSky 的覆盖放在导数调用之后——分支本身仍然是逐像素发散的, 但发生在 ddx/ddy 已经算完之后,
    // 不影响前面的 uniform 控制流要求
    if (isSky)
        ao = 1.0;

    return float4(saturate(ao), saturate(ao), saturate(ao), saturate(ao));
}
