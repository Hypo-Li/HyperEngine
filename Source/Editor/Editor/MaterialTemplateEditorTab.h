#pragma once
#include "AssetEditorTab.h"

#include <memory>

namespace hyp
{
    class Asset;
}

namespace hyp::ed
{
    class MaterialTemplateEditor;
    class ThumbnailProvider;
    class AssetBrowser;

    // MaterialTemplateEditor 目前只有一个面板, 默认布局就是让它占满整个 tab。
    class MaterialTemplateEditorTab : public AssetEditorTab
    {
    public:
        MaterialTemplateEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);
        ~MaterialTemplateEditorTab() override;

        bool hasUnsavedChanges() const override;
        void applyPendingChanges() override;

    protected:
        void buildDefaultLayout(ImGuiID dockspaceId) override;
        void drawWindows() override;

    private:
        std::unique_ptr<MaterialTemplateEditor> mEditor;
    };
}
