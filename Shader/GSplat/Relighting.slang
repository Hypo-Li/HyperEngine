#include "../Common/Binding.slang"

[[vk::binding(0, 2)]]
Texture2D<float4> GSplatDepthImage;

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

float3 getViewPosByLinearDepth(float2 uv, float linearDepth, in ViewData viewData)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos = float4(ndc, 1.0, 1.0);
    float4 viewRay = mul(clipPos, viewData.clipToView);
    return (viewRay.xyz / viewRay.z) * linearDepth;
}

float3 getViewPosByDepth(float2 uv, float depth, in ViewData viewData)
{
    float4 clipPos = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(clipPos, viewData.clipToView);
    return viewPos.xyz / viewPos.w;
}

static constexpr uint kStep = 1;

[shader("fragment")]
void FragmentMain(in float4 svPosition : SV_Position, out float4 oColor : SV_Target)
{
    uint2 pixelCoord = uint2(svPosition.xy);
    float2 depthAndTrans = GSplatDepthImage[pixelCoord].ra;
    float d = depthAndTrans.x;
    float trans = depthAndTrans.y;
    if (d == 0 || trans > 0.1)
    {
        oColor = float4(1, 1, 1, 1);
        return;
    }
    ViewData viewData = sViewData[0];
    uint2 rightPixelCoord = pixelCoord.x > (uint(viewData.resolution.x) - kStep) ? pixelCoord : uint2(svPosition.xy) + uint2(kStep, 0);
    uint2 upPixelCoord = pixelCoord.y < kStep ? pixelCoord : uint2(svPosition.xy) - uint2(0, kStep);
    float dRight = GSplatDepthImage[rightPixelCoord].r;
    float dUp = GSplatDepthImage[upPixelCoord].r;

    float2 uv = float2(svPosition.xy * viewData.resolution.zw);
    float3 posCenter = getViewPosByLinearDepth(uv, d, viewData);
    float3 posRight = getViewPosByLinearDepth(uv + float2(viewData.resolution.z * float(kStep), 0), dRight, viewData);
    float3 posUp = getViewPosByLinearDepth(uv - float2(0, viewData.resolution.w * float(kStep)), dUp, viewData);

    float3 normalVS = normalize(cross(posRight - posCenter, posUp - posCenter));
    float3 normalWS = mul(viewData.worldToViewRot, normalVS);

    float3 lightDir = normalize(float3(1.0, 1.0, 1.0));
    float NoL = max(dot(normalWS, lightDir), 0.5);

    oColor = float4(float3(NoL), 1);
}