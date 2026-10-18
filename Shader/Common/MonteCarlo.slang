// Radiosity Trace/ConvertToSH 用到的蒙特卡洛采样工具——跟 UE Engine/Shaders/Private/
// MonteCarlo.ush、RandomPCG.ush 逐值核对一致(UE checkout 行号见各函数注释)。
#pragma once
#include "Math.slang"

// [Frisvad 2012, "Building an Orthonormal Basis from a 3D Unit Vector Without Normalization"]
// UE MonteCarlo.ush:27-42。TangentZ.z < -0.9999999 处有不连续, 跟 UE 一致(未处理奇异点前
// 再往下退化会有精度问题, UE 也是直接接受这个已知的窄条件分支)
float3x3 getTangentBasisFrisvad(float3 tangentZ)
{
    float3 tangentX, tangentY;
    if (tangentZ.z < -0.9999999)
    {
        tangentX = float3(0, -1, 0);
        tangentY = float3(-1, 0, 0);
    }
    else
    {
        float a = 1.0 / (1.0 + tangentZ.z);
        float b = -tangentZ.x * tangentZ.y * a;
        tangentX = float3(1.0 - tangentZ.x * tangentZ.x * a, b, -tangentZ.x);
        tangentY = float3(b, 1.0 - tangentZ.y * tangentZ.y * a, -tangentZ.y);
    }
    return float3x3(tangentX, tangentY, tangentZ);
}

// 均匀半球采样, E 是 [0,1)^2 的两个随机数。PDF = 1/(2π)——跟 UE MonteCarlo.ush 里
// "PDF = 1 / (2 * PI)" 那个 UniformSampleHemisphere 一致(不是余弦加权的版本)
float4 uniformSampleHemisphere(float2 e)
{
    float phi = 2.0 * PI * e.x;
    float cosTheta = e.y;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 h;
    h.x = sinTheta * cos(phi);
    h.y = sinTheta * sin(phi);
    h.z = cosTheta;

    return float4(h, 1.0 / (2.0 * PI));
}

// 32 位整体位反转——不用内建 reversebits(), 跟 CubemapPrefilter.slang::hammersley() 同一个
// 理由: WGSL 后端对这个内建函数没有把握, 手写移位网络两边都保真
uint reverseBits32(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return bits;
}

// 16 位量化版 Hammersley——用于样本数很少(比如按帧循环的时域抖动, NumSamples=4)的场合,
// Random 是外部提供的去相关种子。UE MonteCarlo.ush:65-70
float2 hammersley16(uint index, uint numSamples, uint2 random)
{
    float e1 = frac(float(index) / float(numSamples) + float(random.x) * (1.0 / 65536.0));
    float e2 = float((reverseBits32(index) >> 16u) ^ random.y) * (1.0 / 65536.0);
    return float2(e1, e2);
}

// 3D 整数坐标 -> 3 个独立的随机数(每个只有高 16 位是充分打乱的, 见返回值 >>16u)。
// UE RandomPCG.ush:12-44, Feistel 型 PCG 变体, 逐步核对过常数一致
uint3 rand3DPCG16(int3 p)
{
    uint3 v = uint3(p);
    v = v * 1664525u + 1013904223u;

    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;

    return v >> 16u;
}

// 16 位随机数 -> [0,1), UE RandomPCG.ush:107-110
float rand16ToFloat(uint rand16Bits)
{
    return float(rand16Bits) * (1.0 / 65536.0);
}

// [Duff et al. 2017, "Building an Orthonormal Basis, Revisited"]。跟 getTangentBasisFrisvad 不是
// 同一个算法——GGX 反射光线采样、Resolve 邻域重建、粗糙镜面采样统一用这个(UE MC.ush:13-23)
float3x3 getTangentBasis(float3 tangentZ)
{
    float sign = tangentZ.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + tangentZ.z);
    float b = tangentZ.x * tangentZ.y * a;

    float3 tangentX = float3(1.0 + sign * a * pow2(tangentZ.x), sign * b, -sign * tangentZ.x);
    float3 tangentY = float3(b, sign + a * pow2(tangentZ.y), -tangentZ.y);
    return float3x3(tangentX, tangentY, tangentZ);
}

// [Shirley & Chiu 1997] 方形到圆盘的等面积映射, UE MC.ush:114-134(ConcentricDiskSamplingHelper +
// UniformSampleDiskConcentric),Resolve 和空间滤波的邻域偏移采样用
float2 uniformSampleDiskConcentric(float2 e)
{
    float2 p = 2.0 * e - 0.99999994;
    float2 a = abs(p);
    float lo = min(a.x, a.y);
    float hi = max(a.x, a.y);
    float epsilon = 5.42101086243e-20;
    float phi = (PI / 4.0) * (lo / (hi + epsilon) + 2.0 * float(a.y >= a.x));

    const uint signMask = 0x80000000u;
    float diskX = asfloat((asuint(cos(phi)) & ~signMask) | (asuint(p.x) & signMask));
    float diskY = asfloat((asuint(sin(phi)) & ~signMask) | (asuint(p.y) & signMask));
    return float2(diskX, diskY) * hi;
}

// GGX 可见法线分布的采样 PDF(各向异性),k 项是有界 VNDF 采样的修正(Eto & Tokuyoshi 2023,
// "Bounded VNDF Sampling for Smith-GGX Reflections")——UE 默认打开(GGX_BOUNDED_VNDF_SAMPLING=1),
// 这里直接固定用有界版本,不做开关。UE MC.ush:416-438
float visibleGGXPDFAniso(float3 v, float3 h, float2 alpha)
{
    float noV = v.z;
    float noH = h.z;
    float voH = dot(v, h);
    float a2 = alpha.x * alpha.y;
    float3 hs = float3(alpha.y * h.x, alpha.x * h.y, a2 * noH);
    float s = dot(hs, hs);
    float d = (1.0 / PI) * a2 * pow2(a2 / s);
    float lenV = length(float3(v.x * alpha.x, v.y * alpha.y, noV));

    float boundedAlpha = saturate(min(alpha.x, alpha.y));
    float boundS = 1.0 + length(v.xy);
    float ka2 = boundedAlpha * boundedAlpha;
    float s2 = boundS * boundS;
    float k = (s2 - ka2 * s2) / (s2 + ka2 * v.z * v.z);

    return (2.0 * d * voH) / (k * noV + lenV);
}

// [Dupuy & Benyoub 2023, "Sampling Visible GGX Normals with Spherical Caps"] + 有界版本(见
// visibleGGXPDFAniso 注释)。e 是 [0,1)^2 的随机数,alpha 是切线/副切线方向粗糙度(各向同性时
// alpha.x == alpha.y),v 是切空间视线方向(+Z 为法线)。UE MC.ush:442-472
float4 importanceSampleVisibleGGX(float2 e, float2 alpha, float3 v)
{
    float3 vh = normalize(float3(alpha * v.xy, v.z));

    float phi = (2.0 * PI) * e.x;
    float boundedAlpha = saturate(min(alpha.x, alpha.y));
    float boundS = 1.0 + length(v.xy);
    float a2 = boundedAlpha * boundedAlpha;
    float s2 = boundS * boundS;
    float k = (s2 - a2 * s2) / (s2 + a2 * v.z * v.z);

    float z = lerp(1.0, -k * vh.z, e.y);
    float sinTheta = sqrt(saturate(1.0 - z * z));
    float x = sinTheta * cos(phi);
    float y = sinTheta * sin(phi);
    float3 h = float3(x, y, z) + vh;

    h = normalize(float3(alpha * h.xy, max(0.0, h.z)));
    return float4(h, visibleGGXPDFAniso(v, h, alpha));
}
