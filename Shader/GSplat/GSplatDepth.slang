#include "../Common/Binding.slang"

struct GPUSplatData
{
    float3 position;
    uint instanceIndex;
    uint3 sigma;
    uint color;
};
[[vk::binding(0, 2)]]
StructuredBuffer<GPUSplatData> sGPUSplatDataBuffer;

[[vk::binding(1, 2)]]
StructuredBuffer<uint> VisibleSplatIndexBuffer;

struct DrawIndirectCommand
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};
[[vk::binding(2, 2)]]
StructuredBuffer<DrawIndirectCommand> DrawIndirectBuffer;

struct V2F
{
    float2 position;
    float4 color;
    float linearDepth;
};

static constexpr float3 kQuadPositions[4] = {
    float3(-1, -1, 0),
    float3(-1,  1, 0),
    float3( 1,  1, 0),
    float3( 1, -1, 0)
};

[shader("vertex")]
void VertexMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID, out V2F v2f, out float4 oPosition : SV_Position)
{
    uint instanceCount = DrawIndirectBuffer[0].instanceCount;
    uint splatIndex = VisibleSplatIndexBuffer[instanceCount - instanceID - 1];
    
    const GPUSplatData splatData = sGPUSplatDataBuffer[splatIndex];

    v2f.position = kQuadPositions[vertexID].xy;
    v2f.color = unpackUnorm4x8ToFloat(splatData.color);

    const InstanceData instanceData = sInstanceData[splatData.instanceIndex];
    const ViewData viewData = sViewData[0];
    float4x4 localToView;
    calculateLocalToView(
        instanceData.localToWorld,
        viewData.worldToViewRot,
        viewData.cameraOrigin,
        localToView
    );
    float4 viewSpace = mul(float4(splatData.position, 1), localToView);
    v2f.linearDepth = -viewSpace.z;
    float4 clipSpace = mul(viewSpace, viewData.viewToClip);
    clipSpace *= 1.0 / clipSpace.w;
    if (any(abs(clipSpace.xyz) > float3(1.0)))
    {
        oPosition = float4(0, 0, 2.0, 1.0);
        return;
    }

    float2 focal = float2(
        0.5 * viewData.resolution.x * viewData.viewToClip[0][0],
        0.5 * viewData.resolution.y * viewData.viewToClip[1][1]
    );
    float3x3 J = float3x3(
        focal.x / viewSpace.z, 0.0, -(focal.x * viewSpace.x) / (viewSpace.z * viewSpace.z),
        0.0, focal.y / viewSpace.z, -(focal.y * viewSpace.y) / (viewSpace.z * viewSpace.z),
        0.0, 0.0, 0.0
    );

    float3x3 W = transpose(float3x3(localToView));

    float3x3 T = mul(J, W);
    float2 u1 = unpackHalf2x16ToFloat(splatData.sigma[0]);
    float2 u2 = unpackHalf2x16ToFloat(splatData.sigma[1]);
    float2 u3 = unpackHalf2x16ToFloat(splatData.sigma[2]);
    float3x3 Vrk = float3x3(
        u1.x, u1.y, u2.x,
        u1.y, u2.y, u3.x,
        u2.x, u3.x, u3.y
    ) * 4;
    float3x3 cov2d = mul(mul(T, Vrk), transpose(T));

    float diagonal1 = cov2d[0][0] + 0.3;
    float offDiagonal = cov2d[0][1];
    float diagonal2 = cov2d[1][1] + 0.3;

    float mid = (diagonal1 + diagonal2) * 0.5;
    float radius = length(float2((diagonal1 - diagonal2) * 0.5, offDiagonal));
    float lambda1 = mid + radius;
    float lambda2 = max(mid - radius, 0.1);
    float vmin = min(1024.0, min(viewData.resolution.x, viewData.resolution.y));
    float l1 = 2.0 * min(sqrt(2.0 * lambda1), vmin);
    float l2 = 2.0 * min(sqrt(2.0 * lambda2), vmin);

    float2 diagonalVector = normalize(float2(offDiagonal, lambda1 - diagonal1));
    float2 majorAxis = l1 * diagonalVector;
    float2 minorAxis = l2 * float2(diagonalVector.y, -diagonalVector.x);

    float2 vCenter = clipSpace.xy;
    oPosition = float4(
        vCenter
        + kQuadPositions[vertexID].x * majorAxis * viewData.resolution.zw
        + kQuadPositions[vertexID].y * minorAxis * viewData.resolution.zw,
        clipSpace.z, 1.0
    );
}

[shader("fragment")]
void FragmentMain(in V2F v2f, out float4 oColor : SV_Target)
{
    float A = dot(v2f.position, v2f.position);
    if (A > 1.0)
        discard;
    float B = exp(-A * 4.0) * v2f.color.a;

    oColor = float4(float3(v2f.linearDepth * B), 1.0 - B);
}