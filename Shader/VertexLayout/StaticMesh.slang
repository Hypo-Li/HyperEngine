#pragma once

#include <Common/Binding.slang>
#include <VertexLayout/VertexInput.slang>

// draw-call 内实例号(SV_InstanceID)到 meshInstanceIndex 的重映射表, 每个 draw pass 当帧重建当帧读
[[vk::binding(1, 3)]]
StructuredBuffer<uint> sIndices;

[[vk::binding(2, 3)]]
StructuredBuffer<MeshInstanceData> sMeshInstanceData;

uint getInstanceIndex(in VSInput vsi)
{
    return sMeshInstanceData[sIndices[vsi.instanceID]].instanceIndex;
}

VertexGeometry getVertexGeometry(in VSInput vsi)
{
    VertexGeometry geo;
#if VERTEX_HAS_POSITION
    geo.position = vsi.position;
#else
    geo.position = float3(0);
#endif
#if VERTEX_HAS_NORMAL
    geo.normal = vsi.normal;
#else
    geo.normal = float3(0, 0, 1);
#endif
#if VERTEX_HAS_TANGENT
    geo.tangent = vsi.tangent;
#else
    geo.tangent = float4(1, 0, 0, 1);
#endif
    return geo;
}

float3 getPrevPosition(in VSInput vsi)
{
#if VERTEX_HAS_POSITION
    return vsi.position;
#else
    return float3(0);
#endif
}
