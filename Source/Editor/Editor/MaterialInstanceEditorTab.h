#pragma once
#include "AssetEditorTab.h"

#include <memory>

namespace hyp
{
    class Asset;
}

namespace hyp::ed
{
    class MaterialInstanceEditor;
    class ThumbnailProvider;
    class AssetBrowser;

    // MaterialInstanceEditor 目前只有一个面板, 默认布局就是让它占满整个 tab。
    class MaterialInstanceEditorTab : public AssetEditorTab
    {
    public:
        MaterialInstanceEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);
        ~MaterialInstanceEditorTab() override;

    protected:
        void buildDefaultLayout(ImGuiID dockspaceId) override;
        void drawWindows() override;

    private:
        std::unique_ptr<MaterialInstanceEditor> mEditor;
    };
}
