#pragma once
#include "AssetEditorWindow.h"
#include "FontBake.h"

#include <Engine/Font/Font.h>

#include <RHI/Texture.h>

#include <HypImGui/ImGuiTextureHandle.h>

#include <memory>
#include <vector>

namespace hyp::ed
{
    class ThumbnailProvider;
    class AssetBrowser;
    class UndoStack;

    class FontEditor final : public AssetEditorWindow
    {
    public:
        FontEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);

        ~FontEditor() override;

        void collectPrepareTextures(std::vector<TextureProxyHandle>& out) override;

    protected:
        void onDraw() override;

    private:
        // 只在渲染线程读写
        struct PageViews
        {
            // 保证数组纹理比视图晚销毁
            TRef<rhi::Texture> texture;
            std::vector<TRef<rhi::TextureView>> views;
        };

        void pollBakeJob();
        void refreshPageViews();
        void releasePageViews();
        void drawPreview();
        void drawInfo();
        void drawBake();
        void drawGlyphs();
        void drawMissingCharacters();
        void drawSave();

    private:
        TRef<Font> mFont;
        TextureProxyHandle mPagesProxy;
        std::shared_ptr<PageViews> mPageViews;
        std::vector<std::shared_ptr<ImGuiTextureHandle>> mPageHandles;
        int mSelectedPage = 0;
        uint32_t mChannelMask = 0xF;

        FontBakeSettings mEditedSettings;
        std::unique_ptr<FontBakeJob> mJob;
        bool mBakeFailed = false;
        std::vector<uint32_t> mMissingCodepoints;
    };
}
