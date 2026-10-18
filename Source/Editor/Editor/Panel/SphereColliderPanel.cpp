#include "SphereColliderPanel.h"
#include "../ComponentRef.h"
#include "ComponentPanelRegistry.h"

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<SphereCollider, SphereColliderPanel> gRegistrar;
    }

    SphereColliderPanel::SphereColliderPanel(SphereCollider& sphereCollider) noexcept :
        ColliderPanel(sphereCollider),
        mSphereCollider(sphereCollider)
    {
    }

    void SphereColliderPanel::onDraw()
    {
        if (!BeginPropertyTable("SphereColliderProperties", ImGuiTableFlags_BordersInnerH))
            return;

        ColliderPanel::onDraw();

        ComponentRef sphereColliderRef = ComponentRef::capture(mSphereCollider);
        auto& ddo = *static_cast<const SphereCollider*>(SphereCollider::staticClass()->getDefaultObject());

        BeginPropertyRow("Radius");
        float radiusBefore = mSphereCollider.getRadius();
        float radius = radiusBefore;
        bool radiusChanged = ImGui::DragFloat("##Radius", &radius, 0.01f, 0.0f, 0.0f, "%.3f");
        auto radiusSetter = [ref = sphereColliderRef](float v) { ref.resolveAs<SphereCollider>()->setRadius(v); };
        getUndoStack().trackGesture("Change Radius"_hs, radiusBefore, radius, radiusSetter);
        if (radiusChanged)
            mSphereCollider.setRadius(radius);
        float radiusBeforeReset = radius;
        if (DrawPropertyReset("Radius", radius, ddo.getRadius()))
        {
            mSphereCollider.setRadius(radius);
            pushReset("Reset Radius"_hs, radiusSetter, radiusBeforeReset, radius);
        }

        EndPropertyTable();
    }
}
