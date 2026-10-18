#include "Joint.h"
#include "../Collider/Collider.h"
#include "../../Framework/Scene.h"
#include "../../Physics/PhysicsScene.h"

#include <Core/Reflection/Reflection.h>

namespace hyp
{
    REGISTER_CLASS(Joint)

    void Joint::onRegister()
    {
        // 解析/重试逻辑放在 onTick() 里(见下), 这里只需要把自己接入 tick——不依赖物理步进
        // 结果, 放 Pre_Physics 或 Post_Physics 都一样, 跟 Collider 一样显式给一个值。
        setTickGroup(TickGroup::Pre_Physics);
    }

    void Joint::onUnregister()
    {
        if (PhysicsScene* physicsScene = getScene()->getPhysicsScene())
            physicsScene->destroyConstraint(mConstraintHandle);
        mConstraintHandle = PhysicsConstraintHandle();
    }

    void Joint::onTick(float deltaTime)
    {
        // Constraint 建好之后在 Jolt 里自己参与模拟, 不需要每帧碰它。
        if (mConstraintHandle.isValid())
            return;

        PhysicsScene* physicsScene = getScene()->getPhysicsScene();
        if (!physicsScene)
            return;

        Collider* collider = getCollider();
        if (!collider)
            return; // 下一帧再试——bodyA 所在 Entity 可能还没轮到 onRegister()。

        PhysicsBodyHandle bodyA = collider->getBodyHandle();
        if (!bodyA.isValid())
            return;

        PhysicsBodyHandle bodyB;
        if (mConnectedCollider.isValid())
        {
            Collider* connectedCollider = getConnectedCollider();
            if (!connectedCollider)
                return; // 下一帧再试, 目标 Entity 可能是运行时后来才生成的。

            bodyB = connectedCollider->getBodyHandle();
            if (!bodyB.isValid())
                return;
        }

        PhysicsScene::ConstraintSettings settings;
        settings.bodyA = bodyA;
        settings.bodyB = bodyB;
        settings.desc = getConstraintDesc();
        mConstraintHandle = physicsScene->createConstraint(settings);
    }

    template <> Type* Reflection::createType<Joint>()
    {
        auto c = new TClass<Joint, Component>("Joint");
        c->addProperty("Collider", &Joint::mCollider);
        c->addProperty("Connected Collider", &Joint::mConnectedCollider);
        return c;
    }
}
