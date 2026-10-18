#pragma once
#include "WorldCoordinate.slang"
#include <Common/Defines.slang>

struct FrameData
{
    float wrappedTime;
    uint timeWrapCount;
    float deltaTime;
    uint frameIndex;
    float prevWrappedTime;
    uint prevTimeWrapCount;
    uint padding0;
    uint padding1;
};
[[vk::binding(0, 0)]]
ConstantBuffer<FrameData> uFrameData;

#if RHI_BACKEND == RHI_BACKEND_VULKAN
[[vk::binding(2, 0)]]
Texture2D BindlessTexture2Ds[];

[[vk::binding(3, 0)]]
Texture2DArray BindlessTexture2DArrays[];

[[vk::binding(4, 0)]]
Texture3D BindlessTexture3Ds[];

[[vk::binding(5, 0)]]
TextureCube BindlessTextureCubes[];

[[vk::binding(6, 0)]]
SamplerState BindlessSamplers[];
#endif

[Flags]
enum class InstanceFlags : uint
{
    None = 0,
    Visible = 1 << 0,
    Billboard_Screen = 1 << 1,
    Billboard_Axis = 1 << 2,
};

struct InstanceData
{
    WorldMatrix objectToWorld;
    float3x3 objectToWorldRot;       // 纯 Rotation, 不含 scale——光源方向这类不该被 scale 影响的方向向量用
    float3x3 objectToWorldRS;        // R*S, 不求逆不转置——切线这类该跟着表面一起形变的向量用
    float3x3 objectToWorldNormalRot; // inverse(transpose(RS))——专给法线用
    WorldMatrix prevObjectToWorld;
    float3x3 worldToObjectRS;
    InstanceFlags flags;
    uint padding0;
    uint padding1;
    uint padding2;
};

// 三根物体轴各自变换到世界空间后的长度, 即那根轴上的世界缩放系数
float3 instanceWorldScale(InstanceData instance)
{
    float3x3 rs = instance.objectToWorldRS;
    return float3(length(rs[0]), length(rs[1]), length(rs[2]));
}

enum class LightType : uint
{
    Directional,
    Point,
    Spot
};
struct LightData
{
    LightType type;
    uint castShadows;
    uint instanceIndex;
    uint padding0;
    float3 intensity;
    float attenuationRadius;
    float sourceRadius;
    float cosOuterCone;
    float invCosConeDifference;
    // 只有 Directional 有意义: LumenScene::DirectLightingShadowTileGrid 在共享 buffer 里的
    // 起始下标(见 CardDirectLighting.slang), kInvalidSurfaceCacheAtlasPageIndex 同款哨兵值表示
    // 没有(Point/Spot, 或这盏方向光这一帧没建出候选)
    uint shadowTileGridOffset;
};

// hyp::SceneProxy::getMeshInstanceDataBuffer() 那份逐 MeshRenderProxy 实例的目录表, 跟 hyp::MeshInstanceData(SceneProxyData.h)必须逐字段保持一致; 绑定声明由用到它的 shader 各自负责
struct MeshInstanceData
{
    uint instanceIndex;
    uint meshShapeDataOffset;
    uint cardRangeSlotBase;
    uint boneMatrixRangeBase;
    uint prevBoneMatrixRangeBase;
    uint sectionTableBase;
};

struct ViewData
{
    float3x3 worldToViewRot;
    WorldCoord cameraOrigin;
    float4x4 viewToClip;
    float4x4 clipToView;
    float4x4 viewToClipUnjittered;
    float4x4 clipToViewUnjittered;
    float4 viewport;
    float4 resolution;
    // TAA 用: 这一帧实际用了多大的亚像素抖动(NDC 单位, xy 有效, zw 保留)
    float4 jitterOffset;
    float3x3 prevWorldToViewRot;
    WorldCoord prevCameraOrigin;
    float4x4 prevViewToClipUnjittered;
    float4x4 prevClipToViewUnjittered;
    float preExposure;
    float oneOverPreExposure;
    float prevPreExposure;
    float padding0;
};
[[vk::binding(0, 1)]]
ConstantBuffer<ViewData> uViewData;

float getPreExposure(in ViewData viewData)
{
    return max(viewData.preExposure, 0.0001);
}
