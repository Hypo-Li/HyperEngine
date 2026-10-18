#pragma once

#include <Common/Binding.slang>
#include <VertexLayout/VertexInput.slang>

uint4 getBone0(in VSInput vsi)
{
#if VERTEX_HAS_BONE_WEIGHT0
    return vsi.bone0;
#else
    return uint4(0, 0, 0, 0);
#endif
}

float4 getWeight0(in VSInput vsi)
{
#if VERTEX_HAS_BONE_WEIGHT0
    return vsi.weight0;
#else
    return float4(1, 0, 0, 0);
#endif
}

uint4 getBone1(in VSInput vsi)
{
#if VERTEX_HAS_BONE_WEIGHT1
    return vsi.bone1;
#else
    return uint4(0, 0, 0, 0);
#endif
}

float4 getWeight1(in VSInput vsi)
{
#if VERTEX_HAS_BONE_WEIGHT1
    return vsi.weight1;
#else
    return float4(0, 0, 0, 0);
#endif
}

// draw-call 内实例号(SV_InstanceID)到 meshInstanceIndex 的重映射表, 每个 draw pass 当帧重建当帧读
[[vk::binding(1, 3)]]
StructuredBuffer<uint> sIndices;

[[vk::binding(2, 3)]]
StructuredBuffer<MeshInstanceData> sMeshInstanceData;

[[vk::binding(3, 3)]]
StructuredBuffer<float3x4> sSkinningMatrixTable;

static const float3x4 kIdentitySkinningMatrix = float3x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0);

// 跟 hyp::kInvalidBoneMatrixRangeBase(SceneProxyData.h)必须保持一致
static const uint kInvalidBoneMatrixRangeBase = 0xFFFFFFFFu;

// 每列的 .w 存的是平移的对应分量
float3x4 computeSkinningMatrix(in VSInput vsi, uint boneMatrixRangeBase)
{
    if (boneMatrixRangeBase == kInvalidBoneMatrixRangeBase)
        return kIdentitySkinningMatrix;

    float3x4 result = float3x4(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    uint4 bone0 = getBone0(vsi);
    float4 weight0 = getWeight0(vsi);
    for (int k = 0; k < 4; ++k)
    {
        float3x4 m = sSkinningMatrixTable[boneMatrixRangeBase + bone0[k]];
        result[0] += weight0[k] * m[0];
        result[1] += weight0[k] * m[1];
        result[2] += weight0[k] * m[2];
    }
#if VERTEX_HAS_BONE_WEIGHT1
    uint4 bone1 = getBone1(vsi);
    float4 weight1 = getWeight1(vsi);
    for (int k = 0; k < 4; ++k)
    {
        float3x4 m = sSkinningMatrixTable[boneMatrixRangeBase + bone1[k]];
        result[0] += weight1[k] * m[0];
        result[1] += weight1[k] * m[1];
        result[2] += weight1[k] * m[2];
    }
#endif
    return result;
}

MeshInstanceData getMeshInstanceData(in VSInput vsi)
{
    return sMeshInstanceData[sIndices[vsi.instanceID]];
}

uint getInstanceIndex(in VSInput vsi)
{
    return getMeshInstanceData(vsi).instanceIndex;
}

VertexGeometry getVertexGeometry(in VSInput vsi)
{
    VertexGeometry geo;
    float3x4 skin = computeSkinningMatrix(vsi, getMeshInstanceData(vsi).boneMatrixRangeBase);
    float3 skinTranslation = float3(skin[0].w, skin[1].w, skin[2].w);
#if VERTEX_HAS_POSITION
    geo.position = mul(vsi.position, skin).xyz + skinTranslation;
#else
    geo.position = float3(0);
#endif
#if VERTEX_HAS_NORMAL
    geo.normal = mul(vsi.normal, skin).xyz;
#else
    geo.normal = float3(0, 0, 1);
#endif
#if VERTEX_HAS_TANGENT
    geo.tangent = float4(mul(vsi.tangent.xyz, skin).xyz, vsi.tangent.w);
#else
    geo.tangent = float4(1, 0, 0, 1);
#endif
    return geo;
}

float3 getPrevPosition(in VSInput vsi)
{
#if VERTEX_HAS_POSITION
    float3x4 prevSkin = computeSkinningMatrix(vsi, getMeshInstanceData(vsi).prevBoneMatrixRangeBase);
    return mul(vsi.position, prevSkin).xyz + float3(prevSkin[0].w, prevSkin[1].w, prevSkin[2].w);
#else
    return float3(0);
#endif
}
