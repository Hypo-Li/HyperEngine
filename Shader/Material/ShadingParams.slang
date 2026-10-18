#pragma once
#include "../Common/GBufferEncoding.slang"
#include "../Common/MaterialIO.slang"

struct ShadingParams
{
    float3 normalWS;
    float3 viewDirWS;
    float3 albedo;      // baseColor * (1 - metallic)
    float3 F0;
    float roughness;    // 已钳到 >= 0.045
    float occlusion;

    ShadingModel shadingModel;
    bool hasAnisotropy;
    float3 anisotropyTangentWS; // 只有 hasAnisotropy 时有意义, 已经 orthogonalize 过
    float anisotropyStrength;   // 0..1 无符号, 只有 hasAnisotropy 时有意义

    float clearCoat;            // 只有 shadingModel==ShadingModel::Clear_Coat 时有意义
    float clearCoatRoughness;
    float3 clearCoatNormalWS;

    float3 fuzzColor;            // 只有 shadingModel==ShadingModel::Cloth 时有意义
    float clothAmount;
};

// 输入是已经采样好的 GBuffer 原始 texel 值(不是纹理句柄)——两个消费 pass 各自的纹理声明/
// binding index 不一样, 采样这一步没法共享, 能共享的是"采样完之后怎么解码成材质数据"这一段
ShadingParams decodeShadingParams(uint2 gbufferA, float4 gbufferB, float4 gbufferC, float4 gbufferD, float3 viewDirWS)
{
    ShadingParams sp;
    sp.normalWS = decodeGBufferNormal(gbufferA.x);
    sp.viewDirWS = viewDirWS;

    float metallic = gbufferB.x;
    float specular = gbufferB.z;
    sp.roughness = max(gbufferB.y, 0.045);
    sp.albedo = gbufferC.xyz * (1.0 - metallic);
    sp.occlusion = gbufferC.w;
    float3 dielectricF0 = float3(0.16 * specular * specular);
    sp.F0 = lerp(dielectricF0, gbufferC.xyz, metallic);

    unpackShadingModelByte(gbufferB.w, sp.shadingModel, sp.hasAnisotropy);

    sp.anisotropyTangentWS = float3(0, 0, 0);
    sp.anisotropyStrength = 0.0;
    if (sp.hasAnisotropy)
    {
        float3 rawTangent; float strength;
        decodeAnisotropy(gbufferA.y, rawTangent, strength);
        sp.anisotropyTangentWS = orthogonalizeTangent(rawTangent, sp.normalWS);
        sp.anisotropyStrength = strength;
    }

    sp.clearCoat = 0.0;
    sp.clearCoatRoughness = 0.0;
    sp.clearCoatNormalWS = sp.normalWS;
    if (sp.shadingModel == ShadingModel::Clear_Coat)
    {
        sp.clearCoat = gbufferD.x;
        sp.clearCoatRoughness = max(gbufferD.y, 0.045);
        sp.clearCoatNormalWS = decodeClearCoatNormalDelta(gbufferD.zw, sp.normalWS);
        // f0ClearCoatToSurface() 近似的是 F0->IOR->F0 往返变换, 只对电介质有物理意义——金属的
        // F0 是复折射率决定的反射率颜色, 不适用这个变换, 所以只重映射电介质基线, 再按 metallic
        // 插值混回金属色, 而不是对已经跟 baseColor 混合过的 sp.F0 整体重映射
        float3 remappedDielectricF0 = lerp(dielectricF0, f0ClearCoatToSurface(dielectricF0), sp.clearCoat);
        sp.F0 = lerp(remappedDielectricF0, gbufferC.xyz, metallic);
    }

    sp.fuzzColor = float3(0.0);
    sp.clothAmount = 0.0;
    if (sp.shadingModel == ShadingModel::Cloth)
    {
        sp.fuzzColor = gbufferD.rgb * gbufferD.rgb;
        sp.clothAmount = gbufferD.a;
    }
    return sp;
}
