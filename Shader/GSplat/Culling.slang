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
StructuredBuffer<GPUSplatData> GPUSplatDataBuffer;

[[vk::binding(1, 2)]]
RWStructuredBuffer<uint> VisibleSplatIndexBuffer;

struct DrawIndirectCommand
{
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};
struct DrawIndirectCommandAtomic
{
    uint vertexCount;
    Atomic<uint> instanceCount;
    uint firstVertex;
    uint firstInstance;
};
[[vk::binding(2, 2)]]
RWStructuredBuffer<DrawIndirectCommandAtomic> DrawIndirectCommandBuffer;

[[vk::binding(3, 2)]]
RWStructuredBuffer<uint> VisibleSplatDistanceBuffer;

[[vk::push_constant]]
uint kSplatCount;

#define GROUP_THREAD_COUNT 256
#define MIN_WAVE_LANE_COUNT 32
#define MAX_GROUP_WAVE_COUNT (GROUP_THREAD_COUNT / MIN_WAVE_LANE_COUNT)

groupshared uint gWaveCount[MAX_GROUP_WAVE_COUNT];
groupshared uint gWaveOffset[MAX_GROUP_WAVE_COUNT];
groupshared uint gGroupBaseOffset;

bool isSplatVisible(uint splatIndex, in ViewData viewData, out float distance)
{
    distance = -1.0;
    if (splatIndex >= kSplatCount)
        return false;

    const GPUSplatData splatData = GPUSplatDataBuffer[splatIndex];
    const InstanceData instanceData = sInstanceData[splatData.instanceIndex];
    if ((instanceData.flags & InstanceFlags::Visible) == InstanceFlags::None)
        return false;

    const float3 splatPosition = splatData.position;
    float4x4 objectToView;
    calculateObjectToView(
        instanceData.objectToWorld,
        viewData.worldToViewRot,
        viewData.cameraOrigin,
        objectToView
    );

    float4 viewSpace = mul(float4(splatData.position, 1), objectToView);
    float4 clipSpace = mul(viewSpace, viewData.viewToClipUnjittered);
    if (any(abs(clipSpace.xyz) > clipSpace.w))
        return false;

//#define CULL_SMALL_SPLAT
#ifdef CULL_SMALL_SPLAT
    float2 focal = float2(
        0.5 * ViewDataBuffer.resolution.x * ViewDataBuffer.viewToClip[0][0],
        0.5 * ViewDataBuffer.resolution.y * ViewDataBuffer.viewToClip[1][1]
    );
    float4x4 objectToWorld = float4x4(
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    );
    float4x4 objectToView = mul(objectToWorld, ViewDataBuffer.worldToView);
    float4 viewSpace = mul(float4(splatData.position, 1), objectToView);

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
    float vmin = min(1024.0, min(ViewDataBuffer.resolution.x, ViewDataBuffer.resolution.y));
    float l1 = 2.0 * min(sqrt(2.0 * lambda1), vmin);
    float l2 = 2.0 * min(sqrt(2.0 * lambda2), vmin);

    #define PIXEL_THRESHOLD 4.0
    const float area = l1 * l2 * 0.5;
    if (area < PIXEL_THRESHOLD)
    {
        return false;
    }

#endif

    distance = -viewSpace.z;
    if (distance < 0.0)
        return false;

    return true;
}

// 计算 uint4 ballot 中 [0 .. laneIndex-1] 的可见数量
uint prefixFromBallot(uint4 ballot, uint laneIndex)
{
    uint prefix = 0;
    const uint worldIndex = laneIndex / 32;
    const uint bitIndex = laneIndex % 32;

    // 计算完整 word 的 bitcount
    for (uint w = 0; w < worldIndex; ++w)
        prefix += countbits(ballot[w]);

    // 最后一个 word 只取前 bitIndex 位
    const uint mask = (1u << bitIndex) - 1u;
    prefix += countbits(ballot[worldIndex] & mask);

    return prefix;
}

// 计算整个 ballot 的 bit count
uint countBallot(uint4 ballot)
{
    return countbits(ballot.x) +
           countbits(ballot.y) +
           countbits(ballot.z) +
           countbits(ballot.w);
}

[shader("compute")]
[numthreads(GROUP_THREAD_COUNT, 1, 1)]
void ComputeMain(uint3 globalID : SV_DispatchThreadID, uint groupLocalIndex : SV_GroupIndex)
{
    const uint splatIndex = globalID.x;

    float distance;
    const bool visible = isSplatVisible(splatIndex, uViewData, distance);

    const uint4 ballot = WaveActiveBallot(visible);

    const uint laneIndex = WaveGetLaneIndex();
    const uint prefix = prefixFromBallot(ballot, laneIndex);
    // wave 中可见的 splat 数目
    const uint waveCount = countBallot(ballot);

    const uint laneCount = WaveGetLaneCount();
    const uint waveID = groupLocalIndex / laneCount;
    if (laneIndex == 0)
        gWaveCount[waveID] = waveCount;

    GroupMemoryBarrierWithGroupSync();

    if (groupLocalIndex == 0)
    {
        uint running = 0;
        const uint groupWaveCount = GROUP_THREAD_COUNT / laneCount;
        for (uint i = 0; i < groupWaveCount; ++i)
        {
            gWaveOffset[i] = running;
            running += gWaveCount[i];
        }

        uint base = DrawIndirectCommandBuffer[0].instanceCount.add(running);
        gGroupBaseOffset = base;
    }

    GroupMemoryBarrierWithGroupSync();

    const uint finalOffset = gGroupBaseOffset + gWaveOffset[waveID] + prefix;

    if (visible)
    {
        VisibleSplatIndexBuffer[finalOffset] = splatIndex;
        VisibleSplatDistanceBuffer[finalOffset] = ~0u - asuint(distance);
    }
}