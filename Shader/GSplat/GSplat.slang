#include "../Common/Binding.slang"

[[vk::binding(0, 3)]]
StructuredBuffer<InstanceData> sInstanceData;

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

// 新 RHI 的 PrimitiveTopology 没有 Triangle_Fan(WebGPU 不支持),所以这里用 Triangle_Strip 画四边形,
// 顶点顺序必须是 strip 的 "Z" 字形(左下, 右下, 左上, 右上),不能再用 fan 那种绕一圈的顺序,
// 否则每个 splat 只会画出四边形里的一个三角形(4 个顶点按 list 只够凑 1 个三角形,第 4 个顶点被丢弃)
static constexpr float3 kQuadPositions[4] = {
    float3(-1, -1, 0),
    float3(-1,  1, 0),
    float3( 1, -1, 0),
    float3( 1,  1, 0)
};

struct V2F
{
    float4 svPosition : SV_Position;
    float2 quadPosition : TEXCOORD0;
    float4 color : TEXCOORD1;
};

[shader("vertex")]
V2F VertexMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    uint splatIndex = VisibleSplatIndexBuffer[instanceID];
    
    const GPUSplatData splatData = sGPUSplatDataBuffer[splatIndex];

    V2F v2f;
    v2f.quadPosition = kQuadPositions[vertexID].xy;
    v2f.color = unpackUnorm4x8ToFloat(splatData.color);

    const InstanceData instanceData = sInstanceData[splatData.instanceIndex];
    float4x4 objectToView;
    calculateObjectToView(
        instanceData.objectToWorld,
        uViewData.worldToViewRot,
        uViewData.cameraOrigin,
        objectToView
    );
    float4 viewSpace = mul(float4(splatData.position, 1), objectToView);
    float4 clipSpace = mul(viewSpace, uViewData.viewToClipUnjittered);
    clipSpace *= 1.0 / clipSpace.w;
    if (any(abs(clipSpace.xyz) > float3(1.0)))
    {
        v2f.svPosition = float4(0, 0, 2.0, 1.0);
        return v2f;
    }

    float2 focal = float2(
        0.5 * uViewData.resolution.x * uViewData.viewToClipUnjittered[0][0],
        0.5 * uViewData.resolution.y * uViewData.viewToClipUnjittered[1][1]
    );
    float3x3 J = float3x3(
        focal.x / viewSpace.z, 0.0, -(focal.x * viewSpace.x) / (viewSpace.z * viewSpace.z),
        0.0, focal.y / viewSpace.z, -(focal.y * viewSpace.y) / (viewSpace.z * viewSpace.z),
        0.0, 0.0, 0.0
    );

    float3x3 W = transpose(float3x3(objectToView));

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
    float vmin = min(1024.0, min(uViewData.resolution.x, uViewData.resolution.y));
    float l1 = 2.0 * min(sqrt(2.0 * lambda1), vmin);
    float l2 = 2.0 * min(sqrt(2.0 * lambda2), vmin);

    float2 diagonalVector = normalize(float2(offDiagonal, lambda1 - diagonal1));
    float2 majorAxis = l1 * diagonalVector;
    float2 minorAxis = l2 * float2(diagonalVector.y, -diagonalVector.x);

    float2 vCenter = clipSpace.xy;
    v2f.svPosition = float4(
        vCenter
        + kQuadPositions[vertexID].x * majorAxis * uViewData.resolution.zw
        + kQuadPositions[vertexID].y * minorAxis * uViewData.resolution.zw,
        clipSpace.z, 1.0
    );
    return v2f;
}

[shader("fragment")]
float4 FragmentMain(in V2F v2f) : SV_Target
{
    float A = dot(v2f.quadPosition, v2f.quadPosition);
    if (A > 1.0)
        discard;
    float B = exp(-A * 4.0) * v2f.color.a;
    return float4(v2f.color.rgb, B);
}
