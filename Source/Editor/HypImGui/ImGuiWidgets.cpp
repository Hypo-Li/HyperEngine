#include "ImGuiWidgets.h"
#include "ImGuiFrontend.h"

namespace hyp
{
    bool TextOnlyButton(const char* label)
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 textSize = ImGui::CalcTextSize(label);
        bool clicked = ImGui::InvisibleButton(label, textSize);
        bool held = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();
        ImU32 color = ImGui::GetColorU32(hovered ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        // 按下(IsItemActive, 鼠标左键还没松开)时文字往下挪 1px(按 DPI 缩放), 模拟物理
        // 按钮被摁下去的位移感, 不需要额外调颜色。
        ImVec2 drawPos = held ? ImVec2(pos.x, pos.y + 1.0f * ImGuiFrontend::getDpiScale()) : pos;
        ImGui::GetWindowDrawList()->AddText(drawPos, color, label);
        return clicked;
    }
}
