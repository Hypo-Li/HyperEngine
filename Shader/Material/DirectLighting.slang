#include "../Common/Binding.slang"

[[vk::binding(0, 2)]]
Texture2D<float4> GbufferAImage;

[[vk::binding(1, 2)]]
Texture2D<float4> GbufferBImage;

[[vk::binding(2, 2)]]
Texture2D<float4> GbufferCImage;

[[vk::binding(3, 2)]]
Texture2D<float> SceneDepthImage;

[[vk::push_constant]]
uint kLightIndex;

static constexpr float2 kPositions[3] = {
    float2(-1, -3),
    float2( 3,  1),
    float2(-1,  1)
};

[shader("vertex")]
void VertexMain(in uint vertexID : SV_VertexID, out float4 oPosition : SV_Position)
{
    oPosition = float4(kPositions[vertexID], 0, 1);
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

static constexpr float PI = 3.1415926535897932384626433832795;

[shader("fragment")]
void FragmentMain(in float4 svPosition : SV_Position, out float4 oColor : SV_Target)
{
    float sceneDepth = SceneDepthImage[uint2(svPosition.xy)].r;
    if (sceneDepth == 0)
    {
        oColor = float4(0, 0, 0, 1);
        return;
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
    
    float3 lightDirWS = -sLightData[kLightIndex].direction;
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
    oColor = float4(6.0 * (diffuseLobe + specularLobe) * NoL + (0.5 * albedo), 1);
    oColor.rgb = pow(oColor.rgb, float3(1.0 / 2.2));

}