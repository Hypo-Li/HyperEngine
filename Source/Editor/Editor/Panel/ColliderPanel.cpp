#include "ColliderPanel.h"
#include "../AssetComboEd.h"
#include "../ThumbnailProvider.h"
#include "../ComponentRef.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Core/Reflection/Class.h>
#include <Engine/Context.h>
#include <Engine/Physics/PhysicsMaterial.h>
#include <Engine/Physics/PhysicsSystem.h>

#include <HypImGui/ImGuiEnumWidgets.h>
#include <HypImGui/ImGuiFilterableCombo.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        TRef<PhysicsMaterial> resolvePhysicsMaterialAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<PhysicsMaterial>() : TRef<PhysicsMaterial>();
        }
    }

    ColliderPanel::ColliderPanel(Collider& collider) noexcept :
        IComponentPanel(collider),
        mCollider(collider)
    {
    }

    void ColliderPanel::onDraw()
    {
        ComponentRef colliderRef = ComponentRef::capture(mCollider);
        // mCollider.getClass() 是虚函数调用, 拿到的是实际具体子类(比如 BoxCollider)的 Class,
        // 它的默认对象上 Collider 这几个字段的值就是 Collider 自己声明的默认值——不需要单独
        // 为 Collider 本身构造一份默认对象。
        auto& ddo = *static_cast<const Collider*>(mCollider.getClass()->getDefaultObject());

        BeginPropertyRow("Is Trigger");
        bool isTriggerBefore = mCollider.isTrigger();
        bool isTrigger = isTriggerBefore;
        bool isTriggerChanged = ImGui::Checkbox("##IsTrigger", &isTrigger);
        auto isTriggerSetter = [ref = colliderRef](bool v) { ref.resolveAs<Collider>()->setTrigger(v); };
        getUndoStack().trackGesture("Change Is Trigger"_hs, isTriggerBefore, isTrigger, isTriggerSetter);
        if (isTriggerChanged)
            mCollider.setTrigger(isTrigger);
        bool isTriggerBeforeReset = isTrigger;
        if (DrawPropertyReset("Is Trigger", isTrigger, ddo.isTrigger()))
        {
            mCollider.setTrigger(isTrigger);
            pushReset("Reset Is Trigger"_hs, isTriggerSetter, isTriggerBeforeReset, isTrigger);
        }

        BeginPropertyRow("Layer");
        PhysicsSystem& physicsSystem = Context::get().getPhysicsSystem();
        const PhysicsLayerSettings& layerSettings = physicsSystem.getLayerSettings();
        HString layerBefore = mCollider.getLayer();
        HString layer = layerBefore;
        std::string layerPreview = layerBefore.toCString();
        if (physicsSystem.findLayerSlot(layerBefore) < 0)
            layerPreview += " (Missing)";
        bool layerChanged = false;
        if (BeginFilterableCombo("##Layer", layerPreview.c_str()))
        {
            HString defaultLayerName = PhysicsLayerSettings::getDefaultLayerName();
            if (FilterableComboItem(defaultLayerName.toCString(), layerBefore == defaultLayerName))
            {
                layer = defaultLayerName;
                layerChanged = true;
            }
            for (HString name : layerSettings.layerNames)
            {
                if (FilterableComboItem(name.toCString(), layerBefore == name))
                {
                    layer = name;
                    layerChanged = true;
                }
            }
            EndFilterableCombo();
        }
        auto layerSetter = [ref = colliderRef](HString v) { ref.resolveAs<Collider>()->setLayer(v); };
        getUndoStack().trackGesture("Change Layer"_hs, layerBefore, layer, layerSetter);
        if (layerChanged)
            mCollider.setLayer(layer);
        HString layerBeforeReset = layer;
        if (DrawPropertyReset("Layer", layer, ddo.getLayer()))
        {
            mCollider.setLayer(layer);
            pushReset("Reset Layer"_hs, layerSetter, layerBeforeReset, layer);
        }

        BeginPropertyRow("Center");
        float3 centerBefore = mCollider.getCenter();
        float3 center = centerBefore;
        bool centerChanged = ImGui::DragFloat3("##Center", &center.x, 0.01f, 0.0f, 0.0f, "%.3f");
        auto centerSetter = [ref = colliderRef](float3 v) { ref.resolveAs<Collider>()->setCenter(v); };
        getUndoStack().trackGesture("Change Center"_hs, centerBefore, center, centerSetter);
        if (centerChanged)
            mCollider.setCenter(center);
        float3 centerBeforeReset = center;
        if (DrawPropertyReset("Center", center, ddo.getCenter()))
        {
            mCollider.setCenter(center);
            pushReset("Reset Center"_hs, centerSetter, centerBeforeReset, center);
        }

        BeginPropertyRow("Motion Type");
        MotionType motionTypeBefore = mCollider.getMotionType();
        MotionType motionType = motionTypeBefore;
        bool motionTypeChanged = mCollider.supportsDynamicMotion()
            ? EnumCombo("##MotionType", &motionType)
            : EnumCombo<MotionType>("##MotionType", &motionType, [](MotionType v) { return v != MotionType::Dynamic; });
        auto motionTypeSetter = [ref = colliderRef](MotionType v) { ref.resolveAs<Collider>()->setMotionType(v); };
        getUndoStack().trackGesture("Change Motion Type"_hs, motionTypeBefore, motionType, motionTypeSetter);
        if (motionTypeChanged)
            mCollider.setMotionType(motionType);
        MotionType motionTypeBeforeReset = motionType;
        if (DrawPropertyReset("Motion Type", motionType, ddo.getMotionType()))
        {
            mCollider.setMotionType(motionType);
            pushReset("Reset Motion Type"_hs, motionTypeSetter, motionTypeBeforeReset, motionType);
        }

        ImGui::BeginDisabled(motionType != MotionType::Dynamic);
        BeginPropertyRow("Override Mass");
        bool overrideMassBefore = mCollider.isOverrideMass();
        bool overrideMass = overrideMassBefore;
        bool overrideMassChanged = ImGui::Checkbox("##OverrideMass", &overrideMass);
        auto overrideMassSetter = [ref = colliderRef](bool v) { ref.resolveAs<Collider>()->setOverrideMass(v); };
        getUndoStack().trackGesture("Change Override Mass"_hs, overrideMassBefore, overrideMass, overrideMassSetter);
        if (overrideMassChanged)
            mCollider.setOverrideMass(overrideMass);
        bool overrideMassBeforeReset = overrideMass;
        if (DrawPropertyReset("Override Mass", overrideMass, ddo.isOverrideMass()))
        {
            mCollider.setOverrideMass(overrideMass);
            pushReset("Reset Override Mass"_hs, overrideMassSetter, overrideMassBeforeReset, overrideMass);
        }

        ImGui::BeginDisabled(!overrideMass);
        BeginPropertyRow("Mass");
        float massBefore = mCollider.getMass();
        float mass = massBefore;
        bool massChanged = ImGui::DragFloat("##Mass", &mass, 0.1f, 0.0f, 0.0f, "%.3f");
        auto massSetter = [ref = colliderRef](float v) { ref.resolveAs<Collider>()->setMass(v); };
        getUndoStack().trackGesture("Change Mass"_hs, massBefore, mass, massSetter);
        if (massChanged)
            mCollider.setMass(mass);
        float massBeforeReset = mass;
        if (DrawPropertyReset("Mass", mass, ddo.getMass()))
        {
            mCollider.setMass(mass);
            pushReset("Reset Mass"_hs, massSetter, massBeforeReset, mass);
        }
        ImGui::EndDisabled();

        BeginPropertyRow("Override Center Of Mass");
        bool overrideCenterOfMassBefore = mCollider.isOverrideCenterOfMass();
        bool overrideCenterOfMass = overrideCenterOfMassBefore;
        bool overrideCenterOfMassChanged = ImGui::Checkbox("##OverrideCenterOfMass", &overrideCenterOfMass);
        auto overrideCenterOfMassSetter = [ref = colliderRef](bool v) { ref.resolveAs<Collider>()->setOverrideCenterOfMass(v); };
        getUndoStack().trackGesture("Change Override Center Of Mass"_hs, overrideCenterOfMassBefore, overrideCenterOfMass, overrideCenterOfMassSetter);
        if (overrideCenterOfMassChanged)
            mCollider.setOverrideCenterOfMass(overrideCenterOfMass);
        bool overrideCenterOfMassBeforeReset = overrideCenterOfMass;
        if (DrawPropertyReset("Override Center Of Mass", overrideCenterOfMass, ddo.isOverrideCenterOfMass()))
        {
            mCollider.setOverrideCenterOfMass(overrideCenterOfMass);
            pushReset("Reset Override Center Of Mass"_hs, overrideCenterOfMassSetter, overrideCenterOfMassBeforeReset, overrideCenterOfMass);
        }

        ImGui::BeginDisabled(!overrideCenterOfMass);
        BeginPropertyRow("Center Of Mass Offset");
        float3 centerOfMassOffsetBefore = mCollider.getCenterOfMassOffset();
        float3 centerOfMassOffset = centerOfMassOffsetBefore;
        bool centerOfMassOffsetChanged = ImGui::DragFloat3("##CenterOfMassOffset", &centerOfMassOffset.x, 0.01f, 0.0f, 0.0f, "%.3f");
        auto centerOfMassOffsetSetter = [ref = colliderRef](float3 v) { ref.resolveAs<Collider>()->setCenterOfMassOffset(v); };
        getUndoStack().trackGesture("Change Center Of Mass Offset"_hs, centerOfMassOffsetBefore, centerOfMassOffset, centerOfMassOffsetSetter);
        if (centerOfMassOffsetChanged)
            mCollider.setCenterOfMassOffset(centerOfMassOffset);
        float3 centerOfMassOffsetBeforeReset = centerOfMassOffset;
        if (DrawPropertyReset("Center Of Mass Offset", centerOfMassOffset, ddo.getCenterOfMassOffset()))
        {
            mCollider.setCenterOfMassOffset(centerOfMassOffset);
            pushReset("Reset Center Of Mass Offset"_hs, centerOfMassOffsetSetter, centerOfMassOffsetBeforeReset, centerOfMassOffset);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        AssetManager& assetManager = Context::get().getAssetManager();
        BeginPropertyRow("Physics Material", 2.0f); // AssetCombo 是两行缩略图布局
        PhysicsMaterial* physicsMaterial = mCollider.getPhysicsMaterial();
        Guid physicsMaterialGuidBefore = physicsMaterial ? physicsMaterial->getGuid() : Guid();
        Guid physicsMaterialGuid = physicsMaterialGuidBefore;
        bool physicsMaterialChanged = AssetComboEd("##PhysicsMaterial", assetManager, PhysicsMaterial::staticClass(), true, physicsMaterialGuid, getAssetBrowser(), getIconProvider());
        physicsMaterialChanged |= DrawPropertyReset("Physics Material", physicsMaterialGuid, Guid());
        if (physicsMaterialChanged)
        {
            mCollider.setPhysicsMaterial(resolvePhysicsMaterialAsset(assetManager, physicsMaterialGuid).get());

            getUndoStack().push("Change Physics Material"_hs,
                [ref = colliderRef, guid = physicsMaterialGuidBefore]()
                {
                    ref.resolveAs<Collider>()->setPhysicsMaterial(resolvePhysicsMaterialAsset(Context::get().getAssetManager(), guid).get());
                },
                [ref = colliderRef, guid = physicsMaterialGuid]()
                {
                    ref.resolveAs<Collider>()->setPhysicsMaterial(resolvePhysicsMaterialAsset(Context::get().getAssetManager(), guid).get());
                });
        }
    }
}
