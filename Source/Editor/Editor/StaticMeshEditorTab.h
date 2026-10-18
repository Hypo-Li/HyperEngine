#pragma once
#include "AssetEditorTab.h"

#include <memory>

namespace hyp
{
    class Asset;
}

namespace hyp::ed
{
    class StaticMeshEditor;
    class ThumbnailProvider;
    class AssetBrowser;

    class StaticMeshEditorTab : public AssetEditorTab
    {
    public:
        StaticMeshEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);
        ~StaticMeshEditorTab() override;

    protected:
        void buildDefaultLayout(ImGuiID dockspaceId) override;
        void drawWindows() override;

    private:
        std::unique_ptr<StaticMeshEditor> mEditor;
    };
}
