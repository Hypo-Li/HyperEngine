#include "EditorMenuBar.h"
#include "EditorTabManager.h"
#include "MainEditorTab.h"
#include <imgui.h>

namespace hyp::ed
{
    EditorMenuBar::EditorMenuBar(EditorTabManager& tabManager) :
        mTabManager(tabManager)
    {
    }

    void EditorMenuBar::drawMenus()
    {
        if (ImGui::BeginMenu("File"))
        {
            drawFileMenu();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window"))
        {
            drawWindowMenu();
            ImGui::EndMenu();
        }
    }

    void EditorMenuBar::drawFileMenu()
    {
        if (ImGui::MenuItem("New")) {}
        if (ImGui::MenuItem("Open..", "Ctrl+O")) {}
        if (ImGui::BeginMenu("Open Recent"))
        {
            if (ImGui::MenuItem("test.syzproj")) {}
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) {}
        if (ImGui::MenuItem("Save As..", "Shift+Ctrl+S")) {}
    }

    void EditorMenuBar::drawWindowMenu()
    {
        mTabManager.getMainTab().drawWindowMenu();
    }
}
