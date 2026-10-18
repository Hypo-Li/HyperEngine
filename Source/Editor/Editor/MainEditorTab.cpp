#include "MainEditorTab.h"
#include "AssetBrowser.h"
#include "Inspector.h"
#include "Hierarchy.h"
#include "SceneView.h"
#include "LogWindow.h"
#include "PrefabTestWindow.h"

#include <imgui.h>

namespace hyp::ed
{
    MainEditorTab::MainEditorTab(Editor& editor, Engine& engine, EditorSelection& selection, UndoStack& undoStack,
        ThumbnailProvider& iconProvider, AssetEditorTabHost& tabHost) :
        EditorTab("Main"),
        mToolbar(editor),
        // AssetBrowser 要先于 Inspector/PrefabTestWindow 构造: 它们会把它转发给各个
        // ComponentPanel(见 AssetComboEd), 需要一个已经存在的实例可传。
        mAssetBrowser(std::make_unique<AssetBrowser>(iconProvider, tabHost)),
        mInspector(std::make_unique<Inspector>(selection, undoStack, iconProvider, *mAssetBrowser)),
        mHierarchy(std::make_unique<Hierarchy>(engine, selection, undoStack)),
        mSceneView(std::make_unique<SceneView>(engine, selection, undoStack)),
        mLogWindow(std::make_unique<LogWindow>()),
        mPrefabTestWindow(std::make_unique<PrefabTestWindow>(engine, selection, iconProvider, *mAssetBrowser))
    {
        mWindows = { mAssetBrowser.get(), mInspector.get(), mHierarchy.get(), mSceneView.get(), mLogWindow.get(), mPrefabTestWindow.get() };

        // 只有这 6 个常驻窗口需要"关闭=隐藏, 能从 Window 菜单重新打开"这套机制, 见
        // EditorWindow::setShowCloseButton() 的说明。
        for (EditorWindow* window : mWindows)
            window->setShowCloseButton(true);
    }

    MainEditorTab::~MainEditorTab() = default;

    UndoStack* MainEditorTab::getUndoStack() noexcept
    {
        for (EditorWindow* window : mWindows)
            if (window->isFocused())
                return window->getUndoStack();
        return nullptr;
    }

    void MainEditorTab::drawWindowMenu()
    {
        for (EditorWindow* window : mWindows)
        {
            bool open = window->isOpen();
            if (ImGui::MenuItem(window->getTitle().c_str(), nullptr, &open))
                window->setOpen(open);
        }
    }

    float MainEditorTab::drawExtraChrome(float topReserved)
    {
        return mToolbar.draw(topReserved);
    }

    void MainEditorTab::drawWindows()
    {
        for (EditorWindow* window : mWindows)
            window->draw();
    }

    void MainEditorTab::collectPrepareTextures(std::vector<TextureProxyHandle>& out)
    {
        for (EditorWindow* window : mWindows)
            window->collectPrepareTextures(out);
    }
}
