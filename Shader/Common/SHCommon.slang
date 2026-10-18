// 2 阶(4 系数, L0+L1)和 3 阶(9 系数)球谐工具函数——跟 UE Engine/Shaders/Private/SHCommon.ush 的
// FTwoBandSHVector 版 SHBasisFunction()/CalcDiffuseTransferSH()/DotSH() 逐值核对一致
// (UE checkout: SHCommon.ush:221-229, 275-289, 143-147), 3 阶版对应 SHBasisFunction3()/
// CalcDiffuseTransferSH3()/DotSH3()(:232-249, 291-316, 177-192)。常量/符号/xyz 顺序跟
// SHCoeffGather.slang 已经在用的前 4 项(L0/L1)也完全一致, 两边共用同一份公式。
#pragma once
#include "Math.slang"

// 分量顺序: (常数项, Y1-1 沿 y, Y10 沿 z, Y11 沿 x) —— 跟 UE 一致, 不是随意排列
float4 shBasisFunction(float3 dir)
{
    return float4(
        0.2820947918,
        -0.4886025119 * dir.y,
        0.4886025119 * dir.z,
        -0.4886025119 * dir.x
    );
}

// max(0, cosθ) 卷积后的 SH——是法线的函数(卷积核本身朝向法线), 不是常量:
// shBasisFunction(normal) 再按每阶缩放系数(L0=2π/(1+1)=π, L1=2π/(2+1)=2π/3, Exponent=1
// 的 Lambertian 情形)缩放
float4 calcDiffuseTransferSH(float3 normal)
{
    static const float kL0 = PI;
    static const float kL1 = 2.0 * PI / 3.0;
    float4 result = shBasisFunction(normal);
    result.x *= kL0;
    result.yzw *= kL1;
    return result;
}

// R/G/B 三通道各自的 SH 投影结果跟 transferSH 做点积, 得到辐照度
float3 dotSH(float4 shR, float4 shG, float4 shB, float4 transferSH)
{
    return float3(dot(shR, transferSH), dot(shG, transferSH), dot(shB, transferSH));
}

// 分量顺序跟 UE FThreeBandSHVector 一致: v0 = L0 + L1 的 3 项, v1 = L2 的前 4 项, v2 = L2 的最后 1 项
struct ThreeBandSH
{
    float4 v0;
    float4 v1;
    float v2;
};

struct ThreeBandSHRGB
{
    ThreeBandSH r;
    ThreeBandSH g;
    ThreeBandSH b;
};

ThreeBandSH zeroSH3()
{
    ThreeBandSH result;
    result.v0 = float4(0.0, 0.0, 0.0, 0.0);
    result.v1 = float4(0.0, 0.0, 0.0, 0.0);
    result.v2 = 0.0;
    return result;
}

ThreeBandSH shBasisFunction3(float3 dir)
{
    ThreeBandSH result;
    result.v0 = shBasisFunction(dir);

    float3 dirSquared = dir * dir;
    result.v1.x = 1.092548 * dir.x * dir.y;
    result.v1.y = -1.092548 * dir.y * dir.z;
    result.v1.z = 0.315392 * (3.0 * dirSquared.z - 1.0);
    result.v1.w = -1.092548 * dir.x * dir.z;
    result.v2 = 0.546274 * (dirSquared.x - dirSquared.y);
    return result;
}

// Exponent = 1 时 UE 的各阶缩放系数: L0 = 2π/2, L1 = 2π/3, L2 = 2π/(3+4+1)
ThreeBandSH calcDiffuseTransferSH3(float3 normal)
{
    static const float kL0 = PI;
    static const float kL1 = 2.0 * PI / 3.0;
    static const float kL2 = 2.0 * PI / 8.0;
    ThreeBandSH result = shBasisFunction3(normal);
    result.v0.x *= kL0;
    result.v0.yzw *= kL1;
    result.v1 *= kL2;
    result.v2 *= kL2;
    return result;
}

ThreeBandSHRGB mulSH3(ThreeBandSH a, float3 color)
{
    ThreeBandSHRGB result;
    result.r.v0 = a.v0 * color.r;
    result.r.v1 = a.v1 * color.r;
    result.r.v2 = a.v2 * color.r;
    result.g.v0 = a.v0 * color.g;
    result.g.v1 = a.v1 * color.g;
    result.g.v2 = a.v2 * color.g;
    result.b.v0 = a.v0 * color.b;
    result.b.v1 = a.v1 * color.b;
    result.b.v2 = a.v2 * color.b;
    return result;
}

ThreeBandSH addSH3(ThreeBandSH a, ThreeBandSH b)
{
    ThreeBandSH result;
    result.v0 = a.v0 + b.v0;
    result.v1 = a.v1 + b.v1;
    result.v2 = a.v2 + b.v2;
    return result;
}

ThreeBandSHRGB addSH3(ThreeBandSHRGB a, ThreeBandSHRGB b)
{
    ThreeBandSHRGB result;
    result.r = addSH3(a.r, b.r);
    result.g = addSH3(a.g, b.g);
    result.b = addSH3(a.b, b.b);
    return result;
}

float dotSH3(ThreeBandSH a, ThreeBandSH b)
{
    return dot(a.v0, b.v0) + dot(a.v1, b.v1) + a.v2 * b.v2;
}

float3 dotSH3(ThreeBandSHRGB a, ThreeBandSH b)
{
    return float3(dotSH3(a.r, b), dotSH3(a.g, b), dotSH3(a.b, b));
}
