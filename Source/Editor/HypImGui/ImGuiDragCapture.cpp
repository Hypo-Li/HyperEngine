#include "ImGuiDragCapture.h"

#include <Platform/Window.h>

#include <imgui.h>

namespace hyp
{
    void SetCursorCaptured(Window& window, bool captured)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (captured)
        {
            window.captureCursor();
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        }
        else
        {
            window.releaseCursor();
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }
    }

    void CaptureCursorDuringDrag(Window& window)
    {
        // DragXXX 双击(或者按住 Ctrl 点击)不是开始拖拽, 是 ImGui 内部切换成文本框输入模式,
        // 让你直接打字改数值——但这种情况下它一样会 SetActiveID(), IsItemActivated() 分不出
        // 这两种情况的区别。这里用跟 ImGui 自己 DragBehavior() 判断"要不要进文本输入模式"完全
        // 相同的条件(双击 或 Ctrl+点击)反过来排除掉, 双击输入数值时就不会误触发光标捕获了。
        ImGuiIO& io = ImGui::GetIO();
        bool enteringTextInput = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || io.KeyCtrl;
        if (ImGui::IsItemActivated() && !enteringTextInput)
            SetCursorCaptured(window, true);
        if (ImGui::IsItemDeactivated())
            SetCursorCaptured(window, false);
    }
}
