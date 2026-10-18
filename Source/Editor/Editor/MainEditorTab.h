#pragma once
#include "EditorTab.h"
#include "EditorToolbar.h"

#include <memory>
#include <vector>

namespace hyp
{
    class Engine;
}

namespace hyp::ed
{
    class Editor;
    class EditorSelection;
    class UndoStack;
    class ThumbnailProvider;
    class AssetEditorTabHost;
    class EditorWindow;
    class AssetBrowser;
    class Inspector;
    class Hierarchy;
    class SceneView;
    class LogWindow;
    class PrefabTestWindow;

    // 常驻、不可关闭的主标签页——直接具名持有 SceneView/Hierarchy/Inspector/AssetBrowser/
    // LogWindow/PrefabTestWindow 这 6 个窗口, 不再通过任何共享的 window manager; mWindows
    // 是一份不持有所有权的列表, 纯粹为了 drawWindows()/getUndoStack()/collectPrepareTextures()/
    // drawWindowMenu() 这几个需要遍历全部窗口的操作准备的。播放控制工具栏也在这——它是唯一
    // 需要图标字体、放不进菜单栏那一行的常驻 UI, 只在这个 tab 激活时才占一行。
    class MainEditorTab : public EditorTab
    {
    public:
        MainEditorTab(Editor& editor, Engine& engine, EditorSelection& selection, UndoStack& undoStack,
            ThumbnailProvider& iconProvider, AssetEditorTabHost& tabHost);
        ~MainEditorTab() override;

        std::string getTitle() const override { return "Main"; }
        UndoStack* getUndoStack() noexcept override;

        // EditorMenuBar 的 Window 菜单调这个, 逐个列出 6 个窗口, 勾选控制显示/隐藏。
        void drawWindowMenu();

        SceneView* getSceneView() noexcept { return mSceneView.get(); }

        // 声明成 public 是有意的: EditorTabManager 通过 unique_ptr<MainEditorTab>(具体类型,
        // 不是基类指针)直接调用这个, 用 protected 会在那个调用点编译不过——跟 drawWindows()/
        // buildDefaultLayout() 不一样, 那两个只在 EditorTab::draw() 内部通过 this 调用,
        // 从不经由外部持有的具体类型指针。
        void collectPrepareTextures(std::vector<TextureProxyHandle>& out) override;

    protected:
        float drawExtraChrome(float topReserved) override;
        void drawWindows() override;

    private:
        EditorToolbar mToolbar;

        std::unique_ptr<AssetBrowser> mAssetBrowser;
        std::unique_ptr<Inspector> mInspector;
        std::unique_ptr<Hierarchy> mHierarchy;
        std::unique_ptr<SceneView> mSceneView;
        std::unique_ptr<LogWindow> mLogWindow;
        std::unique_ptr<PrefabTestWindow> mPrefabTestWindow;

        // 不持有所有权, 构造完那 6 个窗口之后填好——drawWindows()/getUndoStack()/
        // collectPrepareTextures()/drawWindowMenu() 遍历的就是这份列表。
        std::vector<EditorWindow*> mWindows;
    };
}
