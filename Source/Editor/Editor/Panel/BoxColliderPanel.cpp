#include "BoxColliderPanel.h"
#include "../ComponentRef.h"
#include "ComponentPanelRegistry.h"

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<BoxCollider, BoxColliderPanel> gRegistrar;
    }

    BoxColliderPanel::BoxColliderPanel(BoxCollider& boxCollider) noexcept :
        ColliderPanel(boxCollider),
        mBoxCollider(boxCollider)
    {
    }

    void BoxColliderPanel::onDraw()
    {
        if (!BeginPropertyTable("BoxColliderProperties", ImGuiTableFlags_BordersInnerH))
            return;

        ColliderPanel::onDraw();

        ComponentRef boxColliderRef = ComponentRef::capture(mBoxCollider);
        auto& ddo = *static_cast<const BoxCollider*>(BoxCollider::staticClass()->getDefaultObject());

        BeginPropertyRow("Half Extent");
        float3 halfExtentBefore = mBoxCollider.getHalfExtent();
        float3 halfExtent = halfExtentBefore;
        bool halfExtentChanged = ImGui::DragFloat3("##HalfExtent", &halfExtent.x, 0.01f, 0.0f, 0.0f, "%.3f");
        auto halfExtentSetter = [ref = boxColliderRef](float3 v) { ref.resolveAs<BoxCollider>()->setHalfExtent(v); };
        getUndoStack().trackGesture("Change Half Extent"_hs, halfExtentBefore, halfExtent, halfExtentSetter);
        if (halfExtentChanged)
            mBoxCollider.setHalfExtent(halfExtent);
        float3 halfExtentBeforeReset = halfExtent;
        if (DrawPropertyReset("Half Extent", halfExtent, ddo.getHalfExtent()))
        {
            mBoxCollider.setHalfExtent(halfExtent);
            pushReset("Reset Half Extent"_hs, halfExtentSetter, halfExtentBeforeReset, halfExtent);
        }

        EndPropertyTable();
    }
}
