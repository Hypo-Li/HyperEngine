#include <Common/Binding.slang>
#include <Common/VertexPositionOffset.slang>
#include <Common/MaterialIO.slang>
#include "BxDF.slang"

// 半透明前向着色模板——跟 Opaque.slang 共享同一个 evaluateMaterial()/MaterialInput/MaterialOutput
// 约定, 但不写 GBuffer: alpha blend 决定了必须在一次 draw 里把光照结果直接合成到 SceneColor,
// 不能像 Opaque 那样先写 GBuffer 再交给屏幕空间的 DirectLighting/SkyLighting 处理。
//
// 直接光照(太阳)走 push constant 传的方向光索引。间接光照分两条路:
// - 漫反射: 采样 LightVolumeR/G/B 这三张视锥体对齐的体积纹理(每个体素是 1 阶球谐, 见下方
//   binding 处的注释)——它们是半透明渲染跟"场景里到底有哪些间接光源(天光/以后的局部探针/
//   烘焙 GI)"之间唯一的接口, 谁往里面注入、注入的是什么, Translucent.slang 完全不关心,
//   只管按法线方向重建辐照度。
// - 镜面反射: 直接采样天光的 prefilter cubemap(SkyLightSpecularPrefilter/DfgLut), 不经过
//   LightVolume——反射方向性强, 体积纹理的分辨率装不下, 这点上 UE 对半透明体的处理方式也是
//   一样的(Translucency Lighting Volume 只管漫反射, 镜面反射直接采样 Reflection Capture/
//   SkyLight 的 cubemap)。局部反射探针出现之前, 这里只有天光一个来源, 不需要按距离加权混合。
// 这些纹理都挂在 TranslucentPass 专属的 scene-and-view bind group(set1)里, 不占材质自己的
// set2, 具体原因见 TranslucentPass.cpp 顶部注释。

#ifndef TEXCOORD_COUNT
    #define TEXCOORD_COUNT 1
#endif

#ifndef HAS_VERTEX_COLOR
    #define HAS_VERTEX_COLOR 1
#endif

// 跟 AtmosphereParameters 的 sun0/sun1 约定一致: 最多同时支持 2 盏方向光参与半透明前向光照,
// 多出的方向光会被忽略(不影响半透明物体, 仍然正常照亮不透明物体)
struct ForwardLightingConstants
{
    uint lightIndex0;
    uint lightIndex1;
    uint lightCount;
};
[[vk::push_constant]]
ForwardLightingConstants kForwardLighting;

// 光照体积纹理存的是 1 阶球谐(常数项 + 线性项), R/G/B 三个通道各一个 float4——布局跟
// SkyLightPackedSHCoeff 的 packedSH[0..2] 完全一致(xyz = 线性系数, w = 常数项), 没有 2 阶的
// 二次项。1 阶已经够表达漫射光源"哪个方向亮哪个方向暗"的主要特征, 3 个 float4 比完整 2 阶的
// 7 个 float4 省一大截存储/带宽, 具体取舍见跟用户讨论确认的方案
[[vk::binding(4, 1)]]
Texture3D<float4> LightVolumeR;

[[vk::binding(5, 1)]]
Texture3D<float4> LightVolumeG;

[[vk::binding(6, 1)]]
Texture3D<float4> LightVolumeB;

[[vk::binding(7, 1)]]
SamplerState NoMipSampler;

// 天光镜面反射——直接复用 SkyLightProxy 已经算好的 prefilter cubemap + DfgLut(跟
// SkyLighting.slang 给不透明物体用的是同一份数据), 不经过 LightVolume: 反射方向性强, 体积
// 纹理那点分辨率装不下, UE 对半透明体的镜面反射也是直接采样 Reflection Capture/SkyLight 的
// cubemap, 不走它的 Translucency Lighting Volume(那个只管漫反射)
[[vk::binding(8, 1)]]
TextureCube<float4> SkyLightSpecularPrefilter;

[[vk::binding(9, 1)]]
Texture2D DfgLut;

[[vk::binding(10, 1)]]
SamplerState PrefilterSampler;

// 光照体积纹理沿视锥体前向(view space -Z)覆盖的最大距离(米)——半透明物体通常是玻璃/水面这类
// 近处的小物件, 不需要覆盖太远; 超出这个距离的部分直接夹到最后一层体素
static constexpr float kLightVolumeMaxDistance = 50.0;

// 把 view space 位置映射成体积纹理的采样 UV: xy 是屏幕空间位置(跟 DirectLighting.slang 反过来
// 用同一套 clip space 约定), z 是沿视锥体前向的线性距离切片
float3 viewPosToLightVolumeUV(in float3 positionVS, in ViewData viewData)
{
    float4 clipPos = mul(float4(positionVS, 1.0), viewData.viewToClip);
    clipPos.xyz /= clipPos.w;
    float2 screenUV = clipPos.xy * 0.5 + 0.5;
    screenUV.y = 1.0 - screenUV.y;
    float viewDepth = -positionVS.z;
    float sliceZ = saturate(viewDepth / kLightVolumeMaxDistance);
    return float3(screenUV, sliceZ);
}

struct VSOuput
{
    float4 position : SV_Position;
    float3 positionVS : TEXCOORD0;
    float3 normalVS : TEXCOORD1;
    float4 tangentVS : TEXCOORD2;
#if HAS_VERTEX_COLOR
    float4 color : TEXCOORD3;
#endif
#if TEXCOORD_COUNT > 0
    float2 texCoord0 : TEXCOORD4;
#endif
#if TEXCOORD_COUNT > 1
    float2 texCoord1 : TEXCOORD5;
#endif
#if TEXCOORD_COUNT > 2
    float2 texCoord2 : TEXCOORD6;
#endif
#if TEXCOORD_COUNT > 3
    float2 texCoord3 : TEXCOORD7;
#endif
#if TEXCOORD_COUNT > 4
    float2 texCoord4 : TEXCOORD8;
#endif
#if TEXCOORD_COUNT > 5
    float2 texCoord5 : TEXCOORD9;
#endif
#if TEXCOORD_COUNT > 6
    float2 texCoord6 : TEXCOORD10;
#endif
#if TEXCOORD_COUNT > 7
    float2 texCoord7 : TEXCOORD11;
#endif
};

MaterialInput makeMaterialInput(in VSOuput vsout, in ViewData viewData)
{
    MaterialInput mi;
    const float3x3 viewToWorldRot = transpose(viewData.worldToViewRot);
    mi.normalWS = mul(normalize(vsout.normalVS), viewToWorldRot);
    mi.tangentWS = float4(mul(normalize(vsout.tangentVS.xyz), viewToWorldRot), vsout.tangentVS.w);
#if HAS_VERTEX_COLOR
    mi.color = vsout.color;
#else
    mi.color = float4(1);
#endif

#if TEXCOORD_COUNT > 0
    mi.texCoords[0] = vsout.texCoord0;
#else
    mi.texCoords[0] = float2(0, 0);
#endif
#define LAST_TEXCOORD_INDEX 0

#if TEXCOORD_COUNT > 1
    mi.texCoords[1] = vsout.texCoord1;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 1
#else
    mi.texCoords[1] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 2
    mi.texCoords[2] = vsout.texCoord2;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 2
#else
    mi.texCoords[2] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 3
    mi.texCoords[3] = vsout.texCoord3;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 3
#else
    mi.texCoords[3] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 4
    mi.texCoords[4] = vsout.texCoord4;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 4
#else
    mi.texCoords[4] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 5
    mi.texCoords[5] = vsout.texCoord5;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 5
#else
    mi.texCoords[5] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 6
    mi.texCoords[6] = vsout.texCoord6;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 6
#else
    mi.texCoords[6] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

#if TEXCOORD_COUNT > 7
    mi.texCoords[7] = vsout.texCoord7;
#undef LAST_TEXCOORD_INDEX
#define LAST_TEXCOORD_INDEX 7
#else
    mi.texCoords[7] = mi.texCoords[LAST_TEXCOORD_INDEX];
#endif

    mi.viewDirWS = mul(viewData.worldToViewRot, normalize(-vsout.positionVS));
    mi.time = uFrameData.wrappedTime;
    return mi;
}

[MATERIAL_IMPLEMENTATION]

[VERTEX_ATTRIBUTE_GETTERS]

[shader("vertex")]
VSOuput VertexMain(in VSInput input)
{
    uint instanceIndex = sIndices[getInstanceIndex(input)];
    const InstanceData instanceData = sInstanceData[instanceIndex];
    const ViewData viewData = sViewData[0];

    float4x4 localToView;
    calculateLocalToView(
        instanceData.localToWorld,
        viewData.worldToViewRot,
        viewData.cameraOrigin,
        localToView
    );
    const float3x3 localToViewRot = mul(instanceData.localToWorldRot, viewData.worldToViewRot);

    VSOuput output;
    output.positionVS = mul(float4(getPosition(input), 1), localToView).xyz;

    VertexPositionOffsetInput vpoi;
    vpoi.positionLS = getPosition(input);
    vpoi.normalLS = getNormal(input);
    vpoi.time = uFrameData.wrappedTime;
    vpoi.localToWorld = instanceData.localToWorld;
    output.positionVS += mul(evaluateVertexPositionOffset(vpoi), viewData.worldToViewRot);

    output.normalVS = mul(getNormal(input), localToViewRot);
    const float4 tangent = getTangent(input);
    output.tangentVS = float4(mul(tangent.xyz, localToViewRot), tangent.w);
#if HAS_VERTEX_COLOR
    output.color = getColor(input);
#endif
#if TEXCOORD_COUNT > 0
    output.texCoord0 = getTexCoord0(input);
#endif
#if TEXCOORD_COUNT > 1
    output.texCoord1 = getTexCoord1(input);
#endif
#if TEXCOORD_COUNT > 2
    output.texCoord2 = getTexCoord2(input);
#endif
#if TEXCOORD_COUNT > 3
    output.texCoord3 = getTexCoord3(input);
#endif
#if TEXCOORD_COUNT > 4
    output.texCoord4 = getTexCoord4(input);
#endif
#if TEXCOORD_COUNT > 5
    output.texCoord5 = getTexCoord5(input);
#endif
#if TEXCOORD_COUNT > 6
    output.texCoord6 = getTexCoord6(input);
#endif
#if TEXCOORD_COUNT > 7
    output.texCoord7 = getTexCoord7(input);
#endif
    output.position = mul(float4(output.positionVS, 1), viewData.viewToClip);
    return output;
}

// 累加一盏方向光对应的漫反射/高光贡献, 分开返回是因为最终合成时要按预乘 alpha 的方式处理:
// 漫反射项乘 alpha 衰减, 高光项不衰减(混合系数统一用 One, 高光在 Src 里已经是"满强度"了)
void evaluateDirectLight(
    uint lightIndex,
    float3 normalWS,
    float3 viewDirWS,
    float3 albedo,
    float3 F0,
    float a2,
    inout float3 diffuseSum,
    inout float3 specularSum
)
{
    LightData lightData = sLightData[lightIndex];
    InstanceData lightInstanceData = sInstanceData[lightData.instanceIndex];
    float3 lightDirWS = mul(float3(0, 0, 1), lightInstanceData.localToWorldRot);
    float3 halfDirWS = normalize(lightDirWS + viewDirWS);

    float NoV = max(dot(normalWS, viewDirWS), 0.0);
    float NoL = max(dot(normalWS, lightDirWS), 0.0);
    float NoH = max(dot(normalWS, halfDirWS), 0.0);
    float VoH = max(dot(viewDirWS, halfDirWS), 0.0);

    float3 F = F_Schlick(VoH, F0);
    float D = D_GGX(NoH, a2);
    float V = V_SmithGGXCorrelated(NoV, NoL, a2);

    diffuseSum += lightData.intensity * (albedo * (1.0 - F) / PI) * NoL;
    specularSum += lightData.intensity * (F * D * V) * NoL;
}

[shader("fragment")]
float4 FragmentMain(in VSOuput vsout)
{
    MaterialInput mi = makeMaterialInput(vsout, sViewData[0]);
    MaterialOutput mo;
    evaluateMaterial(mi, mo);

    const float3 bitangentWS = normalize(cross(mi.normalWS, mi.tangentWS.xyz)) * mi.tangentWS.w;
    const float3x3 tangentToWorld = float3x3(mi.tangentWS.xyz, bitangentWS, mi.normalWS);
    const float3 normalWS = mul(mo.normal, tangentToWorld);

    float3 albedo = mo.baseColor * (1.0 - mo.metallic);
    float a = mo.roughness * mo.roughness;
    float a2 = a * a;
    float3 F0 = lerp(float3(0.16 * mo.specular * mo.specular), mo.baseColor, mo.metallic);

    float3 diffuseSum = float3(0);
    float3 specularSum = float3(0);
    if (kForwardLighting.lightCount > 0)
    {
        evaluateDirectLight(kForwardLighting.lightIndex0, normalWS, mi.viewDirWS, albedo, F0, a2, diffuseSum, specularSum);
    }
    if (kForwardLighting.lightCount > 1)
    {
        evaluateDirectLight(kForwardLighting.lightIndex1, normalWS, mi.viewDirWS, albedo, F0, a2, diffuseSum, specularSum);
    }

    // 间接光照的漫反射部分从光照体积纹理采样——R/G/B 三个通道各是一个 1 阶 SH 系数 float4,
    // 按当前法线方向重建辐照度(跟 evaluateIBL() 里 Fd 的线性部分是同一个公式, 只是少了 2 阶
    // 的二次项), 再除以 PI 换算成 Lambertian 出射辐亮度乘 albedo
    float3 lightVolumeUV = viewPosToLightVolumeUV(vsout.positionVS, sViewData[0]);
    float4 shR = LightVolumeR.SampleLevel(NoMipSampler, lightVolumeUV, 0.0);
    float4 shG = LightVolumeG.SampleLevel(NoMipSampler, lightVolumeUV, 0.0);
    float4 shB = LightVolumeB.SampleLevel(NoMipSampler, lightVolumeUV, 0.0);
    float4 normalWS1 = float4(normalWS, 1.0);
    float3 indirectIrradiance = float3(dot(shR, normalWS1), dot(shG, normalWS1), dot(shB, normalWS1));
    diffuseSum += albedo / PI * max(indirectIrradiance, float3(0.0));

    // 间接光照的镜面反射部分不走体积纹理(方向性太强, 体素分辨率装不下), 直接采样天光的
    // prefilter cubemap, 跟 evaluateIBL() 给不透明物体算 Fr 是同一套公式
    float3 reflectDirWS = reflect(-mi.viewDirWS, normalWS);
    specularSum += evaluateIBLSpecular(F0, mo.roughness, normalWS, mi.viewDirWS, reflectDirWS, DfgLut, NoMipSampler, SkyLightSpecularPrefilter, PrefilterSampler);

    float alpha = saturate(mo.opacity);
    // 预乘 alpha: 混合系数 (One, OneMinusSrcAlpha), 漫反射项乘 alpha 融入背景, 高光项不乘 alpha
    // 直接叠加, 不会被半透明的 alpha 削弱——参见跟用户讨论确认的方案
    float3 premultipliedColor = mo.emissive + diffuseSum * alpha + specularSum;
    return float4(premultipliedColor, alpha);
}
