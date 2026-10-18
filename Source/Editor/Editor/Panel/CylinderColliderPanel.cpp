#include "CylinderColliderPanel.h"
#include "../ComponentRef.h"
#include "ComponentPanelRegistry.h"

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<CylinderCollider, CylinderColliderPanel> gRegistrar;
    }

    CylinderColliderPanel::CylinderColliderPanel(CylinderCollider& cylinderCollider) noexcept :
        ColliderPanel(cylinderCollider),
        mCylinderCollider(cylinderCollider)
    {
    }

    void CylinderColliderPanel::onDraw()
    {
        if (!BeginPropertyTable("CylinderColliderProperties", ImGuiTableFlags_BordersInnerH))
            return;

        ColliderPanel::onDraw();

        ComponentRef cylinderColliderRef = ComponentRef::capture(mCylinderCollider);
        auto& ddo = *static_cast<const CylinderCollider*>(CylinderCollider::staticClass()->getDefaultObject());

        BeginPropertyRow("Half Height");
        float halfHeightBefore = mCylinderCollider.getHalfHeight();
        float halfHeight = halfHeightBefore;
        bool halfHeightChanged = ImGui::DragFloat("##HalfHeight", &halfHeight, 0.01f, 0.0f, 0.0f, "%.3f");
        auto halfHeightSetter = [ref = cylinderColliderRef](float v) { ref.resolveAs<CylinderCollider>()->setHalfHeight(v); };
        getUndoStack().trackGesture("Change Half Height"_hs, halfHeightBefore, halfHeight, halfHeightSetter);
        if (halfHeightChanged)
            mCylinderCollider.setHalfHeight(halfHeight);
        float halfHeightBeforeReset = halfHeight;
        if (DrawPropertyReset("Half Height", halfHeight, ddo.getHalfHeight()))
        {
            mCylinderCollider.setHalfHeight(halfHeight);
            pushReset("Reset Half Height"_hs, halfHeightSetter, halfHeightBeforeReset, halfHeight);
        }

        BeginPropertyRow("Radius");
        float radiusBefore = mCylinderCollider.getRadius();
        float radius = radiusBefore;
        bool radiusChanged = ImGui::DragFloat("##Radius", &radius, 0.01f, 0.0f, 0.0f, "%.3f");
        auto radiusSetter = [ref = cylinderColliderRef](float v) { ref.resolveAs<CylinderCollider>()->setRadius(v); };
        getUndoStack().trackGesture("Change Radius"_hs, radiusBefore, radius, radiusSetter);
        if (radiusChanged)
            mCylinderCollider.setRadius(radius);
        float radiusBeforeReset = radius;
        if (DrawPropertyReset("Radius", radius, ddo.getRadius()))
        {
            mCylinderCollider.setRadius(radius);
            pushReset("Reset Radius"_hs, radiusSetter, radiusBeforeReset, radius);
        }

        EndPropertyTable();
    }
}
