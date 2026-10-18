#pragma once
#include <imgui.h>

#include <unordered_map>

namespace hyp::ed
{
    class EditorTabManager;
    class EditorTab;
    class AssetEditorTab;

    // 顶部标签条——跟 EditorMenuBar 共享同一行(同一对 BeginMainMenuBar/EndMainMenuBar),
    // 由 Editor::run() 负责把两者包在一起画, 这里只画 TabBar 内容, 不自己开菜单栏窗口。
    class EditorTabBar
    {
    public:
        explicit EditorTabBar(EditorTabManager& tabManager);

        void draw();

    private:
        // assetTab 非空时(Main tab 传 nullptr)顺便把 ImGui::GetItemID() 刚分配给这一项的
        // 真实 ID 记进 mTabIdMap——见该成员的注释, 不重新计算 ID, 避免跟 TabBarCalcTabID()
        // 内部算法细节脱钩。
        void drawTabItem(EditorTab& tab, AssetEditorTab* assetTab);

        // 把 ImGui tab bar 内部当前的显示顺序读出来, 同步进 EditorTabManager::mAssetTabs——
        // 每帧画完所有 tab item 之后都调一次, 见 draw() 里的说明。
        void syncAssetTabOrderFromImGui();

        // 点 × 关闭一个 hasUnsavedChanges() 为 true 的 tab 时调用——不直接关, 弹一个确认框,
        // 真正的 requestCloseContents() 挪到用户在框里点"关闭"之后。
        void drawCloseConfirmPopup();

        EditorTabManager& mTabManager;

        // ImGui 的 ReorderRequestTabId 这一帧非零, 说明用户刚拖了一个 tab 越过邻居——但
        // 真正把 Tabs[] 数组换成新顺序要等到下一帧 BeginTabBar() 开头才发生(见
        // imgui_widgets.cpp 里 TabBarLayout 的实现), 所以这里只记一个标记, 真正读顺序、
        // 同步进 mAssetTabs 放到下一帧开头做, 平时没人拖拽的每一帧不用做这份工作。
        // 这一帧画每个资产 tab item 时, ImGui::GetItemID() 当场给出的真实 ID——不是重新用
        // GetID()/哈希算出来的, 避免依赖 TabBarCalcTabID() 内部算法细节跟外部重算是否
        // 完全等价这个脆弱假设(实测踩过: 重算出来的 ID 对不上, 拖拽换序后 mAssetTabs 底层
        // 顺序完全没同步, 只是视觉上看着换了)。
        std::unordered_map<ImGuiID, AssetEditorTab*> mTabIdMap;

        // 非空表示正在等用户确认要不要关掉这个 tab(它有未保存的修改)——弹窗按钮点了
        // "关闭"才会对它调 requestCloseContents(), 点"取消"或者关掉弹窗就清成 nullptr,
        // 不做任何事。
        EditorTab* mPendingCloseConfirmTab = nullptr;

        // drawTabItem() 里检测到需要弹确认框时只置这个标记——真正调 OpenPopup() 挪到
        // draw() 里 EndTabBar() 之后, 见那边的注释(ID 栈状态必须跟 BeginPopupModal() 一致)。
        bool mCloseConfirmRequested = false;
    };
}
