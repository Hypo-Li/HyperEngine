#pragma once
#include "AssetEditorTab.h"

#include <memory>

namespace hyp
{
    class Asset;
}

namespace hyp::ed
{
    class MaterialFunctionLibraryEditor;
    class ThumbnailProvider;
    class AssetBrowser;

    // MaterialFunctionLibraryEditor 目前只有一个面板, 默认布局就是让它占满整个 tab。
    class MaterialFunctionLibraryEditorTab : public AssetEditorTab
    {
    public:
        MaterialFunctionLibraryEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);
        ~MaterialFunctionLibraryEditorTab() override;

        bool hasUnsavedChanges() const override;
        void applyPendingChanges() override;

    protected:
        void buildDefaultLayout(ImGuiID dockspaceId) override;
        void drawWindows() override;

    private:
        std::unique_ptr<MaterialFunctionLibraryEditor> mEditor;
    };
}
