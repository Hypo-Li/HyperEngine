#pragma once
#include "PhysicsLayer.h"
#include "PhysicsTypes.h"
#include "PhysicsMaterial.h"

#include <Core/Math/Vector.h>
#include <Core/Math/Quaternion.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Constraints/Constraint.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace JPH
{
    class TempAllocator;
    class JobSystem;
    class Body;
}

namespace hyp
{
    // 公开接口(方法签名/参数/返回值)只暴露引擎自己的类型(PhysicsTypes.h), 不出现任何 Jolt
    // 类型——以后要换物理后端, 受影响的只有这个类和 PhysicsSystem 的实现, Collider 等调用方
    // 不需要改。private 字段允许直接用 Jolt 类型(不做 impl 类那套隔离), 这是权衡过的取舍。
    class PhysicsSystem;
    class PhysicsScene
    {
        friend class PhysicsSystem;
    public:
        PhysicsScene();

        ~PhysicsScene();

        PhysicsScene(const PhysicsScene&) = delete;
        PhysicsScene& operator=(const PhysicsScene&) = delete;
        PhysicsScene(PhysicsScene&&) = delete;
        PhysicsScene& operator=(PhysicsScene&&) = delete;

        void setGravity(float3 gravity);

        struct BodySettings
        {
            // 绝对世界坐标, 跟 Transform::getWorldPosition() 一样用 double——Jolt 编译时开了
            // JPH_DOUBLE_PRECISION(见 overlay-ports/joltphysics), body 的世界坐标内部也是
            // double(JPH::RVec3), 这里如果收 float3 会在接口这一层就把精度截断掉。
            double3 position = double3(0.0);
            Quaternion rotation = Quaternion(1, 0, 0, 0);
            float3 scale = float3(1.0f);
            ShapeDesc shape;
            // 槽位号, 0 固定是 Default。
            uint32_t layer = 0;
            MotionType motionType = MotionType::Static;
            bool isTrigger = false;

            // 重叠事件里用它标识这个 body 属于哪个 Collider。
            Guid ownerId;

            // 只在 overrideMass 为 true 时生效。
            float mass = 1.0f;
            bool overrideMass = false;

            // 相对 buildJoltShape() 产出的 Shape 自身局部原点的偏移, 只在 overrideCenterOfMass
            // 为 true 时生效。
            float3 centerOfMassOffset = float3(0.0f);
            bool overrideCenterOfMass = false;

            TRef<PhysicsMaterial> material;
        };

        PhysicsBodyHandle createBody(const BodySettings& bodySettings);

        void destroyBody(PhysicsBodyHandle handle);

        void setBodyMotionType(PhysicsBodyHandle handle, MotionType motionType);

        struct ConstraintSettings
        {
            PhysicsBodyHandle bodyA;
            PhysicsBodyHandle bodyB; // 无效 = 连接静态世界
            ConstraintDesc desc;
        };

        // bodyA 必须有效。
        PhysicsConstraintHandle createConstraint(const ConstraintSettings& settings);

        // 幂等——对已经无效的 handle(比如已经被 destroyBody() 级联销毁过)直接返回, 被
        // destroyBody() 内部级联调用和被 Joint::onUnregister() 直接调用走的是同一个函数。
        void destroyConstraint(PhysicsConstraintHandle handle);

        double3 getBodyPosition(PhysicsBodyHandle handle) const;

        Quaternion getBodyRotation(PhysicsBodyHandle handle) const;

        // 只应对 Kinematic body 调用——Dynamic body 的位姿由模拟结果决定, 外部写入会被下一步
        // 模拟覆盖掉。
        void setBodyTransform(PhysicsBodyHandle handle, double3 position, Quaternion rotation);

        // 力和力矩要每帧调用: 按这一帧的 deltaTime 折算成冲量, 在下一次推进物理步时施加, 与帧率无关。
        void addForce(PhysicsBodyHandle handle, float3 force);

        void addForceAtPosition(PhysicsBodyHandle handle, float3 force, double3 worldPosition);

        void addTorque(PhysicsBodyHandle handle, float3 torque);

        // velocityChange 为真时 impulse 按速度变化量处理, 与质量无关。
        void addImpulse(PhysicsBodyHandle handle, float3 impulse, bool velocityChange = false);

        void addImpulseAtPosition(PhysicsBodyHandle handle, float3 impulse, double3 worldPosition);

        void addRadialImpulse(PhysicsBodyHandle handle, double3 origin, float radius, float strength, PhysicsRadialFalloff falloff, bool velocityChange = false);

        void setLinearVelocity(PhysicsBodyHandle handle, float3 velocity);

        float3 getLinearVelocity(PhysicsBodyHandle handle) const;

        void setAngularVelocity(PhysicsBodyHandle handle, float3 velocity);

        float3 getAngularVelocity(PhysicsBodyHandle handle) const;

        // 线段 [start, end] 上最近的命中。
        bool raycast(double3 start, double3 end, const PhysicsQueryParams& params, PhysicsRaycastHit& outHit) const;

        // 返回与球形有重叠的 Body 的 ownerId, 按 Body 去重。
        std::vector<Guid> overlapSphere(double3 center, float radius, const PhysicsQueryParams& params) const;

        // 外部只管喂真实 deltaTime, 固定步长的 accumulator 逻辑在内部, 见 .cpp。
        void update(float deltaTime);

        std::vector<PhysicsOverlapEvent> takeOverlapEvents();

        // 之后 Jolt 补发的 Removed 会被忽略。
        std::vector<PhysicsOverlapEvent> releaseOverlaps(PhysicsBodyHandle handle);

    private:
        class OverlapContactListener;

        struct RawContact
        {
            JPH::BodyID body1;
            JPH::BodyID body2;
            bool added = false;
            bool body1IsSensor = false;
        };

        struct OverlapContactBuffer
        {
            std::mutex mutex;
            std::vector<RawContact> contacts;
            // 回调线程只读, 主线程只在 Update 之外写。
            std::vector<uint8_t> sensorFlags;

            bool isSensor(JPH::BodyID id) const noexcept
            {
                uint32_t index = id.GetIndex();
                return index < sensorFlags.size() && sensorFlags[index] != 0;
            }
        };

        struct OverlapEntry
        {
            JPH::BodyID trigger;
            JPH::BodyID other;
            uint32_t count = 0;
            bool suspended = false;
            bool awakeGrace = false;
        };

        struct PendingLoad
        {
            JPH::BodyID id;
            float3 force = float3(0.0f);
            float3 torque = float3(0.0f);
            float3 linearImpulse = float3(0.0f);
            float3 angularImpulse = float3(0.0f);
        };

        void initialize(JPH::TempAllocator& tempAllocator, JPH::JobSystem& jobSystem, PhysicsSystem& owner);

        bool tryGetDynamicBody(PhysicsBodyHandle handle, JPH::BodyID& outBodyID) const;

        // handle 无效或不是 Dynamic 返回空, 否则唤醒 Body 并返回它待施加的负载。
        PendingLoad* acquireDynamicLoad(PhysicsBodyHandle handle);

        void applyPendingLoads();

        std::vector<JPH::BodyID> getIgnoredBodyIds(const PhysicsQueryParams& params) const;

        void collectOverlapEvents();

        bool shouldSuspendOverlap(JPH::BodyID other) const;

        void pushOverlapEvent(const OverlapEntry& entry, bool begin);

        Guid getOwnerId(JPH::BodyID id) const noexcept;

        // scale 是 Collider 所在 Entity 的世界缩放。Box 这类形状把缩放直接乘进自己的参数里,
        // 不需要额外包一层; Sphere 这类没法用参数表达非均匀缩放的形状, 才用 Jolt 的
        // ScaledShape 包一层——具体哪种形状走哪条路径由 .cpp 里的实现决定, 调用方不需要关心。
        JPH::RefConst<JPH::Shape> buildJoltShape(const ShapeDesc& desc, float3 scale, float density) const;

        // inBodyA/inBodyB 必须已经是解锁访问过的 Jolt Body(sFixedToWorld 或者 BodyLockWrite
        // 拿到的引用), 这个函数本身不做锁——调用方 createConstraint() 负责。
        JPH::Ref<JPH::Constraint> buildJoltConstraint(const ConstraintDesc& desc, JPH::Body& bodyA, JPH::Body& bodyB) const;

    private:
        // 声明在 mJoltSystem 之前, 保证析构时比它活得久。
        OverlapContactBuffer mOverlapBuffer;
        std::unique_ptr<JPH::ContactListener> mContactListener;

        std::unique_ptr<JPH::PhysicsSystem> mJoltSystem;

        std::unique_ptr<JPH::BroadPhaseLayerInterface> mBroadPhaseLayerInterface;
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> mObjectVsBroadPhaseLayerFilter;
        std::unique_ptr<JPH::ObjectLayerPairFilter> mObjectLayerPairFilter;

        // 非持有指针, 指向 PhysicsSystem 里共享的 TempAllocator/JobSystemThreadPool, 生命周期
        // 由 PhysicsSystem 保证长于所有它创建出来的 PhysicsScene。
        JPH::TempAllocator* mTempAllocator = nullptr;
        JPH::JobSystem* mJobSystem = nullptr;

        // 非持有指针。
        PhysicsSystem* mPhysicsSystem = nullptr;

        TPhysicsHandlePool<JPH::BodyID, PhysicsResourceType::Body> mBodyPool;

        struct ConstraintEntry
        {
            JPH::Ref<JPH::Constraint> constraint; // 不是 RefConst——马达目标之类运行时可能要改。
            PhysicsBodyHandle bodyA;
            PhysicsBodyHandle bodyB;
        };
        TPhysicsHandlePool<ConstraintEntry, PhysicsResourceType::Constraint> mConstraintPool;

        // body 在 mBodyPool 里的 index -> 挂在它身上的 Constraint 列表, destroyBody() 靠这个
        // 反查级联销毁, 避免 Jolt 里出现 Constraint 指向已销毁 Body 的状态。
        std::unordered_map<uint32_t, std::vector<PhysicsConstraintHandle>> mConstraintsByBody;

        std::vector<Guid> mOwnerByBodyIndex;

        // 有序容器让施加顺序可复现。
        std::map<uint32_t, PendingLoad> mPendingLoads;

        // 有序容器让遍历顺序可复现。
        std::map<uint64_t, OverlapEntry> mActiveOverlaps;
        std::vector<PhysicsOverlapEvent> mPendingOverlapEvents;

        // 固定步长模拟: 每帧真实 deltaTime 先攒进这里, 攒够一个步长就推进一次 Jolt, 不足一步
        // 的余量留到下一帧接着攒——真实帧时间抖动直接喂给求解器会导致数值不稳定, 这是所有
        // 物理引擎(包括 Jolt 自己的示例)的标准做法。
        float mAccumulator = 0.0f;

        bool mBroadPhaseOptimized = false;
    };
}
