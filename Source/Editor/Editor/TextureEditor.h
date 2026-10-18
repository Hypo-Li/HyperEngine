#pragma once
#include "AssetEditorWindow.h"

#include <Engine/Texture/Texture.h>

#include <HypImGui/ImGuiTextureHandle.h>

#include <memory>

namespace hyp::ed
{
    class ThumbnailProvider;
    class AssetBrowser;
    class UndoStack;

    class TextureEditor final : public AssetEditorWindow
    {
    public:
        // iconProvider/assetBrowser 这里用不上, 但 TextureEditorTab 统一按这个签名构造所有
        // 资产编辑器窗口, 必须保持一致
        TextureEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser);

        void collectPrepareTextures(std::vector<TextureProxyHandle>& out) override;

    protected:
        void onDraw() override;

    private:
        void drawPreview();
        void drawChannelMaskCheckboxes();
        void drawInfo();
        void drawTextureSettings();
        void drawSamplerSettings();
        void drawSave();

        // 通过渲染前端排队异步执行, 不会立即生效
        void refreshPreviewHandle();

        // 纹理参数改了之后重新烘焙——mMips 是从 mSourcePixels 重算的, 不需要
        // 重新导入源文件; 烘焙完 mMips 内容变了, 顺手把已经在用的 GPU 纹理也重新传一遍
        void rebakeAndRefresh();

    private:
        TRef<Texture> mTexture;
        TextureProxyHandle mProxyHandle;
        // shared_ptr 而不是直接持有: 构造时排给渲染线程的那条 update() 命令要捕获它, 而这个
        // 窗口随时可能在命令执行之前就被关闭销毁
        std::shared_ptr<ImGuiTextureHandle> mPreviewHandle;

        // R/G/B/A 四个 bit(bit0=R...bit3=A), 传给 NormalImage() 等 HypImGui 那族控件——
        // 具体每种模式怎么解读见 ImGuiShadedImage.h 顶部注释。默认全选, 等价于不遮罩。
        uint32_t mChannelMask = 0xF;
    };
}
