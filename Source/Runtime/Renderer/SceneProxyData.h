#pragma once
#include <RenderBase/RenderBaseDefines.h>
#include <Core/Common/EnumBitwise.h>

namespace hyp
{
    enum class InstanceFlags : uint32_t
    {
        None = 0,
        Visible = 1u << 0,
        Billboard_Screen = 1u << 1,
        Billboard_Axis = 1u << 2,
    };
    ENABLE_ENUM_CLASS_BITWISE(InstanceFlags)

    struct InstanceData
    {
        WorldMatrix objectToWorld;
        float3x4 objectToWorldRot;       // 纯 Rotation, 不含 scale——给光源方向这类"不该被 scale 影响"的方向向量用
        float3x4 objectToWorldRS;        // R*S, 不求逆不转置——给切线这类"该跟着表面一起形变"的向量用
        float3x4 objectToWorldNormalRot; // inverse(transpose(RS))——专给法线用, 非均匀缩放下也能保持垂直关系
        WorldMatrix prevObjectToWorld;
        float3x4 worldToObjectRS;
        InstanceFlags flags;
        uint32_t padding0;
        uint32_t padding1;
        uint32_t padding2;
    };
    static_assert(sizeof(InstanceData) == 336, "InstanceData 必须和 Binding.slang 里的 std430 布局(步长 336 字节)一致");

    enum class LightType : uint32_t
    {
        Directional,
        Point,
        Spot,
    };

    struct LightData
    {
        LightType type;
        uint32_t castShadows;
        uint32_t instanceIndex;
        uint32_t padding0;
        float3 intensity;
        float attenuationRadius;     // 只对 Point/Spot 有意义
        float sourceRadius;          // 只对 Point/Spot 有意义
        float cosOuterCone;          // 只对 Spot 有意义
        float invCosConeDifference;  // 只对 Spot 有意义
        // 只对 Directional 有意义: LumenScene::DirectLightingShadowTileGrid 在共享 buffer
        // 里的起始下标, kInvalidCardAtlasPageIndex 哨兵值表示没有
        uint32_t shadowTileGridOffset;
    };

    // 逐 MeshRenderProxy 实例的目录表, 存各子系统里这个实例数据的偏移, 数据和分配归各子系统, 见 hyp::SceneProxy::getMeshInstanceData()
    struct MeshInstanceData
    {
        uint32_t instanceIndex; // 反查通用 InstanceData 用
        uint32_t meshShapeDataOffset;
        uint32_t cardRangeSlotBase;
        uint32_t boneMatrixRangeBase;
        uint32_t prevBoneMatrixRangeBase;
        uint32_t sectionTableBase;
    };

    inline constexpr uint32_t kInvalidBoneMatrixRangeBase = 0xFFFFFFFFu;
    inline constexpr uint32_t kInvalidSectionTableBase = 0xFFFFFFFFu;

    struct MeshSectionEntry
    {
        uint32_t geometryId; // 没有 Geometry(纯顶点拉取)时为 0
        uint32_t materialId;
    };
}
