#include "HingeJointPanel.h"
#include "../ComponentRef.h"
#include "ComponentPanelRegistry.h"

#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<HingeJoint, HingeJointPanel> gRegistrar;
    }

    HingeJointPanel::HingeJointPanel(HingeJoint& hingeJoint) noexcept :
        JointPanel(hingeJoint),
        mHingeJoint(hingeJoint)
    {
    }

    void HingeJointPanel::onDraw()
    {
        if (!BeginPropertyTable("HingeJointProperties", ImGuiTableFlags_BordersInnerH))
            return;

        JointPanel::onDraw();

        ComponentRef hingeJointRef = ComponentRef::capture(mHingeJoint);
        auto& ddo = *static_cast<const HingeJoint*>(HingeJoint::staticClass()->getDefaultObject());

        BeginPropertyRow("Anchor");
        float3 anchorBefore = mHingeJoint.getAnchor();
        float3 anchor = anchorBefore;
        bool anchorChanged = ImGui::DragFloat3("##Anchor", &anchor.x, 0.01f, 0.0f, 0.0f, "%.3f");
        auto anchorSetter = [ref = hingeJointRef](float3 v) { ref.resolveAs<HingeJoint>()->setAnchor(v); };
        getUndoStack().trackGesture("Change Anchor"_hs, anchorBefore, anchor, anchorSetter);
        if (anchorChanged)
            mHingeJoint.setAnchor(anchor);
        float3 anchorBeforeReset = anchor;
        if (DrawPropertyReset("Anchor", anchor, ddo.getAnchor()))
        {
            mHingeJoint.setAnchor(anchor);
            pushReset("Reset Anchor"_hs, anchorSetter, anchorBeforeReset, anchor);
        }

        BeginPropertyRow("Connected Anchor");
        float3 connectedAnchorBefore = mHingeJoint.getConnectedAnchor();
        float3 connectedAnchor = connectedAnchorBefore;
        bool connectedAnchorChanged = ImGui::DragFloat3("##ConnectedAnchor", &connectedAnchor.x, 0.01f, 0.0f, 0.0f, "%.3f");
        auto connectedAnchorSetter = [ref = hingeJointRef](float3 v) { ref.resolveAs<HingeJoint>()->setConnectedAnchor(v); };
        getUndoStack().trackGesture("Change Connected Anchor"_hs, connectedAnchorBefore, connectedAnchor, connectedAnchorSetter);
        if (connectedAnchorChanged)
            mHingeJoint.setConnectedAnchor(connectedAnchor);
        float3 connectedAnchorBeforeReset = connectedAnchor;
        if (DrawPropertyReset("Connected Anchor", connectedAnchor, ddo.getConnectedAnchor()))
        {
            mHingeJoint.setConnectedAnchor(connectedAnchor);
            pushReset("Reset Connected Anchor"_hs, connectedAnchorSetter, connectedAnchorBeforeReset, connectedAnchor);
        }

        BeginPropertyRow("Axis");
        float3 axisBefore = mHingeJoint.getAxis();
        float3 axis = axisBefore;
        bool axisChanged = ImGui::DragFloat3("##Axis", &axis.x, 0.01f, 0.0f, 0.0f, "%.3f");
        auto axisSetter = [ref = hingeJointRef](float3 v) { ref.resolveAs<HingeJoint>()->setAxis(v); };
        getUndoStack().trackGesture("Change Axis"_hs, axisBefore, axis, axisSetter);
        if (axisChanged)
            mHingeJoint.setAxis(axis);
        float3 axisBeforeReset = axis;
        if (DrawPropertyReset("Axis", axis, ddo.getAxis()))
        {
            mHingeJoint.setAxis(axis);
            pushReset("Reset Axis"_hs, axisSetter, axisBeforeReset, axis);
        }

        BeginPropertyRow("Min Angle");
        float minAngleBefore = mHingeJoint.getMinAngle();
        float minAngle = minAngleBefore;
        bool minAngleChanged = ImGui::DragFloat("##MinAngle", &minAngle, 1.0f, -180.0f, 0.0f, "%.1f deg");
        auto minAngleSetter = [ref = hingeJointRef](float v) { ref.resolveAs<HingeJoint>()->setMinAngle(v); };
        getUndoStack().trackGesture("Change Min Angle"_hs, minAngleBefore, minAngle, minAngleSetter);
        if (minAngleChanged)
            mHingeJoint.setMinAngle(minAngle);
        float minAngleBeforeReset = minAngle;
        if (DrawPropertyReset("Min Angle", minAngle, ddo.getMinAngle()))
        {
            mHingeJoint.setMinAngle(minAngle);
            pushReset("Reset Min Angle"_hs, minAngleSetter, minAngleBeforeReset, minAngle);
        }

        BeginPropertyRow("Max Angle");
        float maxAngleBefore = mHingeJoint.getMaxAngle();
        float maxAngle = maxAngleBefore;
        bool maxAngleChanged = ImGui::DragFloat("##MaxAngle", &maxAngle, 1.0f, 0.0f, 180.0f, "%.1f deg");
        auto maxAngleSetter = [ref = hingeJointRef](float v) { ref.resolveAs<HingeJoint>()->setMaxAngle(v); };
        getUndoStack().trackGesture("Change Max Angle"_hs, maxAngleBefore, maxAngle, maxAngleSetter);
        if (maxAngleChanged)
            mHingeJoint.setMaxAngle(maxAngle);
        float maxAngleBeforeReset = maxAngle;
        if (DrawPropertyReset("Max Angle", maxAngle, ddo.getMaxAngle()))
        {
            mHingeJoint.setMaxAngle(maxAngle);
            pushReset("Reset Max Angle"_hs, maxAngleSetter, maxAngleBeforeReset, maxAngle);
        }

        EndPropertyTable();
    }
}
