#pragma once
#include "PhysicsHandle.h"
#include "PhysicsShapeDesc.h"
#include "PhysicsConstraintDesc.h"
#include "CompoundCollision.h"

#include <Core/Common/Guid.h>
#include <Core/Math/Vector.h>

#include <cstdint>
#include <variant>
#include <vector>

namespace hyp
{
    enum class MotionType
    {
        Static,
        Dynamic,
        Kinematic
    };

    template <> Type* Reflection::createType<MotionType>();

    // compoundCollision 为空是合法状态, CompoundCollider::onRegister() 会直接跳过建 Body。
    struct CompoundShapeDesc
    {
        TRef<CompoundCollision> compoundCollision;
    };

    using ShapeDesc = std::variant<BoxShapeDesc, SphereShapeDesc, CapsuleShapeDesc, ConvexHullShapeDesc, CylinderShapeDesc, PlaneShapeDesc, MeshShapeDesc, CompoundShapeDesc>;

    // SwingTwist/SixDOF 还没展开字段, 等对应的 Joint 组件要实现时再加进这个 variant。
    using ConstraintDesc = std::variant<HingeConstraintDesc, FixedConstraintDesc, SpringConstraintDesc>;

    using PhysicsBodyHandle = TPhysicsHandle<PhysicsResourceType::Body>;
    using PhysicsConstraintHandle = TPhysicsHandle<PhysicsResourceType::Constraint>;

    struct PhysicsOverlapEvent
    {
        Guid trigger;
        Guid other;
        bool begin = true;
    };

    enum class PhysicsRadialFalloff : uint8_t
    {
        Constant,
        Linear,
    };

    struct PhysicsQueryParams
    {
        uint32_t layerMask = ~0u;
        bool includeTriggers = false;
        std::vector<PhysicsBodyHandle> ignoredBodies;
    };

    struct PhysicsRaycastHit
    {
        Guid owner;
        double3 position = double3(0.0);
        float3 normal = float3(0.0f);
        float distance = 0.0f;
    };
}
