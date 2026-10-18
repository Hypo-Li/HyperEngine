#include "EditorTabManager.h"
#include "MainEditorTab.h"
#include "AssetEditorTab.h"

#include <algorithm>
#include <vector>

namespace hyp::ed
{
    EditorTabManager::EditorTabManager(Editor& editor, Engine& engine, EditorSelection& selection, UndoStack& undoStack, ThumbnailProvider& iconProvider) :
        mMainTab(std::make_unique<MainEditorTab>(editor, engine, selection, undoStack, iconProvider, *this)),
        mActiveTab(mMainTab.get())
    {
    }

    EditorTabManager::~EditorTabManager() = default;

    void EditorTabManager::draw(float topReserved)
    {
        mActiveTab->draw(topReserved);

        // 资产 tab 唯一的内容窗口不再有自己独立的关闭入口(见 EditorWindow::
        // setShowCloseButton()), 关闭只可能通过 requestCloseContents() 标记, 这里统一收尾。
        std::erase_if(mAssetTabs, [this](const std::unique_ptr<AssetEditorTab>& tab)
        {
            bool shouldClose = tab->isCloseRequested();
            if (shouldClose && mActiveTab == tab.get())
                mActiveTab = mMainTab.get();
            return shouldClose;
        });
    }

    UndoStack* EditorTabManager::getFocusedUndoStack() noexcept
    {
        return mActiveTab->getUndoStack();
    }

    void EditorTabManager::collectPrepareTextures(std::vector<TextureProxyHandle>& out)
    {
        mMainTab->collectPrepareTextures(out);
        for (auto& tab : mAssetTabs)
            tab->collectPrepareTextures(out);
    }

    void EditorTabManager::clear()
    {
        mAssetTabs.clear();
        mMainTab = nullptr;
        mActiveTab = nullptr;
    }

    AssetEditorTab* EditorTabManager::findAssetTab(Guid guid)
    {
        for (auto& tab : mAssetTabs)
            if (tab->getTargetGuid() == guid)
                return tab.get();
        return nullptr;
    }

    void EditorTabManager::addAssetTab(std::unique_ptr<AssetEditorTab> tab)
    {
        mAssetTabs.push_back(std::move(tab));
    }

    void EditorTabManager::activateMainTab()
    {
        // 跟 findAssetTab()/工厂新建 tab 时一样, 不直接改 mActiveTab——那样下一帧
        // EditorTabBar 按 ImGui 自己记的"当前选中项"重新同步一次就会被打回原状。
        // 走 requestFocus()/consumeFocusRequest() 这套, 让 ImGui 的 tab bar 也知道要
        // 切过来, 见 EditorTabBar::drawTabItem()。
        mMainTab->requestFocus();
    }

    void EditorTabManager::activateNextTab()
    {
        activateAdjacentTab(1);
    }

    void EditorTabManager::activatePreviousTab()
    {
        activateAdjacentTab(-1);
    }

    void EditorTabManager::activateAdjacentTab(int direction)
    {
        std::vector<EditorTab*> order;
        order.reserve(mAssetTabs.size() + 1);
        order.push_back(mMainTab.get());
        for (auto& tab : mAssetTabs)
            order.push_back(tab.get());

        auto it = std::find(order.begin(), order.end(), mActiveTab);
        if (it == order.end() || order.size() < 2)
            return;

        size_t index = static_cast<size_t>(it - order.begin());
        size_t count = order.size();
        size_t nextIndex = direction > 0 ? (index + 1) % count : (index + count - 1) % count;
        order[nextIndex]->requestFocus();
    }

    void EditorTabManager::syncAssetTabOrder(const std::vector<AssetEditorTab*>& order)
    {
        // 大小对不上说明数据已经不新鲜(比如拖拽发生的同一帧资产 tab 列表也变了)——宁可
        // 跳过这次同步也不要把 mAssetTabs 搞乱, 下次拖拽还会再触发一次同步。
        if (order.size() != mAssetTabs.size())
            return;

        std::vector<std::unique_ptr<AssetEditorTab>> reordered;
        reordered.reserve(mAssetTabs.size());
        for (AssetEditorTab* tab : order)
        {
            auto it = std::find_if(mAssetTabs.begin(), mAssetTabs.end(),
                [tab](const std::unique_ptr<AssetEditorTab>& owned) { return owned.get() == tab; });
            if (it == mAssetTabs.end())
                return;
            reordered.push_back(std::move(*it));
        }
        mAssetTabs = std::move(reordered);
    }
}
