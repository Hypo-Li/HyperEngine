#include "InspectorPanel.h"

#include <HypImGui/ImGuiFrontend.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace hyp::ed
{
    namespace
    {
        constexpr const char* kReorderPayloadType = "INSPECTOR_PANEL_REORDER";
    }

    IInspectorPanel::IInspectorPanel(Object& target) noexcept :
        mTarget(target)
    {
    }

    InspectorPanelAction IInspectorPanel::draw()
    {
        bool visible = true;

        ImGui::PushID(&mTarget);
        bool expanded = canRemove()
            ? ImGui::CollapsingHeader(getTitle(), &visible, ImGuiTreeNodeFlags_DefaultOpen)
            : ImGui::CollapsingHeader(getTitle(), ImGuiTreeNodeFlags_DefaultOpen);
        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();

        if (mReorderEnabled && ImGui::BeginDragDropSource())
        {
            const Guid draggedGuid = mTarget.getGuid();
            ImGui::SetDragDropPayload(kReorderPayloadType, &draggedGuid, sizeof(draggedGuid));
            ImGui::TextUnformatted(getTitle());
            ImGui::EndDragDropSource();
        }

        if (expanded && visible)
        {
            onDraw();
        }

        if (mReorderEnabled)
        {
            // 内容画完才知道面板的底边, 目标矩形覆盖整个面板(头部 + 内容), 上半算插到之前, 下半算插到之后。
            const float panelBottom = std::max(headerMax.y, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y);
            const ImRect panelRect(headerMin, ImVec2(headerMax.x, panelBottom));
            if (ImGui::BeginDragDropTargetCustom(panelRect, ImGui::GetID("##PanelReorderTarget")))
            {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                    kReorderPayloadType,
                    ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                if (payload && payload->DataSize == sizeof(Guid))
                {
                    const Guid draggedGuid = *static_cast<const Guid*>(payload->Data);
                    if (draggedGuid != mTarget.getGuid())
                    {
                        const bool insertAfter = ImGui::GetMousePos().y > (panelRect.Min.y + panelRect.Max.y) * 0.5f;

                        const float lineY = insertAfter ? panelRect.Max.y : panelRect.Min.y;
                        ImDrawList* drawList = ImGui::GetForegroundDrawList();
                        const ImVec2 windowMin = ImGui::GetWindowPos();
                        drawList->PushClipRect(windowMin, ImVec2(windowMin.x + ImGui::GetWindowSize().x, windowMin.y + ImGui::GetWindowSize().y), true);
                        drawList->AddLine(ImVec2(panelRect.Min.x, lineY), ImVec2(panelRect.Max.x, lineY), ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f * ImGuiFrontend::getDpiScale());
                        drawList->PopClipRect();

                        if (payload->IsDelivery())
                            mPendingReorder = PanelReorderRequest{ draggedGuid, insertAfter };
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::PopID();

        if (!visible)
            return InspectorPanelAction::Remove;
        return mPendingReorder ? InspectorPanelAction::Reorder : InspectorPanelAction::None;
    }

    bool IInspectorPanel::canRemove() const noexcept
    {
        return false;
    }

    Object& IInspectorPanel::getTarget() const noexcept
    {
        return mTarget;
    }
}
