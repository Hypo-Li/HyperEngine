#include "EditorTabBar.h"
#include "EditorTabManager.h"
#include "EditorTab.h"
#include "MainEditorTab.h"
#include "AssetEditorTab.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include <vector>

namespace hyp::ed
{
    EditorTabBar::EditorTabBar(EditorTabManager& tabManager) :
        mTabManager(tabManager)
    {
    }

    void EditorTabBar::draw()
    {
        if (!ImGui::BeginTabBar("##EditorTabBar", ImGuiTabBarFlags_Reorderable))
            return;

        // ReorderRequestTabId 是上一帧拖拽发生时记下、一直留到这一帧开头的——必须在画任何
        // tab item 之前读, 因为真正应用这次换序(TabBarLayout(), 见下面的注释)会顺手把它
        // 清零, 到那之后就再也查不出"这一帧是不是刚发生过拖拽"了。
        ImGuiTabBar* tabBarBeforeLayout = ImGui::GetCurrentTabBar();
        bool reorderPending = tabBarBeforeLayout && tabBarBeforeLayout->ReorderRequestTabId != 0;

        mTabIdMap.clear();
        drawTabItem(mTabManager.getMainTab(), nullptr);
        for (auto& tab : mTabManager.getAssetTabs())
            drawTabItem(*tab, tab.get());

        // TabItemEx() 只在这一帧第一次被调用时才真正跑一遍 TabBarLayout()(imgui_widgets.cpp
        // 里 if (tab_bar->WantLayout) TabBarLayout(tab_bar);), 拖拽产生的换序请求正是在这一步
        // 才真正被应用进 tabBar->Tabs[]——不是在 BeginTabBar() 返回的那一刻(之前在这里就直接
        // 读 Tabs[] 是错的, 那时候还没被 TabBarLayout() 处理过, 读到的是拖拽前的旧顺序)。
        // 上面已经画完了这一帧所有 tab item, 保证至少触发过一次 TabItemEx, 现在读到的 Tabs[]
        // 才是应用过拖拽之后的最新顺序; mTabIdMap 也是刚画完这一帧重新建的。只在 reorderPending
        // 时才做这次同步——没有拖拽发生的绝大多数帧不需要付这份开销。
        if (reorderPending)
            syncAssetTabOrderFromImGui();

        ImGui::EndTabBar();

        // OpenPopup()/BeginPopupModal() 必须在同一个 ID 栈状态下调用才能对上同一个弹窗——
        // BeginTabBar()/EndTabBar() 内部会临时压一层 tab_bar->ID 进 ID 栈(imgui_widgets.cpp
        // 里 PushOverrideID(tab_bar->ID)), 之前在 drawTabItem() 里(还在 BeginTabBar()/
        // EndTabBar() 内部)直接调 OpenPopup() 算出来的 ID, 跟这里 EndTabBar() 之后再调
        // BeginPopupModal() 算出来的 ID 对不上, 弹窗永远不会真的出现。现在只在这里(唯一
        // 调用点, 跟 drawCloseConfirmPopup() 里的 BeginPopupModal() 完全同一个位置)触发。
        if (mCloseConfirmRequested)
        {
            ImGui::OpenPopup("Unapplied Changes###ConfirmCloseTab");
            mCloseConfirmRequested = false;
        }
        drawCloseConfirmPopup();
    }

    void EditorTabBar::drawTabItem(EditorTab& tab, AssetEditorTab* assetTab)
    {
        ImGuiTabItemFlags flags = tab.consumeFocusRequest() ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        // 不可关闭的 tab 目前只有 Main 一个, 固定在第一位不参与拖拽换序; 各个资产 tab
        // 之间可以自由换序。
        if (!tab.isClosable())
            flags |= ImGuiTabItemFlags_NoReorder;
        // 标题(资产名字, 可能重名)只用来显示——ImGui 拿来做 tab 身份区分的是 ###后面这段,
        // 用 tabId(资产 Guid)保证重名资产不会被当成同一个 tab item。
        std::string label = tab.getTitle() + "###" + tab.getTabId();
        bool open = true;
        bool selected = ImGui::BeginTabItem(label.c_str(), tab.isClosable() ? &open : nullptr, flags);
        // GetItemID() 拿的是 ImGui 刚刚给这一项分配的真实 ID(不管这一项这一帧是不是选中态),
        // 见 mTabIdMap 的注释——不重新用 GetID() 算一遍。
        if (assetTab)
            mTabIdMap[ImGui::GetItemID()] = assetTab;
        if (selected)
        {
            // 关闭确认框弹出期间不跟随 ImGui 自己的选中态切换活动 tab——ImGui 的 tab bar
            // 内部假定 p_open 变 false 之后这一项下一帧就真的消失了, 会提前把选中态挪到别的
            // tab(实测挪到了 Main); 我们这边是"弹框问过用户、按了确认才真的关掉", 在用户
            // 做出选择之前不能让画面已经跳走。见 drawCloseConfirmPopup() 里取消时
            // requestFocus() 的说明。
            if (!mPendingCloseConfirmTab)
                mTabManager.setActiveTab(&tab);
            ImGui::EndTabItem();
        }
        if (tab.isClosable() && !open)
        {
            if (tab.hasUnsavedChanges())
            {
                mPendingCloseConfirmTab = &tab;
                mCloseConfirmRequested = true;
            }
            else
            {
                tab.requestCloseContents();
            }
        }
    }

    void EditorTabBar::drawCloseConfirmPopup()
    {
        if (!ImGui::BeginPopupModal("Unapplied Changes###ConfirmCloseTab", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        if (mPendingCloseConfirmTab)
            ImGui::Text("\"%s\" has unapplied changes. Apply before closing?", mPendingCloseConfirmTab->getTitle().c_str());

        if (ImGui::Button("Apply"))
        {
            if (mPendingCloseConfirmTab)
            {
                mPendingCloseConfirmTab->applyPendingChanges();
                mPendingCloseConfirmTab->requestCloseContents();
            }
            mPendingCloseConfirmTab = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            if (mPendingCloseConfirmTab)
                mPendingCloseConfirmTab->requestCloseContents();
            mPendingCloseConfirmTab = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            // 弹框期间冻结了活动 tab 切换(见 drawTabItem() 的说明), 取消时显式请求把焦点
            // 挪回这个 tab——不然 ImGui 内部可能已经因为 p_open 变过 false 而把选中态挪到
            // 别的 tab 了, 用户点了"取消"却发现画面还是停在别的 tab 上。
            if (mPendingCloseConfirmTab)
                mPendingCloseConfirmTab->requestFocus();
            mPendingCloseConfirmTab = nullptr;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorTabBar::syncAssetTabOrderFromImGui()
    {
        ImGuiTabBar* tabBar = ImGui::GetCurrentTabBar();
        if (!tabBar)
            return;

        // mTabIdMap 是上一帧画 tab item 时 ImGui::GetItemID() 当场给出的真实 ID, tabBar->Tabs[]
        // 是这一帧 BeginTabBar() 已经应用了拖拽之后的新顺序——两者直接对照, 不需要重新计算
        // 任何 ID。
        std::vector<AssetEditorTab*> order;
        order.reserve(static_cast<size_t>(tabBar->Tabs.Size));
        for (int i = 0; i < tabBar->Tabs.Size; ++i)
        {
            auto it = mTabIdMap.find(tabBar->Tabs[i].ID);
            if (it != mTabIdMap.end())
                order.push_back(it->second);
        }

        mTabManager.syncAssetTabOrder(order);
    }
}
