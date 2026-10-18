#pragma once
#include <Core/Common/HString.h>

#include <array>
#include <cstdint>
#include <vector>

namespace hyp
{
    // 与 Camera::mCullingMask 同为 32 位掩码。
    inline constexpr uint32_t kMaxPhysicsLayers = 32;

    enum class PhysicsLayerRelation : uint8_t
    {
        Ignore,
        Overlap,
        Block,
    };

    struct PhysicsLayerRelationEntry
    {
        HString a;
        HString b;
        PhysicsLayerRelation relation = PhysicsLayerRelation::Block;
    };

    using PhysicsLayerRelationTable = std::array<std::array<PhysicsLayerRelation, kMaxPhysicsLayers>, kMaxPhysicsLayers>;

    struct PhysicsLayerSettings
    {
        // 下标 i 对应槽位 i + 1, 槽位 0 固定是 Default, 不出现在这里。
        std::vector<HString> layerNames;

        // 没列出的层对关系是 Block。
        std::vector<PhysicsLayerRelationEntry> relations;

        static HString getDefaultLayerName()
        {
            return "Default"_hs;
        }
    };
}
