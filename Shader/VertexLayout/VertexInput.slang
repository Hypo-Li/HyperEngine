#pragma once

struct VSInput
{
#if VERTEX_PULLING
    uint vertexID : SV_VertexID;
#else
#if VERTEX_HAS_POSITION
    float3 position : POSITION;
#endif
#if VERTEX_HAS_NORMAL
    float3 normal : NORMAL;
#endif
#if VERTEX_HAS_TANGENT
    float4 tangent : TANGENT;
#endif
#if VERTEX_HAS_COLOR
    float4 color : COLOR;
#endif
#if VERTEX_HAS_TEXCOORD0
    float2 texCoord0 : TEXCOORD0;
#endif
#if VERTEX_HAS_TEXCOORD1
    float2 texCoord1 : TEXCOORD1;
#endif
#if VERTEX_HAS_TEXCOORD2
    float2 texCoord2 : TEXCOORD2;
#endif
#if VERTEX_HAS_TEXCOORD3
    float2 texCoord3 : TEXCOORD3;
#endif
#if VERTEX_HAS_BONE_WEIGHT0
    uint4 bone0 : BLENDINDICES0;
    float4 weight0 : BLENDWEIGHT0;
#endif
#if VERTEX_HAS_BONE_WEIGHT1
    uint4 bone1 : BLENDINDICES1;
    float4 weight1 : BLENDWEIGHT1;
#endif
#endif
    uint instanceID : SV_VulkanInstanceID;
};

struct VertexGeometry
{
    float3 position;
    float3 normal;
    float4 tangent;
};

#if !VERTEX_PULLING
float4 getColor(in VSInput vsi)
{
#if VERTEX_HAS_COLOR
    return vsi.color;
#else
    return float4(1);
#endif
}

float2 getTexCoord0(in VSInput vsi)
{
#if VERTEX_HAS_TEXCOORD0
    return vsi.texCoord0;
#else
    return float2(0);
#endif
}

float2 getTexCoord1(in VSInput vsi)
{
#if VERTEX_HAS_TEXCOORD1
    return vsi.texCoord1;
#else
    return float2(0);
#endif
}

float2 getTexCoord2(in VSInput vsi)
{
#if VERTEX_HAS_TEXCOORD2
    return vsi.texCoord2;
#else
    return float2(0);
#endif
}

float2 getTexCoord3(in VSInput vsi)
{
#if VERTEX_HAS_TEXCOORD3
    return vsi.texCoord3;
#else
    return float2(0);
#endif
}
#endif
