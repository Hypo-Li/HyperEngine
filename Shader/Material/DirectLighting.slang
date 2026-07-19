#include <Common/Binding.slang>
#include <Atmosphere/AtmosphereCommon.slang>

[[vk::binding(0, 2)]]
Texture2D<float4> GbufferAImage;

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

float3 F_Schlick(float VoH, float3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - VoH, 0), 5.0);
}

float D_GGX(float NoH, float a)
{
    float a2 = a * a;
    float d = (NoH * NoH) * (a2 - 1.0) + 1.0;
    return clamp(a2 / (PI * d * d), 0.0, 1000.0);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    return saturate(0.5 / (lambdaV + lambdaL));
}

[shader("fragment")]
float4 FragmentMain(in float4 svPosition : SV_Position) : SV_Target
{
    float sceneDepth = SceneDepthImage[uint2(svPosition.xy)].r;
    if (sceneDepth == 0)
    {
        return float4(0, 0, 0, 1);
    }

    float4 gbufferA = GbufferAImage[uint2(svPosition.xy)];
    float4 gbufferB = GbufferBImage[uint2(svPosition.xy)];
    float4 gbufferC = GbufferCImage[uint2(svPosition.xy)];

    float3 normalWS = gbufferA.xyz * 2 - 1;
    float metallic = max(gbufferB.x, 0.045);
    float roughness = gbufferB.y;
    float specular = gbufferB.z;
    uint shadingModel = uint(floor(gbufferB.w * 255.0 + 0.5));
    float3 baseColor = gbufferC.xyz;
    float occlusion = gbufferC.w;

    float3 albedo = baseColor * (1 - metallic);
    float a = roughness * roughness;
    float3 F0 = lerp(float3(0.16 * specular * specular), baseColor, metallic);
    
    LightData lightData = sLightData[kLightIndex];
    InstanceData instanceData = sInstanceData[lightData.instanceIndex];
    float3 lightDirWS = mul(float3(0, 0, 1), instanceData.localToWorldRot);
    ViewData viewData = sViewData[0];
    
    float2 uv = svPosition.xy / viewData.resolution.xy;
    float4 fragPosCS = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, sceneDepth, 1);
    float4 fragPosVS = mul(fragPosCS, viewData.clipToView);
    fragPosVS *= 1.0 / fragPosVS.w;
    float3 viewDirVS = normalize(-fragPosVS.xyz);
    float3 viewDirWS = mul(viewData.worldToViewRot, viewDirVS);
    float3 halfDirWS = normalize(lightDirWS + viewDirWS);
    float3 reflectDirWS = reflect(-viewDirWS, normalWS);

    float NoV = max(dot(normalWS, viewDirWS), 0.0);
    float NoL = max(dot(normalWS, lightDirWS), 0.0);
    float NoH = max(dot(normalWS, halfDirWS), 0.0);
    float VoH = max(dot(viewDirWS, halfDirWS), 0.0);

    float3 F = F_Schlick(VoH, F0);
    float D = D_GGX(NoH, a);
    float V = V_SmithGGXCorrelated(NoV, NoL, a);
    float3 diffuseLobe = albedo * (1 - F) / PI;
    float3 specularLobe = F * D * V;

    // 太阳光穿过大气到达着色点的透过率——跟 Atmosphere.slang 渲染天空本体用的是同一张
    // TransmittanceLut、同一套 (高度, 光源天顶角) 参数化, 只是这里只在着色点这一个高度采样
    // 一次(不是沿视线积分)。没有大气的场景 DirectLightingPass 绑的是 DefaultResources 里
    // 完全无内容含义的通用占位 buffer(全零), 这里靠半径退化(topRadiusKm <= bottomRadiusKm,
    // 全零时自然成立)判断"没有真实大气", 整段跳过而不是硬算——大气模型的 LUT 参数化数学
    // 假设了一个半径非零的球壳, 塞退化输入进去必然出 NaN/Inf, 不该指望占位资源填出什么
    // "安全默认值"来兜底
    float3 transmittance = float3(1);
    if (Atmosphere.topRadiusKm > Atmosphere.bottomRadiusKm)
    {
        float3 cameraWorldPosKm = getWorldPos(Atmosphere, viewData.cameraOrigin);
        float distanceToFragmentKm = length(fragPosVS.xyz) * 0.001;
        float3 surfaceWorldPosKm = cameraWorldPosKm - viewDirWS * distanceToFragmentKm;
        float surfaceHeightKm = length(surfaceWorldPosKm);
        float3 upVector = surfaceWorldPosKm / surfaceHeightKm;
        float lightZenithCos = dot(lightDirWS, upVector);
        transmittance = getTransmittance(Atmosphere, TransmittanceLutTexture, LutSampler, lightZenithCos, surfaceHeightKm);
    }

    float3 attenuatedIntensity = lightData.intensity * transmittance;
    float4 color = float4(attenuatedIntensity * (diffuseLobe + specularLobe) * NoL, 1);
    return color;
}