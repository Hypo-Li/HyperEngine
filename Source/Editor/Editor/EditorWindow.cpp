#include "EditorWindow.h"

namespace hyp::ed
{
    EditorWindow::EditorWindow(std::string title, std::string id, ImGuiWindowFlags flags) :
        mTitle(std::move(title)),
        mId(std::move(id)),
        mLabel(mTitle + "###" + mId),
        mFlags(flags)
    {
    }

    void EditorWindow::draw()
    {
        if (!mOpen)
        {
            // 隐藏这一帧不会走到下面 Begin()/IsWindowFocused() 那一段, mFocused/mHovered
            // 必须在这里显式清掉——不然会冻结在隐藏前最后一次为 true 的值上, 干扰
            // MainEditorTab::getUndoStack() 的线性扫描(它找到第一个 isFocused() 为 true 的
            // 窗口就直接返回, 不会再检查这个焦点是不是过期的)。
            mFocused = false;
            mHovered = false;
            return;
        }
        if (mFocusRequested)
        {
            ImGui::SetWindowFocus(mLabel.c_str());
            mFocusRequested = false;
        }

        if (mWindowPadding)
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, *mWindowPadding);
        bool visible = ImGui::Begin(mLabel.c_str(), mShowCloseButton ? &mOpen : nullptr, mFlags);
        if (mWindowPadding)
            ImGui::PopStyleVar();

        // ImGuiFocusedFlags_ChildWindows 不能省: Hierarchy 这类窗口内部用 BeginTable(..,
        // ImGuiTableFlags_ScrollY, ..)/BeginChild 建滚动区域时, ImGui 会为那块区域单独开一个
        // 子窗口——点击树里的某一行, 焦点实际落在这个子窗口上, 不带这个 flag 的话外层窗口的
        // IsWindowFocused() 永远是 false, isFocused() 也就永远是 false。
        mFocused = visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
        mHovered = visible && ImGui::IsWindowHovered();
        if (visible)
            onDraw();
        ImGui::End();
    }

    void EditorWindow::setTitle(std::string title)
    {
        mTitle = std::move(title);
        rebuildLabel();
    }

    void EditorWindow::setIcon(const char* icon)
    {
        mIcon = icon;
        rebuildLabel();
    }

    void EditorWindow::rebuildLabel()
    {
        mLabel = (mIcon.empty() ? mTitle : mIcon + " " + mTitle) + "###" + mId;
    }
}
