#include "DockSpace.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace hyp::ed
{
    DockSpace::DockSpace()
    {
    }

    DockSpace::~DockSpace()
    {
    }

    bool DockSpace::beginHost(float topReserved, const std::string& id, const std::function<void(ImGuiID)>& buildDefaultLayout)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (!(io.ConfigFlags & ImGuiConfigFlags_DockingEnable))
            return false;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();

        // 用 viewport->Pos/Size(平台窗口的原始范围), 不用 WorkPos/WorkSize——后者只在
        // BeginMainMenuBar/BeginViewportSideBar 参与的自动累计机制下才可靠, EditorToolbar
        // 走的是普通 Begin(), 不参与那套机制, 混着用会导致这里的停靠区域和上面的工具栏重叠。
        // topReserved 由调用方按 EditorMenuBar/EditorToolbar 实际占用的高度加总传入。
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + topReserved));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - topReserved));
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking
                                      | ImGuiWindowFlags_NoTitleBar
                                      | ImGuiWindowFlags_NoCollapse
                                      | ImGuiWindowFlags_NoResize
                                      | ImGuiWindowFlags_NoMove
                                      | ImGuiWindowFlags_NoBringToFrontOnFocus
                                      | ImGuiWindowFlags_NoNavFocus;

        std::string windowName = "DockSpaceWindow##" + id;
        ImGui::Begin(windowName.c_str(), nullptr, window_flags);
        ImGui::PopStyleVar(3);
        ImGuiID dockspace_id = ImGui::GetID("DockSpace");

        // 第一次创建这个 dockspace 时(ini 里没有它的记录, DockBuilderGetNode 查不到对应
        // 节点)才走这条分支, 让调用方摆一次默认布局——用户以后手动拖拽调整过, ini 就有
        // 记录了, DockBuilderGetNode 能查到, 这段不会再执行, 原样保留用户的布局。跟
        // imgui_demo.cpp 里 ShowExampleAppDockSpace() 是同一个写法。
        if (buildDefaultLayout && ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
        {
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_None);
            buildDefaultLayout(dockspace_id);
            ImGui::DockBuilderFinish(dockspace_id);
        }

        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f));
        return true;
    }

    void DockSpace::endHost()
    {
        ImGui::End();
    }

    void DockSpace::draw(float topReserved, const std::string& id, const std::function<void(ImGuiID)>& buildDefaultLayout)
    {
        if (!beginHost(topReserved, id, buildDefaultLayout))
            return;
        endHost();
    }

}
