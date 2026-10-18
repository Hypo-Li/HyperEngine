#pragma once
#include "DockSpace.h"

#include <RenderBase/ProxyHandle.h>

#include <string>
#include <utility>
#include <vector>

namespace hyp::ed
{
    class UndoStack;

    // 顶部标签条上的一页——自己持有一份独立的 DockSpace(用 getTabId() 区分宿主窗口名,
    // 不会跟其它 tab 的停靠布局互相干扰)。不持有任何共享的窗口注册表: 具体有哪些窗口、
    // 怎么画、默认布局长什么样, 完全由子类自己知道并实现(见 MainEditorTab/AssetEditorTab
    // 及其派生类)。
    class EditorTab
    {
    public:
        explicit EditorTab(std::string tabId);
        virtual ~EditorTab() = default;

        void draw(float topReserved);

        virtual std::string getTitle() const = 0;
        virtual bool isClosable() const noexcept { return false; }
        virtual UndoStack* getUndoStack() noexcept { return nullptr; }

        // 标签条点击关闭按钮时调用——不可关闭的 tab(isClosable() 为 false)不需要覆写。
        virtual void requestCloseContents() {}

        // 关闭前要不要先弹确认框——默认 false。只有"改动还没 Apply、关闭就会被 UI 缓冲区
        // 直接丢掉"这种情况才需要覆写它, 比如 MaterialTemplateEditorTab; 仅仅是"Apply 过了
        // 但还没存盘"(Asset::isDirty())不算——那份改动已经在活的 Asset 对象里, 关闭这个 tab
        // 不会丢, AssetBrowser 网格上的脏标记星号足够提醒用户还没存盘, 不需要在关闭时额外
        // 打断一次。见 EditorTabBar::drawTabItem()。
        virtual bool hasUnsavedChanges() const { return false; }

        // hasUnsavedChanges() 为真时, 关闭确认框的"Apply"按钮调用——把还留在 UI 缓冲区里的
        // 改动写进活的 Asset 对象(不等于存盘), 默认什么都不做。
        virtual void applyPendingChanges() {}

        virtual void collectPrepareTextures(std::vector<TextureProxyHandle>& out) {}

        const std::string& getTabId() const noexcept { return mTabId; }

        void requestFocus() noexcept { mFocusRequested = true; }

        // EditorTabBar 用: 读一次并清掉标记, 决定这一帧要不要让 ImGui 把这个 tab 强制设为
        // 当前选中项。
        bool consumeFocusRequest() noexcept
        {
            bool requested = mFocusRequested;
            mFocusRequested = false;
            return requested;
        }

    protected:
        // 子类在自己的 dockspace 之上再画一行额外的常驻 UI(比如 MainEditorTab 的播放控制
        // 工具栏), 返回额外占用的高度, 累加进 dockspace 的 topReserved。默认没有。
        virtual float drawExtraChrome(float topReserved) { return 0.0f; }

        // dockspace 第一次创建、还没有已保存布局时调用一次, 摆好这个 tab 的默认布局——
        // 具体往 dockspaceId 里塞什么窗口、怎么分栏, 由子类硬编码, 见 DockSpace::draw()。
        // 默认什么都不做。
        virtual void buildDefaultLayout(ImGuiID dockspaceId) {}

        // 画这个 tab 名下的所有窗口——子类自己知道有哪些、怎么画。
        virtual void drawWindows() = 0;

    private:
        std::string mTabId;
        DockSpace mDockSpace;
        bool mFocusRequested = false;
    };
}
