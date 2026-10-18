#include "PhysicsScene.h"
#include "PhysicsSystem.h"

#include <Jolt/Math/Real.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <span>
#include <utility>
#include <vector>

namespace hyp
{
    namespace
    {
        constexpr float kUnitScaleEpsilon = 1e-4f;

        bool isUnitScale(float3 scale)
        {
            return std::abs(scale.x - 1.0f) < kUnitScaleEpsilon &&
                std::abs(scale.y - 1.0f) < kUnitScaleEpsilon &&
                std::abs(scale.z - 1.0f) < kUnitScaleEpsilon;
        }

        // ObjectLayer 的低位表示是否会动, 高位是用户层槽位。
        constexpr JPH::ObjectLayer makeObjectLayer(uint32_t userLayer, bool moving)
        {
            return static_cast<JPH::ObjectLayer>((userLayer << 1) | (moving ? 1u : 0u));
        }

        constexpr uint32_t getUserLayer(JPH::ObjectLayer objectLayer)
        {
            return objectLayer >> 1;
        }

        constexpr bool isMovingLayer(JPH::ObjectLayer objectLayer)
        {
            return (objectLayer & 1u) != 0;
        }

        JPH::Vec3 toJolt(float3 value)
        {
            return JPH::Vec3(value.x, value.y, value.z);
        }

        JPH::RVec3 toJolt(double3 value)
        {
            return JPH::RVec3(value.x, value.y, value.z);
        }

        float3 fromJolt(JPH::Vec3 value)
        {
            return float3(value.GetX(), value.GetY(), value.GetZ());
        }

        double3 fromJolt(JPH::RVec3 value)
        {
            return double3(value.GetX(), value.GetY(), value.GetZ());
        }

        constexpr JPH::BroadPhaseLayer kNonMovingBroadPhaseLayer(0);
        constexpr JPH::BroadPhaseLayer kMovingBroadPhaseLayer(1);

        class LayerBroadPhaseLayerInterface : public JPH::BroadPhaseLayerInterface
        {
        public:
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
            {
                return isMovingLayer(inLayer) ? kMovingBroadPhaseLayer : kNonMovingBroadPhaseLayer;
            }

            JPH::uint GetNumBroadPhaseLayers() const override
            {
                return 2;
            }
        };

        class LayerObjectVsBroadPhaseLayerFilter : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
            {
                return inLayer2 == kMovingBroadPhaseLayer || isMovingLayer(inLayer1);
            }
        };

        class LayerObjectLayerPairFilter : public JPH::ObjectLayerPairFilter
        {
        public:
            explicit LayerObjectLayerPairFilter(const PhysicsLayerRelationTable& relations) :
                mRelations(relations)
            {
            }

            bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override
            {
                if (!isMovingLayer(inLayer1) && !isMovingLayer(inLayer2))
                    return false;

                return mRelations[getUserLayer(inLayer1)][getUserLayer(inLayer2)] != PhysicsLayerRelation::Ignore;
            }

        private:
            const PhysicsLayerRelationTable& mRelations;
        };

        class QueryObjectLayerFilter : public JPH::ObjectLayerFilter
        {
        public:
            explicit QueryObjectLayerFilter(uint32_t layerMask) :
                mLayerMask(layerMask)
            {
            }

            bool ShouldCollide(JPH::ObjectLayer inLayer) const override
            {
                return (mLayerMask & (1u << getUserLayer(inLayer))) != 0;
            }

        private:
            uint32_t mLayerMask;
        };

        class QueryBodyFilter : public JPH::BodyFilter
        {
        public:
            // sensorFlags 非空时跳过 sensor。
            QueryBodyFilter(const std::vector<JPH::BodyID>& ignoredBodies, const std::vector<uint8_t>* sensorFlags) :
                mIgnoredBodies(ignoredBodies),
                mSensorFlags(sensorFlags)
            {
            }

            bool ShouldCollide(const JPH::BodyID& inBodyID) const override
            {
                if (std::find(mIgnoredBodies.begin(), mIgnoredBodies.end(), inBodyID) != mIgnoredBodies.end())
                    return false;

                if (mSensorFlags)
                {
                    uint32_t index = inBodyID.GetIndex();
                    if (index < mSensorFlags->size() && (*mSensorFlags)[index] != 0)
                        return false;
                }
                return true;
            }

        private:
            const std::vector<JPH::BodyID>& mIgnoredBodies;
            const std::vector<uint8_t>* mSensorFlags;
        };

        JPH::EMotionType translateMotionType(MotionType motionType)
        {
            switch (motionType)
            {
            case MotionType::Static:
                return JPH::EMotionType::Static;
            case MotionType::Kinematic:
                return JPH::EMotionType::Kinematic;
            case MotionType::Dynamic:
                return JPH::EMotionType::Dynamic;
            }
            return JPH::EMotionType::Static;
        }

        // material 为空(Collider 没指定 PhysicsMaterial)时的 fallback, 数值跟 PhysicsMaterial
        // 的字段默认值保持一致。
        constexpr float kDefaultFriction = 0.6f;
        constexpr float kDefaultRestitution = 0.0f;
        constexpr float kDefaultDensity = 1000.0f;

        struct ResolvedMaterial
        {
            float friction = kDefaultFriction;
            float restitution = kDefaultRestitution;
            float density = kDefaultDensity;
            PhysicsMaterial::CombineMode frictionCombine = PhysicsMaterial::CombineMode::Average;
            PhysicsMaterial::CombineMode restitutionCombine = PhysicsMaterial::CombineMode::Average;
        };

        ResolvedMaterial resolveMaterial(const TRef<PhysicsMaterial>& material)
        {
            if (!material)
                return ResolvedMaterial();

            ResolvedMaterial resolved;
            resolved.friction = material->getFriction();
            resolved.restitution = material->getRestitution();
            resolved.density = material->getDensity();
            resolved.frictionCombine = material->getFrictionCombine();
            resolved.restitutionCombine = material->getRestitutionCombine();
            return resolved;
        }

        // 打包进 Body::mUserData 的低 4 位。
        uint64_t packCombineModes(PhysicsMaterial::CombineMode frictionCombine, PhysicsMaterial::CombineMode restitutionCombine)
        {
            return (static_cast<uint64_t>(frictionCombine) & 0x3u) | ((static_cast<uint64_t>(restitutionCombine) & 0x3u) << 2);
        }

        PhysicsMaterial::CombineMode unpackFrictionCombine(const JPH::Body& body)
        {
            return static_cast<PhysicsMaterial::CombineMode>(body.GetUserData() & 0x3u);
        }

        PhysicsMaterial::CombineMode unpackRestitutionCombine(const JPH::Body& body)
        {
            return static_cast<PhysicsMaterial::CombineMode>((body.GetUserData() >> 2) & 0x3u);
        }

        float combineByMode(PhysicsMaterial::CombineMode mode, float a, float b)
        {
            switch (mode)
            {
            case PhysicsMaterial::CombineMode::Average:
                return (a + b) * 0.5f;
            case PhysicsMaterial::CombineMode::Minimum:
                return std::min(a, b);
            case PhysicsMaterial::CombineMode::Multiply:
                return a * b;
            case PhysicsMaterial::CombineMode::Maximum:
                return std::max(a, b);
            }
            return (a + b) * 0.5f;
        }

        // JPH::ContactConstraintManager::CombineFunction 是裸函数指针, 不能捕获状态。
        float combineFriction(const JPH::Body& body1, const JPH::SubShapeID&, const JPH::Body& body2, const JPH::SubShapeID&)
        {
            PhysicsMaterial::CombineMode mode = std::max(unpackFrictionCombine(body1), unpackFrictionCombine(body2));
            return combineByMode(mode, body1.GetFriction(), body2.GetFriction());
        }

        float combineRestitution(const JPH::Body& body1, const JPH::SubShapeID&, const JPH::Body& body2, const JPH::SubShapeID&)
        {
            PhysicsMaterial::CombineMode mode = std::max(unpackRestitutionCombine(body1), unpackRestitutionCombine(body2));
            return combineByMode(mode, body1.GetRestitution(), body2.GetRestitution());
        }

        bool requiresStaticMotion(const ShapeDesc& shape)
        {
            if (std::holds_alternative<PlaneShapeDesc>(shape) || std::holds_alternative<MeshShapeDesc>(shape))
                return true;

            if (auto* compound = std::get_if<CompoundShapeDesc>(&shape))
                return compound->compoundCollision && compound->compoundCollision->requiresStatic();

            return false;
        }

        uint64_t makeOverlapKey(JPH::BodyID a, JPH::BodyID b)
        {
            uint32_t low = a.GetIndexAndSequenceNumber();
            uint32_t high = b.GetIndexAndSequenceNumber();
            if (low > high)
                std::swap(low, high);
            return (static_cast<uint64_t>(low) << 32) | high;
        }
    }

    class PhysicsScene::OverlapContactListener final : public JPH::ContactListener
    {
    public:
        explicit OverlapContactListener(OverlapContactBuffer& buffer) :
            mBuffer(buffer)
        {
        }

        void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold&, JPH::ContactSettings&) override
        {
            bool body1IsSensor = mBuffer.isSensor(inBody1.GetID());
            if (body1IsSensor == mBuffer.isSensor(inBody2.GetID()))
                return;

            push({ inBody1.GetID(), inBody2.GetID(), true, body1IsSensor });
        }

        void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
        {
            bool body1IsSensor = mBuffer.isSensor(inSubShapePair.GetBody1ID());
            if (body1IsSensor == mBuffer.isSensor(inSubShapePair.GetBody2ID()))
                return;

            push({ inSubShapePair.GetBody1ID(), inSubShapePair.GetBody2ID(), false, body1IsSensor });
        }

    private:
        void push(const RawContact& contact)
        {
            std::lock_guard lock(mBuffer.mutex);
            mBuffer.contacts.push_back(contact);
        }

        OverlapContactBuffer& mBuffer;
    };

    PhysicsScene::PhysicsScene()
    {
    }

    PhysicsScene::~PhysicsScene()
    {
    }

    void PhysicsScene::initialize(JPH::TempAllocator& tempAllocator, JPH::JobSystem& jobSystem, PhysicsSystem& owner)
    {
        mTempAllocator = &tempAllocator;
        mJobSystem = &jobSystem;
        mPhysicsSystem = &owner;

        mJoltSystem = std::make_unique<JPH::PhysicsSystem>();
        mBroadPhaseLayerInterface = std::make_unique<LayerBroadPhaseLayerInterface>();
        mObjectVsBroadPhaseLayerFilter = std::make_unique<LayerObjectVsBroadPhaseLayerFilter>();
        mObjectLayerPairFilter = std::make_unique<LayerObjectLayerPairFilter>(owner.getLayerRelations());

        const uint32_t maxBodies = 10000;
        const uint32_t numBodyMutexes = 0; // auto
        const uint32_t maxBodyPairs = 10000;
        const uint32_t maxContactConstraints = 10000;

        mJoltSystem->Init(maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints, *mBroadPhaseLayerInterface, *mObjectVsBroadPhaseLayerFilter, *mObjectLayerPairFilter);

        mJoltSystem->SetCombineFriction(combineFriction);
        mJoltSystem->SetCombineRestitution(combineRestitution);

        mContactListener = std::make_unique<OverlapContactListener>(mOverlapBuffer);
        mJoltSystem->SetContactListener(mContactListener.get());

        // Jolt 默认重力是 (0,-9.81,0), 按 Y-up 假设写的——这个引擎是 Z-up(见
        // Transform::UpVector), 不显式设置的话新 body 会顺着 Y 轴(前后方向)漂, 不会真的往下掉。
        setGravity(float3(0.0f, 0.0f, -9.81f));
    }

    void PhysicsScene::setGravity(float3 gravity)
    {
        mJoltSystem->SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }

    JPH::RefConst<JPH::Shape> PhysicsScene::buildJoltShape(const ShapeDesc& desc, float3 scale, float density) const
    {
        bool unitScale = isUnitScale(scale);

        // Box 半extent直接乘上 scale 就是缩放后的形状, 不需要额外包一层——Sphere 这类形状的
        // 参数(半径)表达不了非均匀缩放(会变成椭球), 只能在生成基础形状之后用 ScaledShape 包
        // 一层。哪种形状用哪条路径由这里的 if constexpr 决定, buildJoltShape() 调用方不需要
        // 关心。
        JPH::RefConst<JPH::Shape> shape = std::visit(
            [this, scale, unitScale, density](auto&& shapeDesc) -> JPH::RefConst<JPH::Shape>
            {
                using T = std::decay_t<decltype(shapeDesc)>;
                if constexpr (std::is_same_v<T, BoxShapeDesc>)
                {
                    float3 halfExtent = unitScale ? shapeDesc.halfExtent : shapeDesc.halfExtent * scale;
                    JPH::BoxShapeSettings settings(JPH::Vec3(halfExtent.x, halfExtent.y, halfExtent.z));
                    settings.SetDensity(density);
                    return settings.Create().Get();
                }
                else if constexpr (std::is_same_v<T, SphereShapeDesc>)
                {
                    JPH::SphereShapeSettings settings(shapeDesc.radius);
                    settings.SetDensity(density);
                    return settings.Create().Get();
                }
                else if constexpr (std::is_same_v<T, CapsuleShapeDesc>)
                {
                    JPH::CapsuleShapeSettings settings(shapeDesc.halfHeight, shapeDesc.radius);
                    settings.SetDensity(density);
                    return settings.Create().Get();
                }
                else if constexpr (std::is_same_v<T, ConvexHullShapeDesc>)
                {
                    // hull 判空已经在 ConvexHullCollider::onRegister() 里做过。
                    JPH::RefConst<JPH::Shape> hullShape;
                    shapeDesc.hull->accessPoints(
                        [&hullShape, density, convexRadius = shapeDesc.hull->getConvexRadius()](std::span<const float3> points)
                        {
                            // JPH::Vec3 是 16 字节 SIMD 对齐的, 跟紧凑排列的 float3 不能 reinterpret_cast。
                            std::vector<JPH::Vec3> joltPoints;
                            joltPoints.reserve(points.size());
                            for (float3 p : points)
                                joltPoints.emplace_back(p.x, p.y, p.z);

                            JPH::ConvexHullShapeSettings settings(joltPoints.data(), static_cast<int>(joltPoints.size()), convexRadius);
                            settings.SetDensity(density);
                            hullShape = settings.Create().Get();
                        }
                    );
                    return hullShape;
                }
                else if constexpr (std::is_same_v<T, CylinderShapeDesc>)
                {
                    JPH::CylinderShapeSettings settings(shapeDesc.halfHeight, shapeDesc.radius);
                    settings.SetDensity(density);
                    return settings.Create().Get();
                }
                else if constexpr (std::is_same_v<T, PlaneShapeDesc>)
                {
                    // PlaneShapeSettings 不是 ConvexShapeSettings 派生, 没有 SetDensity()。
                    JPH::Plane plane(JPH::Vec3(shapeDesc.normal.x, shapeDesc.normal.y, shapeDesc.normal.z), shapeDesc.constant);
                    JPH::PlaneShapeSettings settings(plane, nullptr, shapeDesc.halfExtent);
                    return settings.Create().Get();
                }
                else if constexpr (std::is_same_v<T, MeshShapeDesc>)
                {
                    // buildFn 只在 getOrBuildMeshShape() 未命中缓存时才会执行。
                    return mPhysicsSystem->getOrBuildMeshShape(shapeDesc.mesh->getGuid(), shapeDesc.lodLevel,
                        [mesh = shapeDesc.mesh, lodLevel = shapeDesc.lodLevel]() mutable -> JPH::RefConst<JPH::Shape>
                        {
                            JPH::RefConst<JPH::Shape> meshShape;
                            mesh->accessLODData(lodLevel,
                                [&meshShape, &mesh, lodLevel](Mesh::LODDataView view)
                                {
                                    std::span<const float3> positions = view.getPositions();
                                    JPH::VertexList vertices;
                                    vertices.reserve(positions.size());
                                    for (float3 p : positions)
                                        vertices.emplace_back(p.x, p.y, p.z);

                                    // Submesh 的索引是各自局部的, 拼全局三角形要加上 vertexOffset。
                                    std::span<const uint32_t> indices = view.getIndices();
                                    JPH::IndexedTriangleList triangles;
                                    uint32_t submeshCount = mesh->getSubmeshCount(lodLevel);
                                    for (uint32_t s = 0; s < submeshCount; ++s)
                                    {
                                        int32_t vertexOffset = mesh->getSubmeshVertexOffset(lodLevel, s);
                                        uint32_t indexOffset = mesh->getSubmeshIndexOffset(lodLevel, s);
                                        uint32_t indexCount = mesh->getSubmeshIndexCount(lodLevel, s);
                                        for (uint32_t i = 0; i + 2 < indexCount; i += 3)
                                        {
                                            uint32_t i0 = indices[indexOffset + i] + vertexOffset;
                                            uint32_t i1 = indices[indexOffset + i + 1] + vertexOffset;
                                            uint32_t i2 = indices[indexOffset + i + 2] + vertexOffset;
                                            triangles.emplace_back(i0, i1, i2);
                                        }
                                    }

                                    // MeshShapeSettings 不是 ConvexShapeSettings 派生, 没有 SetDensity()。
                                    JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
                                    meshShape = settings.Create().Get();
                                }
                            );
                            return meshShape;
                        }
                    );
                }
                else if constexpr (std::is_same_v<T, CompoundShapeDesc>)
                {
                    // JPH::CompoundShapeSettings::AddShape() 不接收 scale 参数, 子形状的
                    // localScale 要在传给它之前就烘进各自的 Shape 里。
                    JPH::StaticCompoundShapeSettings compoundSettings;

                    auto addSubShape = [this, &compoundSettings, density](const ShapeDesc& subShapeDesc, const CompoundCollision::SubShapeTransform& transform)
                    {
                        JPH::RefConst<JPH::Shape> subJoltShape = buildJoltShape(subShapeDesc, transform.localScale, density);
                        compoundSettings.AddShape(
                            JPH::Vec3(transform.localPosition.x, transform.localPosition.y, transform.localPosition.z),
                            JPH::Quat(transform.localRotation.x, transform.localRotation.y, transform.localRotation.z, transform.localRotation.w),
                            subJoltShape.GetPtr()
                        );
                    };

                    const CompoundCollision& compoundCollision = *shapeDesc.compoundCollision;
                    for (const auto& entry : compoundCollision.getBoxes())
                        addSubShape(entry.shape, entry.transform);
                    for (const auto& entry : compoundCollision.getSpheres())
                        addSubShape(entry.shape, entry.transform);
                    for (const auto& entry : compoundCollision.getCapsules())
                        addSubShape(entry.shape, entry.transform);
                    for (const auto& entry : compoundCollision.getCylinders())
                        addSubShape(entry.shape, entry.transform);
                    for (const auto& entry : compoundCollision.getConvexHulls())
                        addSubShape(entry.shape, entry.transform);
                    for (const auto& entry : compoundCollision.getMeshes())
                        addSubShape(entry.shape, entry.transform);

                    return compoundSettings.Create().Get();
                }
            },
            desc
        );

        if (!unitScale && !std::holds_alternative<BoxShapeDesc>(desc))
        {
            JPH::ScaledShapeSettings scaledSettings(shape, JPH::Vec3(scale.x, scale.y, scale.z));
            shape = scaledSettings.Create().Get();
        }

        return shape;
    }

    JPH::Ref<JPH::Constraint> PhysicsScene::buildJoltConstraint(const ConstraintDesc& desc, JPH::Body& bodyA, JPH::Body& bodyB) const
    {
        // LocalToBodyCOM 让 anchor/point 字段就是"相对各自 body 局部空间"——sFixedToWorld 的
        // 变换是恒等的, 所以 bodyB 是它时"相对 bodyB 局部空间"自动等价世界空间, 不需要分支处理。
        return std::visit(
            [&bodyA, &bodyB](auto&& constraintDesc) -> JPH::Ref<JPH::Constraint>
            {
                using T = std::decay_t<decltype(constraintDesc)>;
                if constexpr (std::is_same_v<T, HingeConstraintDesc>)
                {
                    JPH::HingeConstraintSettings settings;
                    settings.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
                    settings.mPoint1 = JPH::RVec3(constraintDesc.anchor.x, constraintDesc.anchor.y, constraintDesc.anchor.z);
                    settings.mPoint2 = JPH::RVec3(constraintDesc.connectedAnchor.x, constraintDesc.connectedAnchor.y, constraintDesc.connectedAnchor.z);

                    // axis 是同一个方向在两个 body 各自局部空间下的表示(HingeConstraintDesc
                    // 只存一份), mNormalAxis 只影响"转角从哪个姿态算作 0"这个参考系, 不影响
                    // 实际允许的自由度, 两个 body 用同一个法线可以保证读数一致。
                    JPH::Vec3 hingeAxis(constraintDesc.axis.x, constraintDesc.axis.y, constraintDesc.axis.z);
                    JPH::Vec3 normalAxis = hingeAxis.GetNormalizedPerpendicular();
                    settings.mHingeAxis1 = hingeAxis;
                    settings.mNormalAxis1 = normalAxis;
                    settings.mHingeAxis2 = hingeAxis;
                    settings.mNormalAxis2 = normalAxis;

                    settings.mLimitsMin = constraintDesc.minAngle;
                    settings.mLimitsMax = constraintDesc.maxAngle;

                    return settings.Create(bodyA, bodyB);
                }
                else if constexpr (std::is_same_v<T, FixedConstraintDesc>)
                {
                    // mAutoDetectPoint 用两个 body 创建约束时刻的相对位姿算锁定姿态——只在
                    // WorldSpace 下生效, 这是 Jolt 自己的要求。
                    JPH::FixedConstraintSettings settings;
                    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
                    settings.mAutoDetectPoint = true;
                    return settings.Create(bodyA, bodyB);
                }
                else if constexpr (std::is_same_v<T, SpringConstraintDesc>)
                {
                    JPH::DistanceConstraintSettings settings;
                    settings.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
                    JPH::Vec3 localPoint1(constraintDesc.point1.x, constraintDesc.point1.y, constraintDesc.point1.z);
                    JPH::Vec3 localPoint2(constraintDesc.point2.x, constraintDesc.point2.y, constraintDesc.point2.z);
                    settings.mPoint1 = JPH::RVec3(localPoint1);
                    settings.mPoint2 = JPH::RVec3(localPoint2);

                    // Jolt 的 minDistance/maxDistance < 0 自动取当前距离只在 WorldSpace 下生效
                    // (Distance 字段本身的说明), LocalToBodyCOM 下要自己算世界距离补上。
                    JPH::RVec3 worldPoint1 = bodyA.GetCenterOfMassTransform() * localPoint1;
                    JPH::RVec3 worldPoint2 = bodyB.GetCenterOfMassTransform() * localPoint2;
                    float currentDistance = static_cast<float>((worldPoint2 - worldPoint1).Length());

                    settings.mMinDistance = constraintDesc.minDistance < 0.0f ? currentDistance : constraintDesc.minDistance;
                    settings.mMaxDistance = constraintDesc.maxDistance < 0.0f ? currentDistance : constraintDesc.maxDistance;
                    settings.mLimitsSpringSettings.mFrequency = constraintDesc.springFrequency;
                    settings.mLimitsSpringSettings.mDamping = constraintDesc.springDamping;

                    return settings.Create(bodyA, bodyB);
                }
            },
            desc
        );
    }

    PhysicsBodyHandle PhysicsScene::createBody(const BodySettings& bodySettings)
    {
        assert(!(requiresStaticMotion(bodySettings.shape) && bodySettings.motionType == MotionType::Dynamic));
        assert(bodySettings.layer < kMaxPhysicsLayers);

        JPH::BodyInterface& bodyInterface = mJoltSystem->GetBodyInterface();

        JPH::RVec3 position(bodySettings.position.x, bodySettings.position.y, bodySettings.position.z);
        JPH::Quat rotation(bodySettings.rotation.x, bodySettings.rotation.y, bodySettings.rotation.z, bodySettings.rotation.w);

        ResolvedMaterial material = resolveMaterial(bodySettings.material);

        JPH::RefConst<JPH::Shape> shape = buildJoltShape(bodySettings.shape, bodySettings.scale, material.density);
        if (bodySettings.overrideCenterOfMass)
        {
            // mOffset 是相对被包裹 Shape 自身局部原点的偏移, 跟 Collider::mCenterOfMassOffset
            // "相对 mCenter"的约定一致——mCenter 已经在 buildJoltShape() 之前被折算成了 Shape
            // 的摆放位置, Shape 自己的局部原点就是 mCenter 那个点。
            JPH::OffsetCenterOfMassShapeSettings comSettings(
                JPH::Vec3(bodySettings.centerOfMassOffset.x, bodySettings.centerOfMassOffset.y, bodySettings.centerOfMassOffset.z),
                shape.GetPtr()
            );
            shape = comSettings.Create().Get();
        }

        JPH::BodyCreationSettings creationSettings(
            shape,
            position,
            rotation,
            translateMotionType(bodySettings.motionType),
            makeObjectLayer(bodySettings.layer, bodySettings.motionType != MotionType::Static)
        );

        creationSettings.mFriction = material.friction;
        creationSettings.mRestitution = material.restitution;
        creationSettings.mUserData = packCombineModes(material.frictionCombine, material.restitutionCombine);

        if (bodySettings.motionType != MotionType::Static)
        {
            if (bodySettings.overrideMass)
            {
                // 质量用给定值, 但惯性张量仍按 shape 形状算(不是给个跟形状无关的默认惯性)。
                creationSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                creationSettings.mMassPropertiesOverride.mMass = bodySettings.mass;
            }
            else
            {
                // 质量和惯性都按 shape 体积 x density(已经烘进上面 buildJoltShape 生成的
                // Shape 里)自动算。
                creationSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateMassAndInertia;
            }
        }

        JPH::Body* body = bodyInterface.CreateBody(creationSettings);
        if (!body)
            return PhysicsBodyHandle();

        if (bodySettings.isTrigger)
        {
            body->SetIsSensor(true);
        }

        JPH::BodyID bodyID = body->GetID();

        uint32_t bodyIndex = bodyID.GetIndex();
        if (mOwnerByBodyIndex.size() <= bodyIndex)
        {
            mOwnerByBodyIndex.resize(bodyIndex + 1);
            mOverlapBuffer.sensorFlags.resize(bodyIndex + 1, 0);
        }
        mOwnerByBodyIndex[bodyIndex] = bodySettings.ownerId;
        mOverlapBuffer.sensorFlags[bodyIndex] = bodySettings.isTrigger ? 1 : 0;

        bodyInterface.AddBody(bodyID, bodySettings.motionType == MotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);

        return mBodyPool.insert(bodyID);
    }

    void PhysicsScene::destroyBody(PhysicsBodyHandle handle)
    {
        if (!mBodyPool.isValid(handle))
            return;

        // 先把挂在这个 Body 上的 Constraint 全部销毁掉, 避免 Jolt 里出现 Constraint 指向
        // 已销毁 Body 的状态——destroyConstraint() 内部会顺带清理另一侧 body 的反向列表。
        auto constraintsIt = mConstraintsByBody.find(handle.getIndex());
        if (constraintsIt != mConstraintsByBody.end())
        {
            // destroyConstraint() 会改这个 vector, 拷贝一份出来遍历。
            std::vector<PhysicsConstraintHandle> constraints = constraintsIt->second;
            for (PhysicsConstraintHandle constraintHandle : constraints)
                destroyConstraint(constraintHandle);
        }

        releaseOverlaps(handle);

        JPH::BodyInterface& bodyInterface = mJoltSystem->GetBodyInterface();
        JPH::BodyID bodyID = mBodyPool.get(handle);
        mOverlapBuffer.sensorFlags[bodyID.GetIndex()] = 0;
        mOwnerByBodyIndex[bodyID.GetIndex()] = Guid();
        mPendingLoads.erase(bodyID.GetIndexAndSequenceNumber());
        bodyInterface.RemoveBody(bodyID);
        bodyInterface.DestroyBody(bodyID);

        mBodyPool.remove(handle);
    }

    void PhysicsScene::setBodyMotionType(PhysicsBodyHandle handle, MotionType motionType)
    {
        JPH::BodyInterface& bodyInterface = mJoltSystem->GetBodyInterface();
        JPH::BodyID bodyID = mBodyPool.get(handle);

        bodyInterface.SetMotionType(bodyID, translateMotionType(motionType), JPH::EActivation::Activate);
        bodyInterface.SetObjectLayer(bodyID, makeObjectLayer(getUserLayer(bodyInterface.GetObjectLayer(bodyID)), motionType != MotionType::Static));
    }

    PhysicsConstraintHandle PhysicsScene::createConstraint(const ConstraintSettings& settings)
    {
        assert(mBodyPool.isValid(settings.bodyA));

        JPH::BodyID bodyIDA = mBodyPool.get(settings.bodyA);
        const JPH::BodyLockInterface& lockInterface = mJoltSystem->GetBodyLockInterface();

        JPH::Ref<JPH::Constraint> constraint;
        if (mBodyPool.isValid(settings.bodyB))
        {
            // 两个 Body 各自的锁在 Jolt 的死锁检测里算同一个 PerBody 类别, 同一线程不能嵌套
            // 拿两次(会撞 PhysicsLock::sCheckLock 的断言)——必须用 BodyLockMultiWrite 一次
            // 性把两个都锁上, 不能各开一个 BodyLockWrite。
            JPH::BodyID bodyIDs[2] = { bodyIDA, mBodyPool.get(settings.bodyB) };
            JPH::BodyLockMultiWrite lock(lockInterface, bodyIDs, 2);
            JPH::Body* jphBodyA = lock.GetBody(0);
            JPH::Body* jphBodyB = lock.GetBody(1);
            assert(jphBodyA && jphBodyB);
            constraint = buildJoltConstraint(settings.desc, *jphBodyA, *jphBodyB);
        }
        else
        {
            JPH::BodyLockWrite lockA(lockInterface, bodyIDA);
            assert(lockA.Succeeded());
            constraint = buildJoltConstraint(settings.desc, lockA.GetBody(), JPH::Body::sFixedToWorld);
        }

        mJoltSystem->AddConstraint(constraint);

        ConstraintEntry entry{ constraint, settings.bodyA, settings.bodyB };
        PhysicsConstraintHandle handle = mConstraintPool.insert(std::move(entry));

        mConstraintsByBody[settings.bodyA.getIndex()].push_back(handle);
        if (mBodyPool.isValid(settings.bodyB))
            mConstraintsByBody[settings.bodyB.getIndex()].push_back(handle);

        return handle;
    }

    void PhysicsScene::destroyConstraint(PhysicsConstraintHandle handle)
    {
        if (!mConstraintPool.isValid(handle))
            return;

        const ConstraintEntry& entry = mConstraintPool.get(handle);
        mJoltSystem->RemoveConstraint(entry.constraint);

        auto eraseFromBucket = [this, handle](PhysicsBodyHandle bodyHandle)
        {
            auto it = mConstraintsByBody.find(bodyHandle.getIndex());
            if (it == mConstraintsByBody.end())
                return;
            std::erase(it->second, handle);
            if (it->second.empty())
                mConstraintsByBody.erase(it);
        };
        eraseFromBucket(entry.bodyA);
        if (mBodyPool.isValid(entry.bodyB))
            eraseFromBucket(entry.bodyB);

        mConstraintPool.remove(handle);
    }

    double3 PhysicsScene::getBodyPosition(PhysicsBodyHandle handle) const
    {
        JPH::RVec3 p = mJoltSystem->GetBodyInterface().GetPosition(mBodyPool.get(handle));
        return double3(p.GetX(), p.GetY(), p.GetZ());
    }

    Quaternion PhysicsScene::getBodyRotation(PhysicsBodyHandle handle) const
    {
        JPH::Quat q = mJoltSystem->GetBodyInterface().GetRotation(mBodyPool.get(handle));
        return Quaternion(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }

    void PhysicsScene::setBodyTransform(PhysicsBodyHandle handle, double3 position, Quaternion rotation)
    {
        JPH::RVec3 p(position.x, position.y, position.z);
        JPH::Quat q(rotation.x, rotation.y, rotation.z, rotation.w);
        mJoltSystem->GetBodyInterface().SetPositionAndRotation(mBodyPool.get(handle), p, q, JPH::EActivation::Activate);
    }

    bool PhysicsScene::tryGetDynamicBody(PhysicsBodyHandle handle, JPH::BodyID& outBodyID) const
    {
        if (!mBodyPool.isValid(handle))
            return false;

        outBodyID = mBodyPool.get(handle);
        return mJoltSystem->GetBodyInterface().GetMotionType(outBodyID) == JPH::EMotionType::Dynamic;
    }

    PhysicsScene::PendingLoad* PhysicsScene::acquireDynamicLoad(PhysicsBodyHandle handle)
    {
        JPH::BodyID bodyID;
        if (!tryGetDynamicBody(handle, bodyID))
            return nullptr;

        mJoltSystem->GetBodyInterface().ActivateBody(bodyID);

        PendingLoad& load = mPendingLoads[bodyID.GetIndexAndSequenceNumber()];
        load.id = bodyID;
        return &load;
    }

    void PhysicsScene::applyPendingLoads()
    {
        JPH::BodyInterface& bodyInterface = mJoltSystem->GetBodyInterface();
        for (auto& [key, load] : mPendingLoads)
        {
            if (!bodyInterface.IsAdded(load.id) || bodyInterface.GetMotionType(load.id) != JPH::EMotionType::Dynamic)
                continue;

            bodyInterface.AddImpulse(load.id, toJolt(load.linearImpulse));
            bodyInterface.AddAngularImpulse(load.id, toJolt(load.angularImpulse));
        }
        mPendingLoads.clear();
    }

    void PhysicsScene::addForce(PhysicsBodyHandle handle, float3 force)
    {
        if (force == float3(0.0f))
            return;

        if (PendingLoad* load = acquireDynamicLoad(handle))
            load->force += force;
    }

    void PhysicsScene::addForceAtPosition(PhysicsBodyHandle handle, float3 force, double3 worldPosition)
    {
        if (force == float3(0.0f))
            return;

        PendingLoad* load = acquireDynamicLoad(handle);
        if (!load)
            return;

        double3 centerOfMass = fromJolt(mJoltSystem->GetBodyInterface().GetCenterOfMassPosition(load->id));
        load->force += force;
        load->torque += float3(glm::cross(worldPosition - centerOfMass, double3(force)));
    }

    void PhysicsScene::addTorque(PhysicsBodyHandle handle, float3 torque)
    {
        if (torque == float3(0.0f))
            return;

        if (PendingLoad* load = acquireDynamicLoad(handle))
            load->torque += torque;
    }

    void PhysicsScene::addImpulse(PhysicsBodyHandle handle, float3 impulse, bool velocityChange)
    {
        JPH::BodyID bodyID;
        if (impulse == float3(0.0f) || !tryGetDynamicBody(handle, bodyID))
            return;

        JPH::BodyInterface& bodyInterface = mJoltSystem->GetBodyInterface();
        if (velocityChange)
        {
            bodyInterface.SetLinearVelocity(bodyID, bodyInterface.GetLinearVelocity(bodyID) + toJolt(impulse));
            bodyInterface.ActivateBody(bodyID);
        }
        else
        {
            bodyInterface.AddImpulse(bodyID, toJolt(impulse));
        }
    }

    void PhysicsScene::addImpulseAtPosition(PhysicsBodyHandle handle, float3 impulse, double3 worldPosition)
    {
        JPH::BodyID bodyID;
        if (impulse == float3(0.0f) || !tryGetDynamicBody(handle, bodyID))
            return;

        mJoltSystem->GetBodyInterface().AddImpulse(bodyID, toJolt(impulse), toJolt(worldPosition));
    }

    void PhysicsScene::addRadialImpulse(PhysicsBodyHandle handle, double3 origin, float radius, float strength, PhysicsRadialFalloff falloff, bool velocityChange)
    {
        JPH::BodyID bodyID;
        if (!tryGetDynamicBody(handle, bodyID))
            return;

        double3 offset = fromJolt(mJoltSystem->GetBodyInterface().GetCenterOfMassPosition(bodyID)) - origin;
        double distance = glm::length(offset);
        if (distance >= radius)
            return;

        float3 impulse;
        if (distance > 0.0)
        {
            float falloffScale = falloff == PhysicsRadialFalloff::Linear ? static_cast<float>((radius - distance) / radius) : 1.0f;
            impulse = float3(offset / distance) * (strength * falloffScale);
        }
        else
        {
            // 质心与爆炸点重合时方向任选。
            impulse = float3(0.0f, 0.0f, strength);
        }
        addImpulse(handle, impulse, velocityChange);
    }

    void PhysicsScene::setLinearVelocity(PhysicsBodyHandle handle, float3 velocity)
    {
        JPH::BodyID bodyID;
        if (!tryGetDynamicBody(handle, bodyID))
            return;

        mJoltSystem->GetBodyInterface().SetLinearVelocity(bodyID, toJolt(velocity));
        if (velocity != float3(0.0f))
            mJoltSystem->GetBodyInterface().ActivateBody(bodyID);
    }

    float3 PhysicsScene::getLinearVelocity(PhysicsBodyHandle handle) const
    {
        if (!mBodyPool.isValid(handle))
            return float3(0.0f);

        return fromJolt(mJoltSystem->GetBodyInterface().GetLinearVelocity(mBodyPool.get(handle)));
    }

    void PhysicsScene::setAngularVelocity(PhysicsBodyHandle handle, float3 velocity)
    {
        JPH::BodyID bodyID;
        if (!tryGetDynamicBody(handle, bodyID))
            return;

        mJoltSystem->GetBodyInterface().SetAngularVelocity(bodyID, toJolt(velocity));
        if (velocity != float3(0.0f))
            mJoltSystem->GetBodyInterface().ActivateBody(bodyID);
    }

    float3 PhysicsScene::getAngularVelocity(PhysicsBodyHandle handle) const
    {
        if (!mBodyPool.isValid(handle))
            return float3(0.0f);

        return fromJolt(mJoltSystem->GetBodyInterface().GetAngularVelocity(mBodyPool.get(handle)));
    }

    std::vector<JPH::BodyID> PhysicsScene::getIgnoredBodyIds(const PhysicsQueryParams& params) const
    {
        std::vector<JPH::BodyID> bodyIDs;
        bodyIDs.reserve(params.ignoredBodies.size());
        for (PhysicsBodyHandle handle : params.ignoredBodies)
        {
            if (mBodyPool.isValid(handle))
                bodyIDs.push_back(mBodyPool.get(handle));
        }
        return bodyIDs;
    }

    bool PhysicsScene::raycast(double3 start, double3 end, const PhysicsQueryParams& params, PhysicsRaycastHit& outHit) const
    {
        double3 delta = end - start;
        double length = glm::length(delta);
        if (length <= 0.0)
            return false;

        std::vector<JPH::BodyID> ignoredBodies = getIgnoredBodyIds(params);
        QueryObjectLayerFilter objectLayerFilter(params.layerMask);
        QueryBodyFilter bodyFilter(ignoredBodies, params.includeTriggers ? nullptr : &mOverlapBuffer.sensorFlags);

        JPH::RRayCast ray(toJolt(start), toJolt(float3(delta)));
        JPH::RayCastResult result;
        if (!mJoltSystem->GetNarrowPhaseQuery().CastRay(ray, result, JPH::BroadPhaseLayerFilter(), objectLayerFilter, bodyFilter))
            return false;

        JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
        JPH::BodyLockRead lock(mJoltSystem->GetBodyLockInterface(), result.mBodyID);
        if (!lock.Succeeded())
            return false;

        outHit.owner = getOwnerId(result.mBodyID);
        outHit.position = fromJolt(point);
        outHit.normal = fromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
        outHit.distance = static_cast<float>(result.mFraction * length);
        return true;
    }

    std::vector<Guid> PhysicsScene::overlapSphere(double3 center, float radius, const PhysicsQueryParams& params) const
    {
        std::vector<Guid> owners;
        if (radius <= 0.0f)
            return owners;

        std::vector<JPH::BodyID> ignoredBodies = getIgnoredBodyIds(params);
        QueryObjectLayerFilter objectLayerFilter(params.layerMask);
        QueryBodyFilter bodyFilter(ignoredBodies, params.includeTriggers ? nullptr : &mOverlapBuffer.sensorFlags);

        JPH::SphereShape sphere(radius);
        sphere.SetEmbedded();

        JPH::RVec3 centerPoint = toJolt(center);
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        mJoltSystem->GetNarrowPhaseQuery().CollideShape(
            &sphere, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(centerPoint), JPH::CollideShapeSettings(), centerPoint,
            collector, JPH::BroadPhaseLayerFilter(), objectLayerFilter, bodyFilter
        );

        std::vector<JPH::BodyID> bodyIDs;
        bodyIDs.reserve(collector.mHits.size());
        for (const JPH::CollideShapeResult& hit : collector.mHits)
            bodyIDs.push_back(hit.mBodyID2);

        std::sort(bodyIDs.begin(), bodyIDs.end());
        bodyIDs.erase(std::unique(bodyIDs.begin(), bodyIDs.end()), bodyIDs.end());

        for (JPH::BodyID bodyID : bodyIDs)
        {
            Guid owner = getOwnerId(bodyID);
            if (owner.isValid())
                owners.push_back(owner);
        }
        return owners;
    }

    void PhysicsScene::update(float deltaTime)
    {
        // Jolt 官方示例的标准做法: 固定步长(1/60s)推进, 用累加器吸收真实帧时间的抖动。
        static constexpr float kFixedTimestep = 1.0f / 60.0f;

        // 卡顿/断点等极端情况下真实 deltaTime 可能远超一步的量——不设上限的话下一帧要追的
        // 步数只会越滚越多, 陷入"死亡螺旋"。这里牺牲精度换稳定: 超过上限的部分直接丢弃。
        static constexpr int kMaxStepsPerFrame = 4;

        mAccumulator += deltaTime;

        for (auto& [key, load] : mPendingLoads)
        {
            load.linearImpulse += load.force * deltaTime;
            load.angularImpulse += load.torque * deltaTime;
            load.force = float3(0.0f);
            load.torque = float3(0.0f);
        }

        int steps = 0;
        while (mAccumulator >= kFixedTimestep && steps < kMaxStepsPerFrame)
        {
            if (!mBroadPhaseOptimized)
            {
                mJoltSystem->OptimizeBroadPhase();
                mBroadPhaseOptimized = true;
            }

            if (!mPendingLoads.empty())
                applyPendingLoads();

            mJoltSystem->Update(kFixedTimestep, 1, mTempAllocator, mJobSystem);
            mAccumulator -= kFixedTimestep;
            ++steps;
        }

        if (steps == kMaxStepsPerFrame)
            mAccumulator = 0.0f;

        if (steps > 0)
            collectOverlapEvents();
    }

    std::vector<PhysicsOverlapEvent> PhysicsScene::takeOverlapEvents()
    {
        return std::exchange(mPendingOverlapEvents, {});
    }

    std::vector<PhysicsOverlapEvent> PhysicsScene::releaseOverlaps(PhysicsBodyHandle handle)
    {
        std::vector<PhysicsOverlapEvent> events;
        if (!mBodyPool.isValid(handle))
            return events;

        JPH::BodyID bodyID = mBodyPool.get(handle);
        for (auto it = mActiveOverlaps.begin(); it != mActiveOverlaps.end();)
        {
            const OverlapEntry& entry = it->second;
            if (entry.trigger != bodyID && entry.other != bodyID)
            {
                ++it;
                continue;
            }

            events.push_back({ getOwnerId(entry.trigger), getOwnerId(entry.other), false });
            it = mActiveOverlaps.erase(it);
        }
        return events;
    }

    void PhysicsScene::collectOverlapEvents()
    {
        std::vector<RawContact> contacts;
        {
            std::lock_guard lock(mOverlapBuffer.mutex);
            contacts.swap(mOverlapBuffer.contacts);
        }

        // 回调顺序取决于线程调度, 按 key 稳定排序让事件顺序可复现。
        std::stable_sort(contacts.begin(), contacts.end(),
            [](const RawContact& a, const RawContact& b)
            {
                return makeOverlapKey(a.body1, a.body2) < makeOverlapKey(b.body1, b.body2);
            }
        );

        for (const RawContact& contact : contacts)
        {
            uint64_t key = makeOverlapKey(contact.body1, contact.body2);
            auto it = mActiveOverlaps.find(key);

            if (contact.added)
            {
                if (it == mActiveOverlaps.end())
                {
                    OverlapEntry entry;
                    entry.trigger = contact.body1IsSensor ? contact.body1 : contact.body2;
                    entry.other = contact.body1IsSensor ? contact.body2 : contact.body1;
                    entry.count = 1;
                    mActiveOverlaps.emplace(key, entry);
                    pushOverlapEvent(entry, true);
                }
                else if (it->second.suspended)
                {
                    it->second.suspended = false;
                    it->second.awakeGrace = false;
                    it->second.count = 1;
                }
                else
                {
                    ++it->second.count;
                }
            }
            else if (it != mActiveOverlaps.end() && !it->second.suspended)
            {
                OverlapEntry& entry = it->second;
                if (--entry.count > 0)
                    continue;

                if (shouldSuspendOverlap(entry.other))
                {
                    entry.suspended = true;
                    entry.awakeGrace = false;
                }
                else
                {
                    pushOverlapEvent(entry, false);
                    mActiveOverlaps.erase(it);
                }
            }
        }

        const JPH::BodyInterface& bodyInterface = mJoltSystem->GetBodyInterface();
        for (auto it = mActiveOverlaps.begin(); it != mActiveOverlaps.end();)
        {
            OverlapEntry& entry = it->second;
            if (!entry.suspended)
            {
                ++it;
                continue;
            }

            bool ended = false;
            if (!bodyInterface.IsAdded(entry.other) || bodyInterface.GetMotionType(entry.other) == JPH::EMotionType::Static)
            {
                ended = true;
            }
            else if (bodyInterface.IsActive(entry.other))
            {
                // 被唤醒的 body 要到下一步才会重新报 Added, 所以宽限一次折叠。
                ended = entry.awakeGrace;
                entry.awakeGrace = true;
            }
            else
            {
                entry.awakeGrace = false;
            }

            if (ended)
            {
                pushOverlapEvent(entry, false);
                it = mActiveOverlaps.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Static sensor 只检测 active 的 body, 对方入睡时 Jolt 会发 Removed, 但它仍在原地。
    bool PhysicsScene::shouldSuspendOverlap(JPH::BodyID other) const
    {
        const JPH::BodyInterface& bodyInterface = mJoltSystem->GetBodyInterface();
        return bodyInterface.IsAdded(other)
            && bodyInterface.GetMotionType(other) != JPH::EMotionType::Static
            && !bodyInterface.IsActive(other);
    }

    void PhysicsScene::pushOverlapEvent(const OverlapEntry& entry, bool begin)
    {
        mPendingOverlapEvents.push_back({ getOwnerId(entry.trigger), getOwnerId(entry.other), begin });
    }

    Guid PhysicsScene::getOwnerId(JPH::BodyID id) const noexcept
    {
        uint32_t index = id.GetIndex();
        return index < mOwnerByBodyIndex.size() ? mOwnerByBodyIndex[index] : Guid();
    }
}
