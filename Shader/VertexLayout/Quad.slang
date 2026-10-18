#pragma once

#include <VertexLayout/VertexInput.slang>

struct QuadElement
{
    float2 position;
    float2 size;
    uint uvMin;
    uint uvMax;
    uint pageAndFlags;
    uint color;
};

struct QuadInstanceData
{
    uint instanceIndex;
    uint elementBase;
    uint elementCount;
    uint tint;
    float2 blockSize;
    float2 anchor;
    float depthBias;
    float autoScaleStrength;
    float referenceDistance;
    float minScale;
    float maxScale;
    float reserved;
};

// draw-call 内实例号(SV_InstanceID)到 QuadInstanceData 下标的重映射表, 每个 draw pass 当帧重建当帧读
[[vk::binding(1, 3)]]
StructuredBuffer<uint> sIndices;

[[vk::binding(2, 3)]]
StructuredBuffer<QuadElement> sQuadElements;

[[vk::binding(3, 3)]]
StructuredBuffer<QuadInstanceData> sQuadInstanceData;

float2 unpackUnorm16x2(uint packed)
{
    return float2(packed & 0xFFFFu, packed >> 16) / 65535.0;
}

float4 unpackRGBA8(uint packed)
{
    return float4(packed & 0xFFu, (packed >> 8) & 0xFFu, (packed >> 16) & 0xFFu, packed >> 24) / 255.0;
}

QuadInstanceData getQuadInstanceData(in VSInput vsi)
{
    return sQuadInstanceData[sIndices[vsi.instanceID]];
}

uint getQuadIndex(in VSInput vsi)
{
    return vsi.vertexID / 6u;
}

// 每个四边形是两个三角形 (0,0) (1,0) (0,1) 和 (0,1) (1,0) (1,1)
float2 getQuadCorner(in VSInput vsi)
{
    static const uint kCornerIndices[6] = { 0, 1, 2, 2, 1, 3 };
    uint corner = kCornerIndices[vsi.vertexID % 6u];
    return float2(corner & 1u, corner >> 1);
}

bool isQuadValid(in VSInput vsi, in QuadInstanceData quadInstance)
{
    return getQuadIndex(vsi) < quadInstance.elementCount;
}

QuadElement getQuadElement(in VSInput vsi, in QuadInstanceData quadInstance)
{
    uint lastIndex = quadInstance.elementCount > 0u ? quadInstance.elementCount - 1u : 0u;
    return sQuadElements[quadInstance.elementBase + min(getQuadIndex(vsi), lastIndex)];
}

uint getInstanceIndex(in VSInput vsi)
{
    return getQuadInstanceData(vsi).instanceIndex;
}

float2 getQuadLocalPosition(in VSInput vsi, in QuadInstanceData quadInstance)
{
    if (!isQuadValid(vsi, quadInstance))
        return float2(0);

    QuadElement element = getQuadElement(vsi, quadInstance);
    return element.position + getQuadCorner(vsi) * element.size - quadInstance.anchor * quadInstance.blockSize;
}

struct BillboardFrame
{
    float3 right;
    float3 up;
    float3 back;
};

// Axis 模式在相机恰好沿物体 Z 轴看过去时退化成 Screen 模式
BillboardFrame getBillboardFrame(in InstanceData instance, in float3x3 worldToViewRot)
{
    BillboardFrame frame;
    frame.right = mul(worldToViewRot, float3(1, 0, 0));
    frame.up = mul(worldToViewRot, float3(0, 1, 0));
    frame.back = mul(worldToViewRot, float3(0, 0, 1));

    if ((instance.flags & InstanceFlags::Billboard_Axis) != InstanceFlags::None)
    {
        const float3 axis = normalize(instance.objectToWorldRS[2]);
        const float3 facing = frame.back - dot(frame.back, axis) * axis;
        const float facingLength = length(facing);
        if (facingLength > 1e-4)
        {
            frame.back = facing / facingLength;
            frame.up = axis;
            frame.right = cross(-frame.back, axis);
        }
    }
    return frame;
}

bool isQuadBillboard(in InstanceData instance)
{
    return (instance.flags & (InstanceFlags::Billboard_Screen | InstanceFlags::Billboard_Axis)) != InstanceFlags::None;
}

// 依赖当前视图的自动缩放和深度偏移, 阴影 pass 的视图是光源, 要在包含这个文件之前把它定义成 0
#ifndef QUAD_VIEW_DEPENDENT_ADJUST
    #define QUAD_VIEW_DEPENDENT_ADJUST 1
#endif

// strength 为 0 时恒为 1, 为 1 时缩放跟距离成正比(屏幕尺寸恒定), referenceDistance 处缩放为 1
float getQuadAutoScale(in QuadInstanceData quadInstance, in float3 relativeOrigin, in float3x3 worldToViewRot)
{
#if QUAD_VIEW_DEPENDENT_ADJUST
    if (quadInstance.autoScaleStrength <= 0.0)
        return 1.0;

    const float viewDepth = max(-mul(relativeOrigin, worldToViewRot).z, 1e-4);
    const float scale = lerp(1.0, viewDepth / max(quadInstance.referenceDistance, 1e-4), quadInstance.autoScaleStrength);
    return clamp(scale, quadInstance.minScale, quadInstance.maxScale);
#else
    return 1.0;
#endif
}

VertexGeometry makeQuadGeometry(
    in InstanceData instance,
    in QuadInstanceData quadInstance,
    in WorldMatrix objectToWorld,
    in float3x3 worldToViewRot,
    in WorldCoord cameraOrigin,
    in float2 unscaledLocal)
{
    const float3 relativeOrigin = calculateRelativeWorldPosition(objectToWorld.getTranslation(), cameraOrigin);
    const float2 local = unscaledLocal * getQuadAutoScale(quadInstance, relativeOrigin, worldToViewRot);

    VertexGeometry geo;
    if (isQuadBillboard(instance))
    {
        const BillboardFrame frame = getBillboardFrame(instance, worldToViewRot);
        const float3 scale = instanceWorldScale(instance);
        const float3 offsetWS = frame.right * (local.x * scale.x) + frame.up * (local.y * scale.z);
        geo.position = mul(offsetWS, instance.worldToObjectRS);
        geo.normal = mul(instance.objectToWorldRS, frame.back);
        geo.tangent = float4(mul(frame.right, instance.worldToObjectRS), 1);
    }
    else
    {
        // 引擎 Z 朝上, 排版平面是 XZ 平面, 正面朝 -Y
        geo.position = float3(local.x, 0, local.y);
        geo.normal = float3(0, -1, 0);
        geo.tangent = float4(1, 0, 0, 1);
    }

#if QUAD_VIEW_DEPENDENT_ADJUST
    if (quadInstance.depthBias != 0.0)
    {
        const float3 relativePosition = relativeOrigin + mul(geo.position, instance.objectToWorldRS);
        const float3 towardCamera = -normalize(relativePosition);
        geo.position += mul(towardCamera * quadInstance.depthBias, instance.worldToObjectRS);
    }
#endif
    return geo;
}

VertexGeometry getVertexGeometry(in VSInput vsi)
{
    QuadInstanceData quadInstance = getQuadInstanceData(vsi);
    InstanceData instance = sInstanceData[quadInstance.instanceIndex];
    return makeQuadGeometry(
        instance, quadInstance, instance.objectToWorld, uViewData.worldToViewRot, uViewData.cameraOrigin,
        getQuadLocalPosition(vsi, quadInstance));
}

float3 getPrevPosition(in VSInput vsi)
{
    QuadInstanceData quadInstance = getQuadInstanceData(vsi);
    InstanceData instance = sInstanceData[quadInstance.instanceIndex];
    return makeQuadGeometry(
        instance, quadInstance, instance.prevObjectToWorld, uViewData.prevWorldToViewRot, uViewData.prevCameraOrigin,
        getQuadLocalPosition(vsi, quadInstance)).position;
}

float4 getColor(in VSInput vsi)
{
    QuadInstanceData quadInstance = getQuadInstanceData(vsi);
    return unpackRGBA8(getQuadElement(vsi, quadInstance).color) * unpackRGBA8(quadInstance.tint);
}

// 图集 UV, 纹理坐标 v 向下, 所以角的 y 越大越靠近 uvMin.y
float2 getTexCoord0(in VSInput vsi)
{
    QuadElement element = getQuadElement(vsi, getQuadInstanceData(vsi));
    float2 corner = getQuadCorner(vsi);
    float2 uvMin = unpackUnorm16x2(element.uvMin);
    float2 uvMax = unpackUnorm16x2(element.uvMax);
    return float2(lerp(uvMin.x, uvMax.x, corner.x), lerp(uvMax.y, uvMin.y, corner.y));
}

// 四边形内 (0..1) 的局部坐标, y 向上
float2 getTexCoord1(in VSInput vsi)
{
    return getQuadCorner(vsi);
}

// x 是页号, y 保留
float2 getTexCoord2(in VSInput vsi)
{
    QuadElement element = getQuadElement(vsi, getQuadInstanceData(vsi));
    return float2(element.pageAndFlags & 0xFFFFu, 0);
}

// 文字块内的归一化坐标 (0..1)
float2 getTexCoord3(in VSInput vsi)
{
    QuadInstanceData quadInstance = getQuadInstanceData(vsi);
    QuadElement element = getQuadElement(vsi, quadInstance);
    float2 local = element.position + getQuadCorner(vsi) * element.size;
    return local / max(quadInstance.blockSize, float2(1e-6));
}
