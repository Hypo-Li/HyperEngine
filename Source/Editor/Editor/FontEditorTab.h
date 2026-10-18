#pragma once
#include "AssetEditorTab.h"

#include <memory>

namespace hyp
{
    class Asset;
}

namespace hyp::ed
{
    class FontEditor;
    class ThumbnailProvider;
    class AssetBrowser;

    class FontEditorTab : public AssetEditorTab
    {
    public:
        FontEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);
        ~FontEditorTab() override;

        void collectPrepareTextures(std::vector<TextureProxyHandle>& out) override;

    protected:
        void buildDefaultLayout(ImGuiID dockspaceId) override;
        void drawWindows() override;

    private:
        std::unique_ptr<FontEditor> mEditor;
    };
}
