#include "EditorToolbar.h"
#include "Editor.h"
#include <HypImGui/ImGuiWidgets.h>
#include <HypImGui/IconsMaterialSymbols.h>
#include <imgui.h>

namespace hyp::ed
{
    EditorToolbar::EditorToolbar(Editor& editor) :
        mEditor(editor)
    {
    }

    float EditorToolbar::draw(float topReserved)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiStyle& style = ImGui::GetStyle();

        // 图标字形是按 24px 烤的(正文字体是 16px), 比 GetFrameHeight() 假设的单行文字高——
        // 需要额外留白让图标不被裁切。用 FramePadding(跟 ImGuiPropertyWidgets.cpp 里图标
        // 按钮宽度的算法同一套余量)而不是更大的默认 WindowPadding, 尽量把行高压紧——下面
        // Begin() 前必须把 WindowPadding 也 Push 成同一个值, 否则窗口实际内边距还是默认值,
        // 内容比这里算出来的 rowHeight 还高, 会溢出到窗口自己的边界外面。
        float rowHeight = ImGui::GetFrameHeight() + style.FramePadding.y * 2.0f;

        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + topReserved));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, rowHeight));
        ImGui::SetNextWindowViewport(viewport->ID);

        // style.WindowMinSize 默认 (32,32)(按 DPI 缩放后更大), 比这里算出来的 rowHeight
        // 还高——不清掉的话 ImGui 会把窗口实际高度悄悄拉到 WindowMinSize.y, 跟返回给调用方
        // 的 rowHeight 对不上, DockSpace 按 rowHeight 让出的位置就不够, 被真实更高的窗口盖住
        // 一截。BeginMainMenuBar 内部(BeginViewportSideBar)对同类细长条形窗口做的是同一件事。
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                                | ImGuiWindowFlags_NoResize
                                | ImGuiWindowFlags_NoMove
                                | ImGuiWindowFlags_NoScrollbar
                                | ImGuiWindowFlags_NoScrollWithMouse
                                | ImGuiWindowFlags_NoCollapse
                                | ImGuiWindowFlags_NoSavedSettings
                                | ImGuiWindowFlags_NoDocking
                                | ImGuiWindowFlags_NoBringToFrontOnFocus
                                | ImGuiWindowFlags_NoNavFocus;

        ImGui::Begin("##Toolbar", nullptr, flags);
        ImGui::PopStyleVar(2);

        // 按钮本身按正文字体单行高度算(TextOnlyButton 内部用 CalcTextSize), 比 rowHeight
        // 矮一圈 WindowPadding, 手动把光标下移半个差值让它在这一整行工具栏里垂直居中。
        float itemHeight = ImGui::GetFrameHeight();
        float availHeight = ImGui::GetContentRegionAvail().y;
        if (availHeight > itemHeight)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (availHeight - itemHeight) * 0.5f);

        PlayState playState = mEditor.getPlayState();

        // 只在 Stoped 时能按——Playing/Paused 期间挂的是临时 Play 场景, 不是磁盘上那份编辑场景。
        ImGui::BeginDisabled(playState != PlayState::Stoped);
        if (TextOnlyButton(ICON_MS_SAVE))
            mEditor.saveCurrentScene();
        ImGui::EndDisabled();

        ImGui::SameLine();

        // 第一个按钮在 Play/Pause/Resume 之间循环: Stoped 时按下去开始播放, Playing 时
        // 按下去暂停, Paused 时按下去恢复(回到 Playing, 图标随之变回 Pause)。第二个是单步,
        // 只在 Paused 时能按。第三个是独立的 Stop, 只要不是 Stoped(Playing 或 Paused)就能按。
        switch (playState)
        {
        case PlayState::Stoped:
            if (TextOnlyButton(ICON_MS_PLAY_ARROW))
                mEditor.play();
            break;
        case PlayState::Playing:
            if (TextOnlyButton(ICON_MS_PAUSE))
                mEditor.pause();
            break;
        case PlayState::Paused:
            if (TextOnlyButton(ICON_MS_RESUME))
                mEditor.resume();
            break;
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(playState != PlayState::Paused);
        if (TextOnlyButton(ICON_MS_SKIP_NEXT))
            mEditor.stepFrame();
        ImGui::EndDisabled();

        ImGui::SameLine();

        ImGui::BeginDisabled(playState == PlayState::Stoped);
        if (TextOnlyButton(ICON_MS_STOP))
            mEditor.stop();
        ImGui::EndDisabled();

        ImGui::End();
        return rowHeight;
    }
}
