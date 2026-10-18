#include "../Common/Binding.slang"

[[vk::binding(0, 2)]]
DepthTexture2D SceneDepth;

[[vk::binding(1, 2)]]
SamplerState HZBPointClampSampler;

[[vk::binding(2, 2)]]
Texture2D<float2> SceneHZB;

[[vk::binding(3, 2)]]
[format("r8")]
WTexture2D<float> AORawImage;

struct AOGenerateConstants
{
    float radius;
    float fadeDistance;
    float fadeRadius;
    uint quality;
    uint hzbMipCount;
    uint padding0; // keeps screenToHZBUVScale at offset 24 under WGSL's std430 rules too
    float2 screenToHZBUVScale;
};
[[vk::push_constant]]
AOGenerateConstants PC;

// GTAO(Ground Truth AO, Jimenez et al. 2016, "Practical Realtime Strategies for Accurate
// Indirect Occlusion")的水平角积分方法, 对照 UE Engine/Shaders/Private/PostProcessAmbientOcclusion.usf
// 里 SearchForLargestAngleDual_HZB / ComputeInnerIntegral / GTAOCombinedPSandCS 三个函数的
// 实际公式重新实现(不是照抄代码, 是按同一套数学关系用这个引擎自己的写法重写)。slice 数量
// 和 thickness 系数对应 UE 默认值，step 数量及 HZB bias 由质量档位选择。
static constexpr uint kSliceCount = 2;
static constexpr uint kMaxStepsPerSlice = 20;
static constexpr float kPI = 3.14159265;
static constexpr float kHalfPI = kPI * 0.5;
static constexpr float kTwoOverPI = 2.0 / kPI;
static constexpr float kMaxPixelScreenRadius = 256.0;
// UE 的 r.GTAO.ThicknessBlend 默认输入是 0.5, 上传 shader 前会变换成
// clamp(1 - value^2, 0, 0.99), 所以 shader 实际使用的是 0.75。
static constexpr float kThicknessBlend = 0.75;
// 对应 UE GetGTAOShaderParameters() 的 temporal sampling 序列。方向以 PI 为周期,
// 所以 UE 使用 degrees * PI/360, 得到下面这 6 个半角；径向 offset 每 6 帧切换一次。
static constexpr float kTemporalAngles[6] = {
    kPI / 6.0, 5.0 * kPI / 6.0, kPI / 2.0,
    2.0 * kPI / 3.0, kPI / 3.0, 0.0
};
static constexpr float kTemporalOffsets[4] = { 0.1, 0.6, 0.35, 0.85 };

float interleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

uint getStepCount(uint quality)
{
    switch (quality)
    {
    case 0u: return 4u;
    case 1u: return 6u;
    case 3u: return 12u;
    case 4u: return 20u;
    default: return 8u;
    }
}

uint getBaseHZBMip(uint quality)
{
    if (quality == 0u)
        return 2u;
    if (quality == 1u)
        return 1u;
    return 0u;
}

// UV(0..1, y 向下)+ 该像素的 SceneDepth -> view space 位置, 复用 SkyLighting.slang 里同一套
// clip->view 重建公式
float3 reconstructViewPos(float2 uv, float depth, in ViewData viewData)
{
    float4 posCS = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 posVS = mul(posCS, viewData.clipToView);
    return posVS.xyz / posVS.w;
}

// 取绝对值更小的单侧差分作为局部斜率, 深度不连续时自动靠向没有跳变的一侧。
float takeSmallerAbsDelta(float left, float mid, float right)
{
    float a = mid - left;
    float b = right - mid;
    return (abs(a) < abs(b)) ? a : b;
}

float3 deriveNormalFromDepth(
    int2 coord,
    float2 uv,
    uint width,
    uint height,
    float centerDepth,
    float3 centerPosVS,
    in ViewData viewData)
{
    int2 maxCoord = int2(width - 1u, height - 1u);
    int2 leftCoord = max(coord - int2(1, 0), int2(0, 0));
    int2 rightCoord = min(coord + int2(1, 0), maxCoord);
    int2 topCoord = max(coord - int2(0, 1), int2(0, 0));
    int2 bottomCoord = min(coord + int2(0, 1), maxCoord);

    float leftDepth = SceneDepth[leftCoord];
    float rightDepth = SceneDepth[rightCoord];
    float topDepth = SceneDepth[topCoord];
    float bottomDepth = SceneDepth[bottomCoord];

    float ddx = takeSmallerAbsDelta(leftDepth, centerDepth, rightDepth);
    float ddy = takeSmallerAbsDelta(topDepth, centerDepth, bottomDepth);

    // 外推深度只取决于靠谱一侧的斜率, 不会被背景/无效深度污染
    float2 invExtent = 1.0 / float2(width, height);
    float2 rightUV = uv + float2(invExtent.x, 0.0);
    float2 bottomUV = uv + float2(0.0, invExtent.y);

    float3 right = reconstructViewPos(rightUV, centerDepth + ddx, viewData) - centerPosVS;
    float3 down = reconstructViewPos(bottomUV, centerDepth + ddy, viewData) - centerPosVS;

    return normalize(cross(right, down));
}

// view space 位置 -> UV, 是上面重建公式的逆变换, 用来把"半径"这个世界空间量换算成这个像素在
// 屏幕上对应的步长
float2 projectToUV(float3 posVS, in ViewData viewData, out bool valid)
{
    float4 posCS = mul(float4(posVS, 1.0), viewData.viewToClip);
    valid = posCS.w > 0.0;
    posCS.xyz /= posCS.w;
    return float2(posCS.x * 0.5 + 0.5, 1.0 - (posCS.y * 0.5 + 0.5));
}

// 沿 screenDir2D 方向(screenDir2D 已经按 UV 尺度缩放到"半径对应的满步长")找地平线: 累积
// dot(采样方向, viewVec) 的最大值(离 viewVec 越近夹角余弦越大, 代表这个方向上离相机更"正对"、
// 遮挡更多), 配合按距离的 falloff(超出 radius 后不再让远处样本推进已经找到的地平线)。
// 对应 UE SearchForLargestAngleDual_HZB 的核心循环。
float searchHorizon(
    float2 uv,
    float2 screenDirUVPerPixel,
    float3 posVS,
    float3 viewVec,
    float attenFactor,
    float stepRadiusPixels,
    float initialOffset,
    uint stepCount,
    uint baseHZBMip,
    uint width,
    uint height,
    in ViewData viewData
)
{
    float bestCos = -1.0;
    for (uint step = 0; step < kMaxStepsPerSlice; ++step)
    {
        if (step >= stepCount)
            break;

        uint tapIndex = step + 1u;
        float fi = float(tapIndex);
        // 对应 UE SearchForLargestAngleDual: InitialOffset 只平移整条规则采样序列；同时至少
        // 每一步前进一个像素, 避免远处很小的屏幕半径让多个 tap 重复读取中心 texel。
        float sampleDistancePixels = max(stepRadiusPixels * (fi + initialOffset), fi + 1.0);
        float2 sampleUV = uv + screenDirUVPerPixel * sampleDistancePixels;

        // UE Quality 0 uses base HZB mip bias 2, then raises tap 2 to mip 3 and
        // tap 4 to mip 4. HZB mip 0 already represents a 2x2 SceneDepth footprint.
        uint mipLevel = baseHZBMip;
        if (tapIndex == 2u)
            mipLevel += 1u;
        if (tapIndex > 3u)
            mipLevel += 2u;
        mipLevel = min(mipLevel, PC.hzbMipCount - 1u);

        float2 hzbUV = sampleUV * PC.screenToHZBUVScale;
        float sampleDepth = SceneHZB.SampleLevel(
            HZBPointClampSampler, hzbUV, float(mipLevel)).r;
        if (sampleDepth <= 0.0)
            continue;

        // HZB depth represents an area rather than an exact full-resolution texel.
        // Reconstruct it on the requested ray, matching UE's horizon-search convention.
        float3 samplePosVS = reconstructViewPos(sampleUV, sampleDepth, viewData);
        float3 delta = samplePosVS - posVS;
        float lenSq = dot(delta, delta);
        float invLen = rsqrt(lenSq + 1e-8); // UE 的 0.0001 是厘米制, 这里按米制换算(×0.01²)

        float ang = dot(delta, viewVec) * invLen;
        float falloff = saturate(lenSq * attenFactor);
        ang = lerp(ang, bestCos, falloff);
        bestCos = (ang > bestCos) ? ang : lerp(ang, bestCos, kThicknessBlend);
    }
    return bestCos;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    AORawImage.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
        return;

    uint2 coord = dispatchThreadID.xy;
    float sceneDepth = SceneDepth[coord];

    // reversed-Z, 0 = far/天空, 没有几何体可供遮蔽
    if (sceneDepth <= 0.0)
    {
        AORawImage.Store(coord, 1.0);
        return;
    }

    float2 uv = (float2(coord) + 0.5) / float2(width, height);
    float3 posVS = reconstructViewPos(uv, sceneDepth, uViewData);
    // 从表面指向相机——后面所有夹角都是相对这个方向量的
    float3 viewVec = normalize(-posVS);

    float3 normalVS = deriveNormalFromDepth(
        int2(coord), uv, width, height, sceneDepth, posVS, uViewData);
    if (dot(normalVS, viewVec) < 0.0)
        normalVS = -normalVS;

    if (PC.radius <= 0.0)
    {
        AORawImage.Store(coord, 1.0);
        return;
    }

    // UE AttenFactor = 2 / radius^2, 配合 searchHorizon 里 saturate(lenSq * attenFactor) 的
    // falloff, 在约 0.707*radius 处衰减到 0 权重, 而不是在 radius 处才骤降到 0
    float attenFactor = 2.0 / (PC.radius * PC.radius);

    // UE 使用投影后的像素半径, 再除以 NumTaps+1 得到规则步长；不是让最后一个 tap 直接
    // 落到完整世界半径上。限制最大屏幕半径也避免近景产生跨越整屏的离散采样。
    bool radiusValid;
    float2 radiusEndUV = projectToUV(posVS + float3(PC.radius, 0.0, 0.0), uViewData, radiusValid);
    if (!radiusValid)
    {
        AORawImage.Store(coord, 1.0);
        return;
    }
    // NDC -> UV 会带入 0.5；UE 的 GTAO 搜索半径使用完整的 viewport * projectionScale，
    // 因此这里乘回 2，才能让相同世界空间半径得到相同的屏幕 footprint。
    float pixelRadius = abs(radiusEndUV.x - uv.x) * float(width) * 2.0;
    uint stepCount = getStepCount(PC.quality);
    uint baseHZBMip = getBaseHZBMip(PC.quality);
    pixelRadius = clamp(pixelRadius, float(stepCount), kMaxPixelScreenRadius);
    float stepRadiusPixels = pixelRadius / float(stepCount + 1u);

    // 对应 UE GetRandomVector + GetGTAOShaderParameters: IGN 决定像素自身的空间方向和 4 相
    // 径向 offset；frameIndex 再按 6 个角度旋转方向, 并每 6 帧切换一次径向 phase。
    // 6*4=24 帧覆盖完整序列, 交给后续 AOTemporalFilter 跨帧收敛。
    float2 noiseCoord = float2(float(coord.x), 16384.0 - float(coord.y));
    uint temporalFrame = uFrameData.frameIndex;
    float temporalAngle = kTemporalAngles[temporalFrame % 6u];
    float temporalOffset = kTemporalOffsets[(temporalFrame / 6u) % 4u] * 0.25;
    float basePhi = interleavedGradientNoise(noiseCoord) * kPI + temporalAngle;
    uint offsetPhase = (0u - coord.y - coord.x) & 3u;
    float initialOffset = frac(float(offsetPhase) * 0.25 + temporalOffset);

    float distanceFade = 0.0;
    if (PC.fadeDistance > 0.0)
    {
        float fadeRadius = max(PC.fadeRadius, 0.0);
        float fadeStart = max(PC.fadeDistance - fadeRadius, 0.0);
        distanceFade = fadeRadius > 0.0001
            ? saturate((abs(posVS.z) - fadeStart) / fadeRadius)
            : float(abs(posVS.z) >= PC.fadeDistance);
    }

    float visibilitySum = 0.0;
    for (uint slice = 0; slice < kSliceCount; ++slice)
    {
        float sliceOffset = frac(initialOffset + float(slice) * 0.617);
        float phi = basePhi + float(slice) * (kPI / float(kSliceCount));
        float cosPhi = cos(phi);
        float sinPhi = sin(phi);
        float3 sliceDirVS = float3(cosPhi, sinPhi, 0.0);
        // UE 在积分中使用 view-space 的 (cosPhi,sinPhi), 在纹理采样时把 Y 翻转并按像素尺寸
        // 换成 UV。这样 search plane 与实际屏幕方向严格对应。
        float2 screenDirUVPerPixel = float2(cosPhi / float(width), -sinPhi / float(height));

        // 切面(过像素、包含 viewVec 的一个平面)的法线: 直接用 (cosPhi, sinPhi, 0) 跟 viewVec
        // 叉乘——不需要先把它相对 viewVec 做 Gram-Schmidt 正交化, cross() 本身就会丢掉沿 viewVec
        // 的分量。perp 是切面内垂直于 viewVec 的那根轴, 用来定义法线夹角 gamma 的零点。
        float3 planeNormal = normalize(cross(sliceDirVS, viewVec));
        float3 perp = cross(viewVec, planeNormal);
        float3 projNormal = normalVS - planeNormal * dot(normalVS, planeNormal);
        float lenProjNormal = length(projNormal) + 1e-6;
        float recipMag = 1.0 / lenProjNormal;

        float cosAng = dot(projNormal, perp) * recipMag;
        float gamma = acos(clamp(cosAng, -1.0, 1.0)) - kHalfPI;
        float cosGamma = dot(projNormal, viewVec) * recipMag;
        // sin(gamma) = sin(acos(cosAng) - PI/2) = -cosAng, 这里的 2 提前乘进去, 后面直接用
        // 不用再乘 2
        float sinGamma = cosAng * -2.0;

        float bestCosPos = searchHorizon(
            uv, screenDirUVPerPixel, posVS, viewVec, attenFactor,
            stepRadiusPixels, sliceOffset, stepCount, baseHZBMip,
            width, height, uViewData);
        float bestCosNeg = searchHorizon(
            uv, -screenDirUVPerPixel, posVS, viewVec, attenFactor,
            stepRadiusPixels, sliceOffset, stepCount, baseHZBMip,
            width, height, uViewData);

        float h0raw = acos(clamp(bestCosPos, -1.0, 1.0));
        float h1raw = acos(clamp(bestCosNeg, -1.0, 1.0));

        // 把地平线夹角相对 gamma(法线在这个切面内的夹角)钳到法线自己的半球范围内——不能被
        // "遮挡"到比表面法线本身的地平线更低的地方去, 那部分本来就已经是自遮蔽, 不该重复计入
        float angle0 = gamma + max(-h0raw - gamma, -kHalfPI);
        float angle1 = gamma + min(h1raw - gamma, kHalfPI);

        float innerIntegral = lenProjNormal * 0.25 * (
            (angle0 * sinGamma + cosGamma - cos(2.0 * angle0 - gamma)) +
            (angle1 * sinGamma + cosGamma - cos(2.0 * angle1 - gamma))
        );

        visibilitySum += innerIntegral;
    }

    // UE 的 Generate/Horizon 输出使用 2/PI 归一化, 随后的 Spatial Filter 再乘 PI/2 后应用
    // 强度。为了让 intensity=0 的最终结果仍为 1, 这里的无 AO 基线也必须是 2/PI。
    float normalizedVisibility = saturate(visibilitySum / float(kSliceCount)) * kTwoOverPI;
    // Intensity / Power 与 UE 一样留到 spatial filter 恢复 [0,1] 后统一应用。
    float ao = saturate(lerp(normalizedVisibility, 1.0, distanceFade));
    AORawImage.Store(coord, ao);
}
