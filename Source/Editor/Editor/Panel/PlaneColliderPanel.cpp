#include "PlaneColliderPanel.h"
#include "../ComponentRef.h"
#include "ComponentPanelRegistry.h"

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<PlaneCollider, PlaneColliderPanel> gRegistrar;
    }

    PlaneColliderPanel::PlaneColliderPanel(PlaneCollider& planeCollider) noexcept :
        ColliderPanel(planeCollider),
        mPlaneCollider(planeCollider)
    {
    }

    void PlaneColliderPanel::onDraw()
    {
        if (!BeginPropertyTable("PlaneColliderProperties", ImGuiTableFlags_BordersInnerH))
            return;

        ColliderPanel::onDraw();

        ComponentRef planeColliderRef = ComponentRef::capture(mPlaneCollider);
        auto& ddo = *static_cast<const PlaneCollider*>(PlaneCollider::staticClass()->getDefaultObject());

        BeginPropertyRow("Normal");
        float3 normalBefore = mPlaneCollider.getNormal();
        float3 normal = normalBefore;
        bool normalChanged = ImGui::DragFloat3("##Normal", &normal.x, 0.01f, 0.0f, 0.0f, "%.3f");
        auto normalSetter = [ref = planeColliderRef](float3 v) { ref.resolveAs<PlaneCollider>()->setNormal(v); };
        getUndoStack().trackGesture("Change Normal"_hs, normalBefore, normal, normalSetter);
        if (normalChanged)
            mPlaneCollider.setNormal(normal);
        float3 normalBeforeReset = normal;
        if (DrawPropertyReset("Normal", normal, ddo.getNormal()))
        {
            mPlaneCollider.setNormal(normal);
            pushReset("Reset Normal"_hs, normalSetter, normalBeforeReset, normal);
        }

        BeginPropertyRow("Constant");
        float constantBefore = mPlaneCollider.getConstant();
        float constant = constantBefore;
        bool constantChanged = ImGui::DragFloat("##Constant", &constant, 0.01f, 0.0f, 0.0f, "%.3f");
        auto constantSetter = [ref = planeColliderRef](float v) { ref.resolveAs<PlaneCollider>()->setConstant(v); };
        getUndoStack().trackGesture("Change Constant"_hs, constantBefore, constant, constantSetter);
        if (constantChanged)
            mPlaneCollider.setConstant(constant);
        float constantBeforeReset = constant;
        if (DrawPropertyReset("Constant", constant, ddo.getConstant()))
        {
            mPlaneCollider.setConstant(constant);
            pushReset("Reset Constant"_hs, constantSetter, constantBeforeReset, constant);
        }

        BeginPropertyRow("Half Extent");
        float halfExtentBefore = mPlaneCollider.getHalfExtent();
        float halfExtent = halfExtentBefore;
        bool halfExtentChanged = ImGui::DragFloat("##HalfExtent", &halfExtent, 1.0f, 0.0f, 0.0f, "%.3f");
        auto halfExtentSetter = [ref = planeColliderRef](float v) { ref.resolveAs<PlaneCollider>()->setHalfExtent(v); };
        getUndoStack().trackGesture("Change Half Extent"_hs, halfExtentBefore, halfExtent, halfExtentSetter);
        if (halfExtentChanged)
            mPlaneCollider.setHalfExtent(halfExtent);
        float halfExtentBeforeReset = halfExtent;
        if (DrawPropertyReset("Half Extent", halfExtent, ddo.getHalfExtent()))
        {
            mPlaneCollider.setHalfExtent(halfExtent);
            pushReset("Reset Half Extent"_hs, halfExtentSetter, halfExtentBeforeReset, halfExtent);
        }

        EndPropertyTable();
    }
}
