#pragma once
#include <Common/Defines.slang>
#include "MaterialBuiltins.slang"

#ifndef MATERIAL_DOMAIN
    #define MATERIAL_DOMAIN MATERIAL_DOMAIN_SURFACE
#endif

enum ShadingModel : uint
{
    Unlit = 0,
    Standard = 1,
    Clear_Coat = 2,
    Cloth = 3,
};

// evaluateMaterial() 的输入/输出类型, 各材质模板(Opaque.slang/ShadowDepth.slang 等)共享同一份
// 定义——RenderTagFlags::GeometryOnly 的模板(比如 ShadowDepth)虽然从不实际调用 evaluateMaterial(),
// 但材质自己的 evaluateMaterial 源码仍然会被无差别 splice 进这些模板编译(为了让共享 shader 和
// 逐材质 shader 的输入完全一致), 所以这两个类型必须在所有模板里都能解析到, 不能只在 Opaque.slang
// 里定义
#if MATERIAL_DOMAIN == MATERIAL_DOMAIN_SKY

// direction 指向光源
struct MainLight
{
    float3 direction;
    float3 intensity;
};

struct MaterialInput
{
    // 指向相机, 和表面域一致, 所以采样 cubemap 时需要取负
    float3 viewDirWS;
    float time;
    MainLight mainLights[2];
};

#else

struct MaterialInput
{
    float3 normalWS;
    float4 tangentWS;
    float4 color;
    MatUV texCoords[4];
    float3 viewDirWS;
    float time;
    float4 positionRWS;
    float3x4 relativeWorldToObject;
    float twoSidedSign;
};

#endif

#if MATERIAL_DOMAIN == MATERIAL_DOMAIN_SKY

struct MaterialOutput
{
    float3 emissive;

    __init()
    {
        emissive = float3(0);
    }
};

#else

struct MaterialOutput
{
    float3 emissive;
    float3 baseColor;
    float metallic;
    float roughness;
    float specular;
    float3 normal;
    float occlusion;
    float opacity;

    // 正交于 shading model 的开关(Standard/Clear_Coat 都能用), IS_ANISOTROPIC 为 0 时
    // anisotropyTangent 不必赋值
#if IS_ANISOTROPIC
    float anisotropy;
    float3 anisotropyTangent;
#endif

#if SHADING_MODEL == SHADING_MODEL_CLEAR_COAT || SHADING_MODEL == SHADING_MODEL_FROM_MATERIAL
    float clearCoat;
    float clearCoatRoughness;
    float3 clearCoatNormal;
#endif

#if SHADING_MODEL == SHADING_MODEL_CLOTH || SHADING_MODEL == SHADING_MODEL_FROM_MATERIAL
    float3 fuzzColor;
    float clothAmount;
#endif

#if SHADING_MODEL == SHADING_MODEL_FROM_MATERIAL
    ShadingModel shadingModel;
#endif

    __init()
    {
        emissive = float3(0);
        baseColor = float3(1);
        metallic = 0;
        roughness = 0.5;
        specular = 0.5;
        normal = float3(0, 0, 1);
        occlusion = 1;
        opacity = 1;
#if IS_ANISOTROPIC
        anisotropy = 0;
        anisotropyTangent = float3(1, 0, 0);
#endif
#if SHADING_MODEL == SHADING_MODEL_CLEAR_COAT || SHADING_MODEL == SHADING_MODEL_FROM_MATERIAL
        clearCoat = 0;
        clearCoatRoughness = 0;
        clearCoatNormal = float3(0, 0, 1);
#endif
#if SHADING_MODEL == SHADING_MODEL_CLOTH || SHADING_MODEL == SHADING_MODEL_FROM_MATERIAL
        fuzzColor = float3(0);
        clothAmount = 0;
#endif
#if SHADING_MODEL == SHADING_MODEL_FROM_MATERIAL
        shadingModel = ShadingModel::Standard;
#endif
    }
};

#endif
