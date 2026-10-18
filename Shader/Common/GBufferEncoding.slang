#pragma once
#include "MaterialIO.slang"

// ---- 标准 oct 编码(带 fold), 输入输出都是 [0,1] 归一化浮点值 ----
float2 octEncode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 oct = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * (n.xy >= 0.0 ? float2(1, 1) : float2(-1, -1));
    return oct * 0.5 + 0.5;
}
float3 octDecode(float2 f)
{
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = saturate(-n.z);
    n.xy += n.xy >= 0.0 ? -t : t;
    return normalize(n);
}

// ---- GBufferA.r: 世界空间法线, oct 编码, 16 bit/分量, 整个装进一个 uint32 ----
uint encodeGBufferNormal(float3 worldNormal)
{
    float2 oct = octEncode(worldNormal);
    uint x16 = uint(round(oct.x * 65535.0));
    uint y16 = uint(round(oct.y * 65535.0));
    return (x16 << 16) | y16;
}
float3 decodeGBufferNormal(uint r)
{
    float2 oct = float2(r >> 16, r & 0xFFFFu) / 65535.0;
    return octDecode(oct);
}

// ---- GBufferA.g: 各向异性切线方向(oct, 12 bit/分量)+ 强度(8 bit), 整个装进一个 uint32 ----
// 32 bit 布局: [octX(12) | octY(12) | strength(8)]
// 强度存的是无符号量(0..1), 符号在编码时被主动折进方向——anisotropy 取负等价于把切线方向转
// 90°(叉乘法线得到副切线方向), 这是精确的数学恒等(D_GGXaniso/Vis_SmithJointAniso 在同时
// 交换 ax<->ay 和 X<->Y 轴时值不变), 不是近似
uint encodeAnisotropy(float3 anisotropyTangentWS, float3 normalWS, float strength)
{
    float3 orthoTangent = normalize(anisotropyTangentWS - normalWS * dot(normalWS, anisotropyTangentWS));
    float3 tangentToEncode = strength >= 0.0 ? orthoTangent : cross(normalWS, orthoTangent);
    float2 oct = octEncode(tangentToEncode);

    uint octX12 = uint(round(saturate(oct.x) * 4095.0));
    uint octY12 = uint(round(saturate(oct.y) * 4095.0));
    uint strength8 = uint(round(saturate(abs(strength)) * 255.0));

    return (octX12 << 20) | (octY12 << 8) | strength8;
}

// 输出: rawTangentWS(未投影, 需要调用方再用 orthogonalizeTangent 修正), strength(0..1 无符号)
void decodeAnisotropy(uint g, out float3 rawTangentWS, out float strength)
{
    uint octX12 = g >> 20;
    uint octY12 = (g >> 8) & 0xFFFu;
    uint strength8 = g & 0xFFu;

    float2 oct = float2(octX12, octY12) / 4095.0;
    rawTangentWS = octDecode(oct);
    strength = float(strength8) / 255.0;
}

// 读回来的方向不保证严格垂直于解码出来的法线(oct 量化误差), 强制 Gram-Schmidt 投影修正
float3 orthogonalizeTangent(float3 rawTangent, float3 normal)
{
    return normalize(rawTangent - normal * dot(normal, rawTangent));
}

// ---- GBufferB.a: shading model(低 4 bit) + 各向异性标志(bit 4) ----
uint packShadingModelByte(ShadingModel shadingModel, bool hasAnisotropy)
{
    return (uint(shadingModel) & 0xFu) | (hasAnisotropy ? 0x10u : 0u);
}
void unpackShadingModelByte(float encoded, out ShadingModel shadingModel, out bool hasAnisotropy)
{
    uint packed = uint(round(encoded * 255.0));
    shadingModel = ShadingModel(packed & 0xFu);
    hasAnisotropy = (packed & 0x10u) != 0u;
}

// ---- GBufferD.zw: 清漆法线相对 GBufferA 基础法线的偏差 ----
// 基础法线和清漆法线各自独立 oct 编码, 存两组 2D oct 坐标的差, 缩放+偏移打进 [0,1]。解码时
// 基础法线已经从 gbufferA.r 解出来了, 现算一次 octEncode(baseNormalWS) 就能反解出清漆法线的
// oct 坐标——全程只在 2D oct 坐标空间里做加减, 不需要任何 3D 切线基, 光照 pass 没有稳定切线
// 可用这个问题天然不存在。
// bias 用 128/255 而不是理想的 0.5——GBufferD 是 RGBA8_UNORM, 0.5 卡在 127.5/255 两格正中间会被舍入, decode 端仍用 0.5 相减就会在 delta≈0 时引入误差, 128/255 是精确格点, 能保证 delta=0 时无损往返
static const float2 kClearCoatDeltaBias = float2(128.0 / 255.0, 128.0 / 255.0);
static const float kClearCoatDeltaScale = 0.5;

float2 encodeClearCoatNormalDelta(float3 clearCoatNormalWS, float3 baseNormalWS)
{
    float2 oct1 = octEncode(baseNormalWS);
    float2 oct2 = octEncode(clearCoatNormalWS);
    return (oct1 - oct2) * kClearCoatDeltaScale + kClearCoatDeltaBias;
}
float3 decodeClearCoatNormalDelta(float2 zw, float3 baseNormalWS)
{
    float2 oct1 = octEncode(baseNormalWS);
    float2 delta = (zw - kClearCoatDeltaBias) / kClearCoatDeltaScale;
    float2 oct2 = oct1 - delta;
    return octDecode(oct2);
}

// 清漆层假设 IOR=1.5, 把基础层的 F0 从"空气→基础层"重新映射成"空气→清漆(1.5)→基础层"这个
// 界面下的等效值——多项式来自 Filament surface_material.fs 的 f0ClearCoatToSurface()
// (FILAMENT_QUALITY_HIGH 分支), 是 iorTof0(f0ToIor(F0), 1.5) 的近似
float3 f0ClearCoatToSurface(float3 F0)
{
    return saturate(F0 * (F0 * (0.941892 - 0.263008 * F0) + 0.346479) - 0.0285998);
}

// ---- Velocity: RGBA16_UNORM, 跟 UE EncodeVelocityToTexture/DecodeVelocityFromTexture 一致(不含 gamma 编码和 pixel animation 标志位) ----
// xy 是 NDC 位移映射到 [0,1], zw 是 deviceZ 位移 float32 的高/低 16 位
static const float kVelocityXYScale = 0.499 * 0.5;
static const float kVelocityXYBias = 32767.0 / 65535.0;

float4 encodeVelocity(float3 velocity)
{
    float4 encoded;
    encoded.xy = velocity.xy * kVelocityXYScale + kVelocityXYBias;
    uint z = asuint(velocity.z);
    encoded.z = saturate(float((z >> 16) & 0xFFFFu) / 65535.0 + 0.1 / 65535.0);
    encoded.w = saturate(float(z & 0xFFFFu) / 65535.0 + 0.1 / 65535.0);
    return encoded;
}

// 写入过的 texel 的 x 恒大于 0(|velocity.x| < 2), 所以 x == 0 就是没写过 velocity 的清屏值
float3 decodeVelocity(float4 encoded)
{
    if (encoded.x == 0.0)
        return float3(0.0, 0.0, 0.0);

    float3 velocity;
    velocity.xy = (encoded.xy - kVelocityXYBias) / kVelocityXYScale;
    velocity.z = asfloat((uint(round(encoded.z * 65535.0)) << 16) | uint(round(encoded.w * 65535.0)));
    return velocity;
}
