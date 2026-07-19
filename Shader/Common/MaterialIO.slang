#pragma once

// evaluateMaterial() 的输入/输出类型, 各材质模板(Opaque.slang/ShadowDepth.slang 等)共享同一份
// 定义——RenderTagFlags::GeometryOnly 的模板(比如 ShadowDepth)虽然从不实际调用 evaluateMaterial(),
// 但材质自己的 evaluateMaterial 源码仍然会被无差别 splice 进这些模板编译(为了让共享 shader 和
// 逐材质 shader 的输入完全一致), 所以这两个类型必须在所有模板里都能解析到, 不能只在 Opaque.slang
// 里定义
struct MaterialInput
{
    float3 normalWS;
    float4 tangentWS;
    float4 color;
    float2 texCoords[8];
    float3 viewDirWS;
    float time;
};

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
    }
};
