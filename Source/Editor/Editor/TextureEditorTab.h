#pragma once
#include "AssetEditorTab.h"

#include <memory>

namespace hyp
{
    class Asset;
}

namespace hyp::ed
{
    class TextureEditor;
    class ThumbnailProvider;
    class AssetBrowser;

    // TextureEditor 目前只有一个面板, 默认布局就是让它占满整个 tab。
    class TextureEditorTab : public AssetEditorTab
    {
    public:
        TextureEditorTab(Asset& asset, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);
        ~TextureEditorTab() override;

        // public 是有意的, 见 MainEditorTab::collectPrepareTextures() 的注释——
        // EditorTabManager 通过 unique_ptr<AssetEditorTab>(基类指针)调用这个, 目前碰巧
        // protected 也能编译过(基类那份声明是 public, 访问检查看的是调用点的静态类型),
        // 但依赖这个巧合容易让人误解, 索性也声明成 public。
        void collectPrepareTextures(std::vector<TextureProxyHandle>& out) override;

    protected:
        void buildDefaultLayout(ImGuiID dockspaceId) override;
        void drawWindows() override;

    private:
        std::unique_ptr<TextureEditor> mEditor;
    };
}
