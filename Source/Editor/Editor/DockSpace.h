#pragma once
#include <imgui.h>

#include <functional>
#include <string>

namespace hyp::ed
{
    // EditorMenuBar/EditorToolbar 都钉在视口顶部、自己独立开窗绘制, 不归这个类管——这里只负责
    // 把停靠区域摆在它们下面。每个 EditorTab 持有自己的一份实例, 靠宿主窗口名里的 id 后缀
    // 区分, 停靠布局互不干扰(见 EditorTab)。
    class DockSpace
    {
    public:
        DockSpace();

        ~DockSpace();

        // topReserved: 视口顶部已经被 EditorMenuBar/EditorToolbar 占掉的高度(像素, 已按 DPI
        // 缩放), 调用方按自己摆的行数加总传入——这里只管照这个值摆自己的位置, 不关心上面
        // 具体是谁、占了几行。id 是这份 dockspace 的唯一标识(比如所属 tab 的 tabId), 拼进
        // 宿主窗口名里, 让不同 tab 的 dockspace 各自独立。
        //
        // buildDefaultLayout: 这份 dockspace 第一次创建、还没有任何已保存布局(ini 里没有
        // 这个 id 对应的记录)时调用一次, 传入这份 dockspace 的 ImGuiID——具体往里面摆什么
        // (DockBuilderDockWindow 占满, 还是 DockBuilderSplitNode 分栏)完全由调用方决定,
        // 这里不关心。用户手动拖拽调整过之后 ini 里就有记录了, 以后这个回调不会再被调用。
        // 空回调表示这份 dockspace 不需要这套"首次建默认布局"的行为。
        void draw(float topReserved, const std::string& id, const std::function<void(ImGuiID)>& buildDefaultLayout);

    private:
        bool beginHost(float topReserved, const std::string& id, const std::function<void(ImGuiID)>& buildDefaultLayout);
        void endHost();
    };
}
