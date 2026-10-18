#include "CapsuleColliderPanel.h"
#include "../ComponentRef.h"
#include "ComponentPanelRegistry.h"

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<CapsuleCollider, CapsuleColliderPanel> gRegistrar;
    }

    CapsuleColliderPanel::CapsuleColliderPanel(CapsuleCollider& capsuleCollider) noexcept :
        ColliderPanel(capsuleCollider),
        mCapsuleCollider(capsuleCollider)
    {
    }

    void CapsuleColliderPanel::onDraw()
    {
        if (!BeginPropertyTable("CapsuleColliderProperties", ImGuiTableFlags_BordersInnerH))
            return;

        ColliderPanel::onDraw();

        ComponentRef capsuleColliderRef = ComponentRef::capture(mCapsuleCollider);
        auto& ddo = *static_cast<const CapsuleCollider*>(CapsuleCollider::staticClass()->getDefaultObject());

        BeginPropertyRow("Half Height");
        float halfHeightBefore = mCapsuleCollider.getHalfHeight();
        float halfHeight = halfHeightBefore;
        bool halfHeightChanged = ImGui::DragFloat("##HalfHeight", &halfHeight, 0.01f, 0.0f, 0.0f, "%.3f");
        auto halfHeightSetter = [ref = capsuleColliderRef](float v) { ref.resolveAs<CapsuleCollider>()->setHalfHeight(v); };
        getUndoStack().trackGesture("Change Half Height"_hs, halfHeightBefore, halfHeight, halfHeightSetter);
        if (halfHeightChanged)
            mCapsuleCollider.setHalfHeight(halfHeight);
        float halfHeightBeforeReset = halfHeight;
        if (DrawPropertyReset("Half Height", halfHeight, ddo.getHalfHeight()))
        {
            mCapsuleCollider.setHalfHeight(halfHeight);
            pushReset("Reset Half Height"_hs, halfHeightSetter, halfHeightBeforeReset, halfHeight);
        }

        BeginPropertyRow("Radius");
        float radiusBefore = mCapsuleCollider.getRadius();
        float radius = radiusBefore;
        bool radiusChanged = ImGui::DragFloat("##Radius", &radius, 0.01f, 0.0f, 0.0f, "%.3f");
        auto radiusSetter = [ref = capsuleColliderRef](float v) { ref.resolveAs<CapsuleCollider>()->setRadius(v); };
        getUndoStack().trackGesture("Change Radius"_hs, radiusBefore, radius, radiusSetter);
        if (radiusChanged)
            mCapsuleCollider.setRadius(radius);
        float radiusBeforeReset = radius;
        if (DrawPropertyReset("Radius", radius, ddo.getRadius()))
        {
            mCapsuleCollider.setRadius(radius);
            pushReset("Reset Radius"_hs, radiusSetter, radiusBeforeReset, radius);
        }

        EndPropertyTable();
    }
}
