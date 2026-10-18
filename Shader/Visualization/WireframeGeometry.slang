#include "WireframeCommon.slang"

// GeometryProxy(ConvexHull/Mesh 碰撞体复用同一份渲染用三角网格)的线框可视化——不能直接把三角形
// index buffer 当 Line_List 数据画: index buffer 是按 3 个一组写的, Line_List 按 2 个一组消费,
// 3 和 2 不对齐, 每两个三角形才重新对齐一次, 中间会画出连着两个不相关三角形的假边(具体推导见
// 设计讨论), 也没有额外建一份去重后的边索引 buffer(不想为了画线单独占内存)。这里改成
// vertex-pulling: 把 GeometryProxy 那块顶点+索引数据整个当只读 uint 数组绑进来(binding=1,
// CPU 侧 GeometryProxy::getBuffer() 就是同一块已经在正常当 Vertex/Index buffer 用的 buffer,
// 额外多绑了 Storage usage), 每个 Line_List 顶点(SV_VertexID)现算要读哪个三角形的哪条边、
// 边的哪个端点, 进而查哪个顶点的哪个分量——不经过任何固定管线的顶点/索引抓取阶段。三角形内部
// 共享边会被相邻三角形各自画一次(轻微重叠, 不算错误, 只有真正只属于一个三角形的边界/轮廓边
// 才画一次)
struct VisualizationWireframeGeometryInstance
{
    WorldMatrix transform;
    float4 color;
    uint pickId;
    uint ignoreDepth;
    uint _pad0[2];
};
[[vk::binding(0, 2)]]
StructuredBuffer<VisualizationWireframeGeometryInstance> gInstances;

[[vk::binding(1, 2)]]
StructuredBuffer<uint> gGeometryData;

struct WireframeGeometryPushConstants
{
    uint instanceOffset;
    uint positionUintOffset;
    uint indexBaseUint;
    uint firstIndex;    // 逻辑索引元素下标, 不是 uint/字节偏移
    int vertexOffset;
    uint indexIs16Bit;  // 0 = Uint32(一个索引一个 uint), 1 = Uint16(两个索引共享一个 uint)
};
[[vk::push_constant]]
WireframeGeometryPushConstants kPushConstants;

float3 fetchPosition(uint vertexIndex)
{
    uint base = kPushConstants.positionUintOffset + vertexIndex * 3;
    return asfloat(uint3(gGeometryData[base], gGeometryData[base + 1], gGeometryData[base + 2]));
}

// globalIndex16 是逻辑索引元素下标(第几个索引, 不管索引本身是 16 位还是 32 位都从 0 数起)。
// Uint32: 一个索引正好占一个 uint, 直接按下标读。Uint16: 两个索引紧凑共享一个 uint(小端序,
// 偶数下标在低 16 位、奇数下标在高 16 位), 先算这个索引落在哪个 word(下标/2), 读出那个 word
// 再按下标奇偶取低/高 16 位
uint fetchIndex(uint globalIndex16)
{
    if (kPushConstants.indexIs16Bit == 0)
    {
        return gGeometryData[kPushConstants.indexBaseUint + globalIndex16];
    }
    uint packedWord = gGeometryData[kPushConstants.indexBaseUint + globalIndex16 / 2];
    return (globalIndex16 % 2 == 0) ? (packedWord & 0xFFFFu) : (packedWord >> 16);
}

[shader("vertex")]
VaryingsWireframe VertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VisualizationWireframeGeometryInstance instance = gInstances[kPushConstants.instanceOffset + instanceId];

    uint triangleIndex = vertexId / 6;
    uint edgeInTriangle = (vertexId / 2) % 3;
    uint endpoint = vertexId % 2;

    uint indexBase16 = kPushConstants.firstIndex + triangleIndex * 3;
    uint i0 = fetchIndex(indexBase16 + 0);
    uint i1 = fetchIndex(indexBase16 + 1);
    uint i2 = fetchIndex(indexBase16 + 2);

    uint2 edge = edgeInTriangle == 0 ? uint2(i0, i1) : (edgeInTriangle == 1 ? uint2(i1, i2) : uint2(i2, i0));
    uint fetchedIndex = endpoint == 0 ? edge.x : edge.y;
    uint vertexIndex = uint(int(fetchedIndex) + kPushConstants.vertexOffset);

    float3 localPos = fetchPosition(vertexIndex);

    VaryingsWireframe output;
    output.svPosition = applyIgnoreDepth(computeWireframeClipPosition(localPos, instance.transform), instance.ignoreDepth);
    output.color = instance.color;
    return output;
}

[shader("fragment")]
float4 FragmentMain(VaryingsWireframe input) : SV_Target
{
    return input.color;
}
