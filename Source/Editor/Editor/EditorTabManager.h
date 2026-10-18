#pragma once
#include "AssetEditorTabHost.h"

#include <RenderBase/ProxyHandle.h>

#include <memory>
#include <vector>

namespace hyp
{
    class Engine;
}

namespace hyp::ed
{
    class Editor;
    class EditorTab;
    class MainEditorTab;
    class AssetEditorTab;
    class EditorSelection;
    class UndoStack;
    class ThumbnailProvider;

    // 持有"tab 从哪来、谁是激活的"这一整套状态——从 Editor 里搬出来, Editor 只转发给它。
    // 实现 AssetEditorTabHost: AssetBrowser 双击资产时真正打开/聚焦 tab 就发生在这里。
    class EditorTabManager : public AssetEditorTabHost
    {
    public:
        EditorTabManager(Editor& editor, Engine& engine, EditorSelection& selection, UndoStack& undoStack, ThumbnailProvider& iconProvider);
        ~EditorTabManager() override;

        // 画当前激活的 tab, 并顺手把这一帧被标记关闭的资产 tab 摘掉, 见 AssetEditorTab::
        // isCloseRequested()。
        void draw(float topReserved);

        UndoStack* getFocusedUndoStack() noexcept;
        void collectPrepareTextures(std::vector<TextureProxyHandle>& out);

        // 渲染线程真正停下之前必须显式调用: SceneView 这类窗口持有的 RenderTarget/相机
        // proxy 在析构时会向渲染线程排一个清理任务, 如果等到这些 tab 作为 Editor 的普通
        // 成员、在渲染线程已经停下之后才被析构, 这些清理任务永远不会被消费, 底层 GPU 资源
        // 就泄漏了。
        void clear();

        MainEditorTab& getMainTab() noexcept { return *mMainTab; }
        const std::vector<std::unique_ptr<AssetEditorTab>>& getAssetTabs() const noexcept { return mAssetTabs; }
        EditorTab* getActiveTab() const noexcept { return mActiveTab; }
        void setActiveTab(EditorTab* tab) noexcept { mActiveTab = tab; }

        // GlobalShortcuts 的 Ctrl+Q/Ctrl+E 用。按 [mMainTab, ...mAssetTabs] 现算前后一个——
        // mAssetTabs 本身就是标签条上的显示顺序, 见 syncAssetTabOrder()。
        void activateNextTab();
        void activatePreviousTab();

        // EditorTabBar 检测到用户刚拖拽调整过标签条顺序时调用一次, 把 mAssetTabs 里的
        // unique_ptr 重新排列成跟 order 一致——之后 mAssetTabs 本身就是唯一的顺序来源,
        // 不需要另外维护一份"显示顺序"的影子列表。order 大小对不上 mAssetTabs 就什么都
        // 不做(说明拖拽发生的同时资产 tab 列表也变了, 数据已经不新鲜, 宁可跳过这次同步
        // 也不要把 mAssetTabs 搞乱)。
        void syncAssetTabOrder(const std::vector<AssetEditorTab*>& order);

        AssetEditorTab* findAssetTab(Guid guid) override;
        void addAssetTab(std::unique_ptr<AssetEditorTab> tab) override;
        void activateMainTab() override;

    private:
        void activateAdjacentTab(int direction);

    private:
        std::unique_ptr<MainEditorTab> mMainTab;
        std::vector<std::unique_ptr<AssetEditorTab>> mAssetTabs;
        EditorTab* mActiveTab;
    };
}
