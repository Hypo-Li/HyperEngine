#include "../Common/Binding.slang"
#include "../Common/GBufferEncoding.slang"

[[vk::binding(0, 2)]]
DepthTexture2D SceneDepth;

[[vk::binding(1, 2)]]
Texture2D<uint2> GBufferA;

struct SSAOSetupConstants
{
    float thresholdInverse;
    float padding0;
    float padding1;
    float padding2;
};
[[vk::push_constant]]
SSAOSetupConstants PC;

// UE 深度存的是厘米制且用 Constant_Float16F_Scale 压进 fp16 上限, 这个引擎是米制, 深度数值本来
// 就小 100 倍, 直接存线性深度会跌进 fp16 非规格化区, 所以这里不做那个缩放, clamp 到一个足够大的
// 有限值防止天空写进 +Inf。
static constexpr float kMaxSceneDepth = 1.0e4;

// deviceZ -> 线性 view space 深度的闭式解: 本引擎透视投影矩阵(CameraProxy::makePerspectiveMatrix)
// 满足 clip.w = -z_view, clip.z = m22*z_view + m32, 令 depth = -z_view 得 deviceZ = m32/depth - m22。
// m22/m32 不受 TAA jitter 影响(jitter 只改 [2][0]/[2][1]), 用 viewToClip(而非 Unjittered)等价。
float linearDepthFromDeviceZ(float deviceZ, in ViewData viewData)
{
    float m22 = viewData.viewToClip[2][2];
    float m32 = viewData.viewToClip[3][2];
    return min(m32 / max(deviceZ + m22, 1e-7), kMaxSceneDepth);
}

// UE ComputeDepthSimilarity(ush:61-64)
float computeDepthSimilarity(float depthA, float depthB, float tweakScale)
{
    return saturate(1.0 - abs(depthA - depthB) * tweakScale);
}

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

// 对应 UE MainSetupPS(usf:200-249): 把全分辨率法线/深度降采样到半分辨率, 供半分辨率 AO pass
// 当作深度/法线源使用。本引擎用整数 texel 2x2 box 采样(而不是 UE 连续 UV 采样), 数学上是同一件
// 事——UE 的 4 个 UV offset 在这个降采样比例下正好落在 2x2 邻域的四个 texel 上。
[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    uint2 outputCoord = uint2(svPosition.xy);
    uint2 baseCoord = outputCoord * 2u;

    uint fullWidth, fullHeight;
    SceneDepth.GetDimensions(fullWidth, fullHeight);
    uint2 maxCoord = uint2(fullWidth - 1u, fullHeight - 1u);

    uint2 c00 = min(baseCoord, maxCoord);
    uint2 c10 = min(baseCoord + uint2(1u, 0u), maxCoord);
    uint2 c01 = min(baseCoord + uint2(0u, 1u), maxCoord);
    uint2 c11 = min(baseCoord + uint2(1u, 1u), maxCoord);

    // oct 编码不是线性映射, 不能像旧的 xyz*2-1 编码那样先平均编码值再统一解码一次(那样做在
    // xyz*2-1 下刚好是等价的仿射变换, 换成 oct 之后不再等价)——这里改成先把 4 个角的法线各自
    // 解码成方向, 再做加权平均。
    float3 n00 = decodeGBufferNormal(GBufferA[c00].x);
    float3 n10 = decodeGBufferNormal(GBufferA[c10].x);
    float3 n01 = decodeGBufferNormal(GBufferA[c01].x);
    float3 n11 = decodeGBufferNormal(GBufferA[c11].x);

    float a00 = linearDepthFromDeviceZ(SceneDepth[c00], uViewData);
    float a10 = linearDepthFromDeviceZ(SceneDepth[c10], uViewData);
    float a01 = linearDepthFromDeviceZ(SceneDepth[c01], uViewData);
    float a11 = linearDepthFromDeviceZ(SceneDepth[c11], uViewData);

    float maxZ = max(max(a00, a10), max(a01, a11));

    float4 avgColor = float4(0.0001, 0.0001, 0.0001, 0.0001);
    avgColor += float4(n00, 1.0) * computeDepthSimilarity(a00, maxZ, PC.thresholdInverse);
    avgColor += float4(n10, 1.0) * computeDepthSimilarity(a10, maxZ, PC.thresholdInverse);
    avgColor += float4(n01, 1.0) * computeDepthSimilarity(a01, maxZ, PC.thresholdInverse);
    avgColor += float4(n11, 1.0) * computeDepthSimilarity(a11, maxZ, PC.thresholdInverse);
    avgColor.rgb /= avgColor.w;

    // SSAOSetupTexture 自己的编码约定(线性 xyz*2-1, 不是 oct)保持不变, 消费端见
    // SSAOCommon.slang 里对这张纹理的解码
    return float4(normalize(avgColor.rgb) * 0.5 + 0.5, maxZ);
}
