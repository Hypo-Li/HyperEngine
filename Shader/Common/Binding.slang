#pragma once
#include "WorldCoordinate.slang"

struct FrameData
{
    float wrappedTime;
    uint timeWrapCount;
    float deltaTime;
    uint frameIndex;
};
[[vk::binding(0, 0)]]
ConstantBuffer<FrameData> uFrameData;

#ifndef RHI_BACKEND_WEBGPU
[[vk::binding(1, 0)]]
Texture2D BindlessTexture2Ds[];

[[vk::binding(2, 0)]]
Texture2DArray BindlessTexture2DArrays[];

[[vk::binding(3, 0)]]
Texture3D BindlessTexture3Ds[];

[[vk::binding(4, 0)]]
TextureCube BindlessTextureCubes[];

[[vk::binding(5, 0)]]
SamplerState BindlessSamplers[];
#endif

[[vk::binding(0, 1)]]
StructuredBuffer<uint> sIndices;

struct InstanceData
{
    WorldMatrix localToWorld;
    float3x3 localToWorldRot;
};

[[vk::binding(1, 1)]]
StructuredBuffer<InstanceData> sInstanceData;

enum class LightType : uint
{
    Directional,
    Point,
    Spot
};
struct LightData
{
    uint type;
    uint castShadows;
    uint instanceIndex;
    float3 intensity;
};
[[vk::binding(2, 1)]]
StructuredBuffer<LightData> sLightData;

struct ViewData
{
    float3x3 worldToViewRot;
    WorldCoord cameraOrigin;
    float4x4 viewToClip;
    float4x4 clipToView;
    float4 viewport;
    float4 resolution;
};
[[vk::binding(3, 1)]]
StructuredBuffer<ViewData> sViewData;