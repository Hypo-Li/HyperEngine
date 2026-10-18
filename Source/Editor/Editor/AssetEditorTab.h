#pragma once
#include "EditorTab.h"
#include "UndoStack.h"

#include <Core/Common/Guid.h>

namespace hyp
{
    class Asset;
}

namespace hyp::ed
{
    // 每个打开的资产对应一个可关闭的标签页——标题带脏标记星号, 用资产 Guid 当标签页 id
    // (也是它 DockSpace 宿主窗口名的后缀)。内部子面板(具体有几个、是谁, 由派生类决定,
    // 见 TextureEditorTab 等)共享这一份 UndoStack, 不像 MainEditorTab 那样按聚焦的子窗口
    // 各自独立。
    //
    // 关闭走标签条自己的按钮(requestCloseContents() 只是标记一下), 真正从列表里摘除由
    // EditorTabManager 每帧检查 isCloseRequested() 来做——内容窗口不再有自己独立的关闭
    // 入口, 见 EditorWindow::setShowCloseButton() 的说明。
    class AssetEditorTab : public EditorTab
    {
    public:
        explicit AssetEditorTab(Asset& asset);

        std::string getTitle() const override;
        bool isClosable() const noexcept override { return true; }
        UndoStack* getUndoStack() noexcept override { return &mUndoStack; }
        void requestCloseContents() override { mCloseRequested = true; }

        Guid getTargetGuid() const noexcept;
        bool isCloseRequested() const noexcept { return mCloseRequested; }

    protected:
        Asset& getAsset() const noexcept { return mAsset; }

        // 派生类构造自己的内容窗口时把这个传进去, 让内容窗口跟这个 tab 共享同一份历史。
        UndoStack& sharedUndoStack() noexcept { return mUndoStack; }

    private:
        Asset& mAsset;
        UndoStack mUndoStack;
        bool mCloseRequested = false;
    };
}
