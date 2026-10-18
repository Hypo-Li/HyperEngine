#include "JointPanel.h"
#include "../ComponentRef.h"

#include <Engine/Component/Collider/Collider.h>
#include <Engine/Framework/Entity.h>
#include <Engine/Framework/Scene.h>

#include <Core/Reflection/Class.h>

#include <HypImGui/ImGuiFilterableCombo.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

#include <string>

namespace hyp::ed
{
    namespace
    {
        bool isCollider(Component& component)
        {
            return component.getClass()->isDerivedFrom(Collider::staticClass());
        }

        std::string colliderLabel(Component& component)
        {
            return std::string(component.getEntity()->getName().toStringView()) + " (" + component.getClass()->getName().toCString() + ")";
        }

        // allowNone: Connected Collider 允许留空(= 连接静态世界), Collider 不允许——
        // Joint::onTick() 拿 mCollider 无效当"这个 Joint 自己的碰撞体还没配置", 会一直
        // 停在重试状态, 不会真的建出约束。
        bool ColliderCombo(const char* strId, Scene& scene, Guid& guid, bool allowNone)
        {
            Component* current = guid.isValid() ? scene.findComponent(guid) : nullptr;
            std::string preview = current ? colliderLabel(*current)
                : (guid.isValid() ? "(Missing)" : "(None)");

            bool changed = false;
            if (BeginFilterableCombo(strId, preview.c_str()))
            {
                if (allowNone && FilterableComboItem("(None)", !guid.isValid()))
                {
                    guid = Guid();
                    changed = true;
                }

                for (const TRef<Entity>& entityRef : scene.getEntities())
                {
                    for (const TRef<Component>& componentRef : entityRef->getAllComponents())
                    {
                        Component* component = componentRef.get();
                        if (!isCollider(*component))
                            continue;

                        ImGui::PushID(component);
                        if (FilterableComboItem(colliderLabel(*component).c_str(), component->getGuid() == guid))
                        {
                            guid = component->getGuid();
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                }

                EndFilterableCombo();
            }
            return changed;
        }

        // Joint 的公开接口现在收发 Collider*(TSoftRef<Collider> 的自然形态), 不是 Guid——
        // 面板这边选择器还是按 Guid 工作(需要枚举场景里所有 Collider), 存取 Joint 之前
        // 在这一层做一次转换。guid 无效或者查到的组件不是 Collider 都转成 nullptr。
        Collider* resolveColliderInScene(Scene& scene, Guid guid)
        {
            if (!guid.isValid())
                return nullptr;
            Component* component = scene.findComponent(guid);
            if (!component || !isCollider(*component))
                return nullptr;
            return static_cast<Collider*>(component);
        }
    }

    JointPanel::JointPanel(Joint& joint) noexcept :
        IComponentPanel(joint),
        mJoint(joint)
    {
    }

    void JointPanel::onDraw()
    {
        Scene* scene = mJoint.getScene();
        if (!scene)
            return;

        ComponentRef jointRef = ComponentRef::capture(mJoint);

        BeginPropertyRow("Collider");
        Collider* colliderBefore = mJoint.getCollider();
        Guid colliderGuidBefore = colliderBefore ? colliderBefore->getGuid() : Guid();
        Guid colliderGuid = colliderGuidBefore;
        if (ColliderCombo("##Collider", *scene, colliderGuid, false))
        {
            mJoint.setCollider(resolveColliderInScene(*scene, colliderGuid));
            getUndoStack().push("Change Collider"_hs,
                [ref = jointRef, guid = colliderGuidBefore]()
                {
                    Joint* joint = ref.resolveAs<Joint>();
                    joint->setCollider(resolveColliderInScene(*joint->getScene(), guid));
                },
                [ref = jointRef, guid = colliderGuid]()
                {
                    Joint* joint = ref.resolveAs<Joint>();
                    joint->setCollider(resolveColliderInScene(*joint->getScene(), guid));
                });
        }

        BeginPropertyRow("Connected Collider");
        Collider* connectedBefore = mJoint.getConnectedCollider();
        Guid connectedGuidBefore = connectedBefore ? connectedBefore->getGuid() : Guid();
        Guid connectedGuid = connectedGuidBefore;
        if (ColliderCombo("##ConnectedCollider", *scene, connectedGuid, true))
        {
            mJoint.setConnectedCollider(resolveColliderInScene(*scene, connectedGuid));
            getUndoStack().push("Change Connected Collider"_hs,
                [ref = jointRef, guid = connectedGuidBefore]()
                {
                    Joint* joint = ref.resolveAs<Joint>();
                    joint->setConnectedCollider(resolveColliderInScene(*joint->getScene(), guid));
                },
                [ref = jointRef, guid = connectedGuid]()
                {
                    Joint* joint = ref.resolveAs<Joint>();
                    joint->setConnectedCollider(resolveColliderInScene(*joint->getScene(), guid));
                });
        }
    }
}
