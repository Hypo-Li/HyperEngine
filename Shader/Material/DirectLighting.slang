#include <Common/Binding.slang>
#include <Common/MaterialIO.slang>
#include <Atmosphere/AtmosphereCommon.slang>
#include "ShadingParams.slang"
#include "BxDF.slang"
#include "Light.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

[[vk::binding(0, 2)]]
Texture2D<uint2> GbufferAImage;

[[vk::binding(1, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(3, 2)]]
DepthTexture2D SceneDepthImage;

// 这个 pass 的 bind group layout 是编译期反射生成的、固定不变的(Pass shader 不支持像材质那样
// 按宏走运行时变体), 所以场景没有大气时也必须绑点什么——DirectLightingPass 会喂一张 1x1 全白
// 纹理 + 一份半径非零的占位 AtmosphereParameters, 让 getTransmittance() 恒返回 (1,1,1), 等效于
// 没有大气衰减, 不需要为"有没有大气"编两份 shader
[[vk::binding(4, 2)]]
ConstantBuffer<AtmosphereParameters> Atmosphere;

[[vk::binding(5, 2)]]
Texture2D<float4> TransmittanceLutTexture;

[[vk::binding(6, 2)]]
SamplerState LutSampler;

// 这盏光自己的 ShadowMask(ShadowMaskPass 生成, 1=见光/0=阴影)。没开 Cast Shadows 的光,
// DirectLightingPass 会绑一张 1x1 全白纹理占位, 等效于完全没有遮挡, 不需要在这里区分
[[vk::binding(7, 2)]]
Texture2D<float4> ShadowMaskImage;

[[vk::binding(8, 2)]]
Texture2D<float4> GbufferDImage;

// Binding.slang 里的公共 sLightData 只在公共 set1 布局里声明——延迟光照是唯一真正需要它的
// 不透明渲染路径, 补一份本地声明放这个 pass 自己的 set2 里, 不占公共 set1
[[vk::binding(9, 2)]]
StructuredBuffer<LightData> sLightData;

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

[[vk::push_constant]]
uint kLightIndex;

[shader("vertex")]
float4 VertexMain(in uint vertexID : SV_VertexID) : SV_Position
{
    return float4(kPositions[vertexID], 0, 1);
}

[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    float sceneDepth = SceneDepthImage[uint2(svPosition.xy)].r;
    if (sceneDepth == 0)
    {
        return float4(0, 0, 0, 1);
    }

    uint2 gbufferA = GbufferAImage[uint2(svPosition.xy)];
    float4 gbufferB = GbufferBImage[uint2(svPosition.xy)];
    float4 gbufferC = GbufferCImage[uint2(svPosition.xy)];
    float4 gbufferD = GbufferDImage[uint2(svPosition.xy)];

    float2 uv = svPosition.xy / uViewData.resolution.xy;
    float4 fragPosCS = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, sceneDepth, 1);
    float4 fragPosVS = mul(fragPosCS, uViewData.clipToView);
    fragPosVS *= 1.0 / fragPosVS.w;
    float3 viewDirVS = normalize(-fragPosVS.xyz);
    float3 viewDirWS = mul(uViewData.worldToViewRot, viewDirVS);

    ShadingParams sp = decodeShadingParams(gbufferA, gbufferB, gbufferC, gbufferD, viewDirWS);
    if (sp.shadingModel == ShadingModel::Unlit)
    {
        return float4(0, 0, 0, 1);
    }

    LightData lightData = sLightData[kLightIndex];
    InstanceData instanceData = sInstanceData[lightData.instanceIndex];

    float3 lightDirWS;
    float3 lightAttenuation = float3(1);
    if (lightData.type == LightType::Directional)
    {
        lightDirWS = mul(float3(0, 0, 1), instanceData.objectToWorldRot);
    }
    else if (lightData.type == LightType::Spot)
    {
        evaluateSpotLightVector(lightData, instanceData, fragPosVS.xyz, uViewData, lightDirWS, lightAttenuation);
    }
    else // Point
    {
        evaluatePointLightVector(lightData, instanceData, fragPosVS.xyz, uViewData, lightDirWS, lightAttenuation);
    }

    BxDFContext ctx;
    initBxDFContext(sp, lightDirWS, ctx);
    float3 litColor = evaluateBxDF(sp, ctx);

    // 太阳光穿过大气到达着色点的透过率——跟 Atmosphere.slang 渲染天空本体用的是同一张
    // TransmittanceLut、同一套 (高度, 光源天顶角) 参数化, 只是这里只在着色点这一个高度采样
    // 一次(不是沿视线积分)。没有大气的场景 DirectLightingPass 绑的是 DefaultResources 里
    // 完全无内容含义的通用占位 buffer(全零), 这里靠半径退化(topRadiusKm <= bottomRadiusKm,
    // 全零时自然成立)判断"没有真实大气", 整段跳过而不是硬算——大气模型的 LUT 参数化数学
    // 假设了一个半径非零的球壳, 塞退化输入进去必然出 NaN/Inf, 不该指望占位资源填出什么
    // "安全默认值"来兜底。点光源不参与这段——LUT 是按太阳天顶角参数化的, 塞点光源方向进去
    // 没有物理意义
    float3 transmittance = float3(1);
    if (lightData.type == LightType::Directional && Atmosphere.topRadiusKm > Atmosphere.bottomRadiusKm)
    {
        float3 cameraWorldPosKm = getWorldPosUnclamped(Atmosphere, uViewData.cameraOrigin);
        float distanceToFragmentKm = length(fragPosVS.xyz) * 0.001;
        float3 surfaceWorldPosKm = cameraWorldPosKm - viewDirWS * distanceToFragmentKm;
        float surfaceHeightKm = length(surfaceWorldPosKm);
        float3 upVector = surfaceWorldPosKm / surfaceHeightKm;
        float lightZenithCos = dot(lightDirWS, upVector);
        transmittance = getTransmittance(Atmosphere, TransmittanceLutTexture, LutSampler, lightZenithCos, surfaceHeightKm)
            * getEarthShadow(lightDirWS, upVector);
    }

    float3 attenuatedIntensity = lightData.intensity * transmittance * lightAttenuation;
    float4 color = float4(attenuatedIntensity * litColor, 1);

    // 这个 pass 是每盏光一次 draw、加法混合叠到 SceneColor 上, 乘遮罩天然只影响这一盏光
    // 自己的贡献。按 UV 采样(不是按绝对像素坐标 Load)——没开 Cast Shadows 的光绑的是
    // DefaultResources 的 1x1 白纹理占位, 按像素坐标 Load 在全分辨率下几乎处处越界
    // (Vulkan 下是未定义行为), 按 UV 采样配合 clamp 采样器则始终落在这张纹理仅有的那个
    // 像素上, 不管实际绑的是全分辨率 ShadowMask 还是 1x1 占位都能正确取值, 见
    // SkyLighting.slang 里 AmbientOcclusionImage 的同一处理。
    float shadow = ShadowMaskImage.SampleLevel(LutSampler, uv, 0.0).r;
    // ShadowMask 存的是衰减的平方根(同 UE 的 LightAttenuation 编码)。
    shadow *= shadow;
    color.rgb *= shadow * getPreExposure(uViewData);
    return color;
}
