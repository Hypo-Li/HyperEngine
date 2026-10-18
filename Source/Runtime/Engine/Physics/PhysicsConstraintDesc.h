#pragma once
#include <Core/Math/Math.h>
#include <Core/Math/Vector.h>

namespace hyp
{
    // anchor 相对 bodyA 局部空间, connectedAnchor 相对 bodyB 局部空间(bodyB 是世界时就是
    // 世界空间坐标)。axis 是同一个方向在两个 body 各自局部空间下的表示——两个 body 创建时的
    // 相对姿态如果就是希望的静止姿态, 这样写完全够用, 不需要为两个 body 分别存一份 axis。
    struct HingeConstraintDesc
    {
        float3 anchor = float3(0.0f);
        float3 connectedAnchor = float3(0.0f);
        float3 axis = float3(0.0f, 0.0f, 1.0f);
        float minAngle = -Math::PI;
        float maxAngle = Math::PI;
    };

    // 不需要参数, Jolt 用两个 body 创建约束时刻的相对位姿锁死。
    struct FixedConstraintDesc
    {
    };

    // point1 相对 bodyA 局部空间, point2 相对 bodyB 局部空间(bodyB 是世界时就是世界空间坐标),
    // 跟 HingeConstraintDesc 的 anchor/connectedAnchor 同一个约定。
    struct SpringConstraintDesc
    {
        float3 point1 = float3(0.0f);
        float3 point2 = float3(0.0f);
        float minDistance = -1.0f; // -1 = 用两点创建时的实际距离
        float maxDistance = -1.0f;
        float springFrequency = 0.0f; // <= 0 = 不当弹簧用, 距离限制是硬限位
        float springDamping = 0.0f;
    };
}
