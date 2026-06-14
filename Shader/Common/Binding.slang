#pragma once
#include "WorldCoordinate.slang"

[[vk::binding(0, 0)]]
Texture2D BindlessTexture2Ds[];

[[vk::binding(1, 0)]]
Texture2DArray BindlessTexture2DArrays[];

[[vk::binding(2, 0)]]
TextureCube BindlessTextureCubes[];

[[vk::binding(3, 0)]]
TextureCubeArray BindlessTextureCubeArrays[];

[[vk::binding(4, 0)]]
Texture3D BindlessTexture3Ds[];

[[vk::binding(5, 0)]]
SamplerState BindlessSamplers[];

struct FrameData
{
    float wrappedTime;
    uint timeWrapCount;
    float deltaTime;
    uint frameIndex;
};
[[vk::binding(6, 0)]]
ConstantBuffer<FrameData> uFrameData;

struct InstanceData
{
    WorldMatrix localToWorld;
    float3x3 localToWorldRot;
};
[[vk::binding(0, 1)]]
StructuredBuffer<InstanceData> sInstanceData;

[[vk::binding(1, 1)]]
StructuredBuffer<uint> sInstanceIndices;

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
    float3 position;
    float3 direction;
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