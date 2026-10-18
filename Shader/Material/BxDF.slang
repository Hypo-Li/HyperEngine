#pragma once
#include "../Common/Math.slang"
#include "ShadingParams.slang"

struct BxDFData
{
    float NoL;
    float NoV;

};

float3 F_Schlick(float VoH, float3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - VoH, 0.0), 5.0);
}

float3 F_Schlick(float VoH, float3 F0, float3 F90)
{
    return F0 + (F90 - F0) * pow(max(1.0 - VoH, 0.0), 5.0);
}

float D_GGX(float NoH, float a2)
{
    float d = (NoH * NoH) * (a2 - 1.0) + 1.0;
    // 不对输出值设上限——调用方必须先把 roughness 钳到一个非零下限(比如 0.045)再算出这里的
    // a2, 从源头保证这个函数不会病态地冲到极高的值。事后硬 clamp 输出值会在 NoH 的函数图像上
    // 削出一个一阶导数不连续的"平台边界", 在光源/相机/表面法线接近共线的对称场景下会被连成
    // 一圈肉眼可见的光圈, 具体分析见 DirectLighting.slang 里同名函数的注释。
    return max(a2 / (PI * d * d), 0.0);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a2)
{
    float GGXV = NoL * sqrt(NoV * (NoV - NoV * a2) + a2);
    float GGXL = NoV * sqrt(NoL * (NoL - NoL * a2) + a2);
    return saturate(0.5 / (GGXV + GGXL));
}

// 清漆专用的廉价 visibility 项(Kelemen 2001), 跟 Filament surface_brdf.fs 的 V_Kelemen 一致
float V_Kelemen(float LoH)
{
    return saturate(0.25 / max(LoH * LoH, 1e-5));
}

// UE BRDF.ush 的 SheenLTC_Cofficients() ALU 近似分支(Zeltner et al. 2022 "Practical
// Multiple-Scattering Sheen Using Linearly Transformed Cosines" 配套拟合公式的解析近似,
// 不需要采样任何纹理)。拿 RenderDoc 截出来的 UE 真实 Sheen_LTC.dds(32x32)核对过: 这三个
// 通道跟真实烘焙数据的相关系数分别是 aInv 0.988、bInv 0.939、方向反照率 0.9992——精度足够,
// 不需要再额外拟合/烘一张表。返回 (aInv, bInv, 方向反照率, 0)
float4 sheenLtcCoefficients(float NoV, float roughness)
{
    float x = saturate(abs(NoV) + 1e-5);
    float y = sqrt(roughness);

    float A = ((2.58126 * x + 0.813703 * y) * y) / (1.0 + 0.310327 * x * x + 2.60994 * x * y);
    float B = sqrt(max(1.0 - x, 0.0)) * (y - 1.0) * y * y * y
        / (0.0000254053 + 1.71228 * x - 1.71506 * x * y + 1.34174 * y * y);

    float s = y * (0.0206607 + 1.58491 * y) / (0.0379424 + y * (1.32227 + y));
    float m = y * (-0.193854 + y * (-1.14885 + y * (1.7932 - 0.95943 * y * y))) / (0.046391 + y);
    float o = y * (0.000654023 + (-0.0207818 + 0.119681 * y) * y) / (1.26264 + y * (-1.92021 + y));
    float R = exp(-0.5 * pow2((x - m) / s)) / (s * sqrt(2.0 * PI)) + o;

    return float4(A, B, R, 0.0);
}

// UE BRDF.ush 的 SheenLTC_Eval(): 把 L 转到切空间, 用 (aInv, bInv) 对余弦分布做剪切变换,
// 返回值已经是"BRDF*NoL"的完整量(不是只有 BRDF), 调用方不能再额外乘一次 NoL——这是跟其余
// 几个 lobe(D*Vis*F, 单独算, NoL 由调用方最后统一乘)不一样的地方, LTC 构造本身把余弦项
// 折进了变换的 Jacobian 里。directionalAlbedo 输出的就是 sheenLtcCoefficients() 的 R 通道,
// 单独暴露出来是因为间接光照(evaluateIBL)只需要这个标量, 用不到完整的逐方向求值
float sheenLtcEval(float3 N, float3 V, float3 L, float NoV, in float4 ltc, out float directionalAlbedo)
{
    float3 T1 = normalize(V - N * NoV);
    float3 T2 = cross(N, T1);
    float3 localL = float3(dot(T1, L), dot(T2, L), dot(N, L));

    float aInv = ltc.x;
    float bInv = ltc.y;
    directionalAlbedo = ltc.z;

    float3 wrappedL = float3(aInv * localL.x + bInv * localL.z, aInv * localL.y, localL.z);
    float invLen2 = 1.0 / max(dot(wrappedL, wrappedL), 1e-12);
    float jacobian = pow2(aInv * invLen2);
    float cosDist = wrappedL.z / PI;
    return max(cosDist * jacobian, 0.0);
}

// alpha = roughness^2。跟 UE BRDF.ush 的 GetAnisotropicRoughness()/Filament
// surface_shading_model_standard.fs 的 at/ab 是同一套公式(Kulla 2017)
void getAnisotropicRoughness(float alpha, float anisotropy, out float ax, out float ay)
{
    ax = max(alpha * (1.0 + anisotropy), 0.001);
    ay = max(alpha * (1.0 - anisotropy), 0.001);
}

float D_GGXaniso(float ax, float ay, float NoH, float XoH, float YoH)
{
    float a2 = ax * ay;
    float3 v = float3(ay * XoH, ax * YoH, a2 * NoH);
    float s = dot(v, v);
    return (1.0 / PI) * a2 * (a2 / s) * (a2 / s);
}

float Vis_SmithJointAniso(float ax, float ay, float NoV, float NoL, float XoV, float XoL, float YoV, float YoL)
{
    float visV = NoL * length(float3(ax * XoV, ay * YoV, NoV));
    float visL = NoV * length(float3(ax * XoL, ay * YoL, NoL));
    return 0.5 / max(visV + visL, 1e-5);
}

// 把法线往各向异性方向"掰弯"再算反射向量, 用于 IBL 镜面反射——跟 UE BRDF.ush 的
// ModifyGGXAnisotropicNormalRoughness()、Filament surface_lighting.fs 的
// getReflectedVector() 是同一个公式(bendFactor 的常数 5.0 两边一致)。roughness 参数
// 保留只是为了跟两边的函数签名对齐, 函数体内不修改它(两边都把粗糙度调整那一步禁用了)
void modifyGGXAnisotropicNormalRoughness(float3 tangentWS, float anisotropy, inout float roughness, inout float3 N, float3 V)
{
    if (abs(anisotropy) > 0.0)
    {
        float3 X = tangentWS;
        float3 Y = normalize(cross(N, X));

        float3 anisotropicDir = anisotropy >= 0.0 ? Y : X;
        float3 anisotropicT = cross(anisotropicDir, V);
        float3 anisotropicN = cross(anisotropicT, anisotropicDir);

        float bendFactor = abs(anisotropy) * saturate(5.0 * roughness);
        N = normalize(lerp(N, anisotropicN, bendFactor));
    }
}

struct BxDFContext
{
    float3 L, H;
    float NoV, NoL, NoH, VoH;
    // 只有 sp.hasAnisotropy 时才有意义
    float3 X, Y;
    float XoH, YoH, XoV, YoV, XoL, YoL;
};

void initBxDFContext(in ShadingParams sp, float3 L, out BxDFContext ctx)
{
    ctx.L = L;
    ctx.H = normalize(sp.viewDirWS + L);
    ctx.NoV = max(dot(sp.normalWS, sp.viewDirWS), 0.0);
    ctx.NoL = max(dot(sp.normalWS, L), 0.0);
    ctx.NoH = max(dot(sp.normalWS, ctx.H), 0.0);
    ctx.VoH = max(dot(sp.viewDirWS, ctx.H), 0.0);

    if (sp.hasAnisotropy)
    {
        ctx.X = sp.anisotropyTangentWS;
        ctx.Y = cross(sp.normalWS, ctx.X);
        ctx.XoH = dot(ctx.X, ctx.H); ctx.YoH = dot(ctx.Y, ctx.H);
        ctx.XoV = dot(ctx.X, sp.viewDirWS); ctx.YoV = dot(ctx.Y, sp.viewDirWS);
        ctx.XoL = dot(ctx.X, L); ctx.YoL = dot(ctx.Y, L);
    }
}

float3 evaluateLitBxDF(in ShadingParams sp, in BxDFContext ctx)
{
    float a = sp.roughness * sp.roughness;
    float a2 = a * a;
    float3 F = F_Schlick(ctx.VoH, sp.F0);
    float D, V;
    if (sp.hasAnisotropy)
    {
        float ax, ay;
        getAnisotropicRoughness(a, sp.anisotropyStrength, ax, ay);
        D = D_GGXaniso(ax, ay, ctx.NoH, ctx.XoH, ctx.YoH);
        V = Vis_SmithJointAniso(ax, ay, ctx.NoV, ctx.NoL, ctx.XoV, ctx.XoL, ctx.YoV, ctx.YoL);
    }
    else
    {
        D = D_GGX(ctx.NoH, a2);
        V = V_SmithGGXCorrelated(ctx.NoV, ctx.NoL, a2);
    }
    return (sp.albedo / PI + F * D * V) * ctx.NoL;
}

float3 evaluateClearCoatBxDF(in ShadingParams sp, in BxDFContext ctx)
{
    float3 baseLobe = evaluateLitBxDF(sp, ctx);

    float clearCoatNoH = saturate(dot(sp.clearCoatNormalWS, ctx.H));
    float clearCoatNoL = saturate(dot(sp.clearCoatNormalWS, ctx.L));
    float ccAlpha = sp.clearCoatRoughness * sp.clearCoatRoughness;
    float ccA2 = ccAlpha * ccAlpha;

    float Fcc = F_Schlick(ctx.VoH, float3(0.04), float3(1.0)).x * sp.clearCoat;
    float Dc = D_GGX(clearCoatNoH, ccA2);
    float Vc = V_Kelemen(ctx.VoH);
    float clearCoatLobe = Dc * Vc * Fcc;

    // 平方: 光进入清漆一次、从底层反射出来再穿出清漆一次, 共两次透射损耗(VoH == HoL, 两次的
    // 菲涅尔项相同)
    float transmission = 1.0 - Fcc;
    return baseLobe * (transmission * transmission) + clearCoatLobe * clearCoatNoL;
}

float D_InvGGX(float a2, float NoH)
{
    float A = 4.0;
    float d = (NoH - a2 * NoH) * NoH + a2;
    return (1.0 + 4.0 * a2 * a2 / (d * d)) / (PI * (1.0 + A * a2));
}

float Vis_Cloth(float NoV, float NoL)
{
    return 1.0 / (4.0 * (NoL + NoV - NoL * NoV));
}

// UE BRDF.ush 的 F_Schlick(SpecularColor, VoH) 特化版本: F0 接近黑色时压掉掠射角那圈菲涅尔
// 亮边, 因为反射率接近零的材质物理上不该有明显的菲涅尔回光
float3 F_SchlickCloth(float VoH, float3 F0)
{
    float Fc = pow(max(1.0 - VoH, 0.0), 5.0);
    return saturate(50.0 * F0.g) * Fc + (1.0 - Fc) * F0;
}

// UE legacy(Substrate 之前)的 Cloth 模型: clothAmount 是"光滑材质"和"起绒材质"两种高光形状
// 之间的插值权重, 不是叠加在普通材质上的一层——spec1 是正常 GGX, spec2 是纤维起绒的
// asperity scattering lobe, 最终镜面反射在两者之间 lerp
float3 evaluateClothBxDF(in ShadingParams sp, in BxDFContext ctx)
{
    // UE ClothBxDF() 对 NoV 的专属偏移: Vis_Cloth 分母 NoL+NoV-NoL*NoV 在掠射角(NoV、NoL
    // 同时趋近 0)下会精确到 0, 这里保证 NoV 恒正, 其他 lobe(evaluateLitBxDF 等)不需要这个
    float clothNoV = saturate(abs(ctx.NoV) + 1e-5);

    float a2 = pow2(sp.roughness * sp.roughness);
    float3 spec1 = F_Schlick(ctx.VoH, sp.F0) * D_GGX(ctx.NoH, a2) * V_SmithGGXCorrelated(clothNoV, ctx.NoL, a2);
    float3 spec2 = D_InvGGX(a2, ctx.NoH) * Vis_Cloth(clothNoV, ctx.NoL) * F_SchlickCloth(ctx.VoH, sp.fuzzColor);

    float3 specular = lerp(spec1, spec2, sp.clothAmount);
    return (sp.albedo / PI + specular) * ctx.NoL;
}

float3 evaluateBxDF(in ShadingParams sp, in BxDFContext ctx)
{
    if (sp.shadingModel == ShadingModel::Clear_Coat)
        return evaluateClearCoatBxDF(sp, ctx);
    if (sp.shadingModel == ShadingModel::Cloth)
        return evaluateClothBxDF(sp, ctx);
    return evaluateLitBxDF(sp, ctx);
}

// 对应 UE EnvBRDF() 的 GF 项,只是编码方式不同(Filament 多重散射编码 vs UE 的 (A,B) scale-bias)
float3 envBRDF(float3 F0, float2 dfg)
{
    return lerp(dfg.xxx, dfg.yyy, F0);
}

// UE EnvBRDF()(BRDF.ush:592-600)完整形式: F0 低于 2%(物理上不可能)时把掠射角菲涅尔项一并关掉
float3 envBRDFFadingF90(float3 F0, float2 dfg)
{
    return F0 * (dfg.y - dfg.x) + saturate(50.0 * F0.g) * dfg.x;
}

// UE RemapClearCoatDiffuseAndSpecularColor(ClearCoatCommon.ush:21-45): 清漆顶层菲涅尔衰减 +
// 金属清漆的吸收近似, 重算底层的等效漫反射/镜面反照率。noV 用清漆顶层法线算, 匹配 UE 全程只有
// 一个 WorldNormal(=顶层)的约定
void remapClearCoatDiffuseAndSpecularColor(
    float3 baseColor, float metallic, float specular, float roughness, float clearCoat, float noV,
    out float3 diffuseColor, out float3 specularColor)
{
    float refractionScale = ((noV * 0.5 + 0.5) * noV - 1.0) * saturate(1.25 - 1.25 * roughness) + 1.0;

    const float kMetalSpec = 0.9;
    float3 absorptionColor = baseColor * (1.0 / kMetalSpec);
    float3 absorption = absorptionColor * ((noV - 1.0) * 0.85 * (1.0 - lerp(absorptionColor, pow2(absorptionColor), -0.78)) + 1.0);

    const float kF0 = 0.04;
    float fc = pow(max(1.0 - noV, 0.0), 5.0);
    float f = fc + (1.0 - fc) * kF0;
    float layerAttenuation = lerp(1.0, 1.0 - f, clearCoat);

    float3 remappedBaseColor = lerp(baseColor * layerAttenuation, kMetalSpec * absorption * refractionScale, metallic * clearCoat);
    diffuseColor = remappedBaseColor - remappedBaseColor * metallic;

    float remappedSpecular = lerp(specular, refractionScale, clearCoat);
    specularColor = lerp(float3(0.08 * remappedSpecular), remappedBaseColor, metallic);
}

// UE ClearCoatLayerCombine 的底层混合项(DiffuseIndirectComposite.usf:246-259, ClearCoatCommon.ush:66):
// 跟 envBRDFFadingF90 同一个 UE EnvBRDF 展开式, 多一个 (1-clearCoat) 只压低尾项(掠射角修正项)
float3 clearCoatBottomLayerEnvBRDF(float3 specularColor, float2 dfg, float clearCoat)
{
    return specularColor * (dfg.y - dfg.x) + saturate(50.0 * specularColor.g) * dfg.x * (1.0 - clearCoat);
}

float3 evaluateIBLSpecular(
    float3 F0,
    float roughness,
    float3 N,
    float3 R,
    float2 dfg,
    in TextureCube specularPrefilter,
    in SamplerState prefilterSampler
)
{
    float3 E = envBRDF(F0, dfg);

    float a2 = pow2(roughness * roughness);
    uint prefilterWidth, prefilterHeight, prefilterMipCount;
    specularPrefilter.GetDimensions(0, prefilterWidth, prefilterHeight, prefilterMipCount);
    float mip = roughness * (2.0 - roughness) * max(float(prefilterMipCount) - 1.0, 0.0);
    float3 prefilter = specularPrefilter.SampleLevel(prefilterSampler, lerp(R, N, a2), mip).rgb;
    float3 energyCompensation = 1.0 + F0 * (1.0 / dfg.y - 1.0);
    return prefilter * E * energyCompensation;
}

float3 evaluateSHIrradiance(float3 N, in float4 packedSH[7])
{
    float4 q1 = float4(N.x * N.y, N.y * N.z, N.z * N.z, N.x * N.z);
    float q2 = N.x * N.x - N.y * N.y;
    return float3(
        dot(packedSH[0], float4(N, 1.0)) + dot(packedSH[3], q1) + packedSH[6][0] * q2,
        dot(packedSH[1], float4(N, 1.0)) + dot(packedSH[4], q1) + packedSH[6][1] * q2,
        dot(packedSH[2], float4(N, 1.0)) + dot(packedSH[5], q1) + packedSH[6][2] * q2
    );
}

float3 evaluateIBLDiffuse(
    float3 albedo,
    float3 N,
    float3 E,
    in float4 packedSH[7]
)
{
    float3 irradiance = evaluateSHIrradiance(N, packedSH);
    return max(irradiance, float3(0.0)) / PI * albedo * (1.0 - E);
}

// Jimenez et al. 2016 的多次反弹近似
float3 AOMultiBounce(float3 baseColor, float ao)
{
    float3 a = 2.0404 * baseColor - 0.3324;
    float3 b = -4.7951 * baseColor + 0.6417;
    float3 c = 2.7552 * baseColor + 0.6903;
    return max(float3(ao), ((ao * a + b) * ao + c) * ao);
}

// 两个圆锥(反射锥、AO 未遮挡锥)张角和夹角求相交面积的近似,UE LumenScreenSpaceBentNormal.ush:28-38
float approximateConeConeIntersection(float arcLength0, float arcLength1, float angleBetweenCones)
{
    float angleDifference = abs(arcLength0 - arcLength1);
    return smoothstep(0.0, 1.0,
        1.0 - saturate((angleBetweenCones - angleDifference) / (arcLength0 + arcLength1 - angleDifference)));
}

// Short Range AO 的 bent normal 估计镜面反射的自遮挡,UE LumenScreenSpaceBentNormal.ush:40-51
float calculateSpecularOcclusion(float3 N, float roughness, float ao, float3 V, float3 bentNormalOcclusion)
{
    float reflectionConeAngle = max(roughness, 0.1) * PI;
    float unoccludedAngle = ao * PI;
    float3 reflectionVector = reflect(-V, N);
    // acosFast 换成精确 acos
    float angleBetween = acos(clamp(dot(bentNormalOcclusion, reflectionVector) / max(ao, 0.001), -1.0, 1.0));
    float specularOcclusion = approximateConeConeIntersection(reflectionConeAngle, unoccludedAngle, angleBetween);
    return lerp(0.0, specularOcclusion, saturate((unoccludedAngle - 0.1) / 0.2));
}

// Fd/Fr 分开返回(不在这里合并)——调用方(SkyLighting.slang)要分别对漫反射/镜面反射项乘不同的
// occlusion 项(specular occlusion 公式跟 diffuse occlusion 不一样), 合并成一个值之后就没法
// 再分开处理了
void evaluateIBL(
    in ShadingParams sp,
    in float4 packedSH[7],
    in Texture2D dfgLut,
    in SamplerState lutSampler,
    in TextureCube specularPrefilter,
    in SamplerState prefilterSampler,
    out float3 Fd,
    out float3 Fr
)
{
    float3 diffuseAlbedo = sp.albedo;
    if (sp.shadingModel == ShadingModel::Cloth)
    {
        // UE legacy(SkyLightingDiffuseShared.ush 的 CLOTH 分支): fuzz 直接并入 diffuse 反照率,
        // 没有单独的镜面反射 IBL 项——起绒 lobe 只在直接光照里存在
        diffuseAlbedo += sp.fuzzColor * sp.clothAmount;
    }

    float diffuseNoV = max(dot(sp.normalWS, sp.viewDirWS), 0.0);
    float2 diffuseDfg = dfgLut.SampleLevel(lutSampler, float2(diffuseNoV, sp.roughness), 0.0).rg;
    float3 diffuseE = lerp(diffuseDfg.xxx, diffuseDfg.yyy, sp.F0);
    Fd = evaluateIBLDiffuse(diffuseAlbedo, sp.normalWS, diffuseE, packedSH);

    float3 specularNormalWS = sp.normalWS;
    float3 specularReflectDirWS = reflect(-sp.viewDirWS, sp.normalWS);
    float2 specularDfg = diffuseDfg;
    if (sp.hasAnisotropy)
    {
        float bentRoughness = sp.roughness;
        modifyGGXAnisotropicNormalRoughness(sp.anisotropyTangentWS, sp.anisotropyStrength,
            bentRoughness, specularNormalWS, sp.viewDirWS);
        specularReflectDirWS = reflect(-sp.viewDirWS, specularNormalWS);
        float specularNoV = max(dot(specularNormalWS, sp.viewDirWS), 0.0);
        specularDfg = dfgLut.SampleLevel(lutSampler, float2(specularNoV, sp.roughness), 0.0).rg;
    }
    Fr = evaluateIBLSpecular(sp.F0, sp.roughness, specularNormalWS,
        specularReflectDirWS, specularDfg, specularPrefilter, prefilterSampler);

    if (sp.shadingModel == ShadingModel::Clear_Coat)
    {
        float clearCoatNoV = saturate(dot(sp.clearCoatNormalWS, sp.viewDirWS));
        float Fc = F_Schlick(clearCoatNoV, float3(0.04), float3(1.0)).x * sp.clearCoat;
        float attenuation = 1.0 - Fc;
        // Fd/Fr 已经对入射方向积分过, 只有出射角 NoV 是唯一确定的, 没有单独的入射角可以
        // 再衰减一次
        Fd *= attenuation;
        Fr *= attenuation;

        float3 clearCoatReflectDirWS = reflect(-sp.viewDirWS, sp.clearCoatNormalWS);
        float2 clearCoatDfg = dfgLut.SampleLevel(lutSampler, float2(clearCoatNoV, sp.clearCoatRoughness), 0.0).rg;
        float3 clearCoatIBL = evaluateIBLSpecular(float3(0.04), sp.clearCoatRoughness,
            sp.clearCoatNormalWS, clearCoatReflectDirWS, clearCoatDfg,
            specularPrefilter, prefilterSampler);
        // clearCoatIBL 内部的 dfg.E 项已经是 F0=0.04 这个粗糙度/角度下的完整反射率估计(菲涅尔
        // 已经积分进 DFG LUT 里了), 这里只需要按 sp.clearCoat 强度遮罩, 不能再乘一次 Fc——
        // 否则等于把菲涅尔衰减算了两遍
        Fr += clearCoatIBL * sp.clearCoat;
    }
}
