#pragma once
#include "ConvexHull.h"
#include "../Mesh/StaticMesh.h"

#include <Core/Math/Vector.h>

#include <cstdint>

namespace hyp
{
    // 各 Collider 派生类只填自己的形状参数, 不直接调用任何物理后端的 API——具体怎么变成一个
    // 可碰撞的形状对象是 PhysicsScene 内部的事。加新形状类型只需要往 ShapeDesc(PhysicsTypes.h)
    // 这个 variant 里加一个新的 XxxShapeDesc, 不需要动 Collider 基类。
    //
    // 各结构体都带 Reflection::createType<T>() 特化, 供 CompoundCollision 的子形状列表存盘。
    struct BoxShapeDesc
    {
        float3 halfExtent;
    };

    template <> Type* Reflection::createType<BoxShapeDesc>();

    struct SphereShapeDesc
    {
        float radius;
    };

    template <> Type* Reflection::createType<SphereShapeDesc>();

    struct CapsuleShapeDesc
    {
        // 胶囊体圆柱段的半高(不含两端半球), 跟 Jolt 的 CapsuleShapeSettings(halfHeight, radius)
        // 参数顺序一致。
        float halfHeight;
        float radius;
    };

    template <> Type* Reflection::createType<CapsuleShapeDesc>();

    struct CylinderShapeDesc
    {
        float halfHeight;
        float radius;
    };

    template <> Type* Reflection::createType<CylinderShapeDesc>();

    // 负半空间(x·normal + constant < 0)是实体, 跟 Jolt 的 Plane 类定义一致。
    struct PlaneShapeDesc
    {
        float3 normal = float3(0.0f, 0.0f, 1.0f);
        float constant = 0.0f;
        float halfExtent = 1000.0f;
    };

    // hull 为空(没有指定 ConvexHull 资产)是合法状态——ConvexHullCollider::onRegister() 遇到
    // 空引用会直接跳过建 Body, 不会走到这里被 PhysicsScene 消费。
    struct ConvexHullShapeDesc
    {
        TRef<ConvexHull> hull;
    };

    template <> Type* Reflection::createType<ConvexHullShapeDesc>();

    // mesh 为空是合法状态, MeshCollider::onRegister() 遇到空引用会直接跳过建 Body。
    struct MeshShapeDesc
    {
        TRef<StaticMesh> mesh;
        uint32_t lodLevel = 0;
    };

    template <> Type* Reflection::createType<MeshShapeDesc>();
}
