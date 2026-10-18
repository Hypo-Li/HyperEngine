#include "TextureEditor.h"
#include "TextureEditorTab.h"
#include "AssetEditorRegistry.h"
#include "UndoStack.h"

#include <Core/Asset/Asset.h>
#include <Core/Reflection/Reflection.h>
#include <Engine/Context.h>
#include <Engine/Texture/Texture2D.h>
#include <Engine/Texture/TextureCube.h>

#include <RenderBase/FrameContext.h>
#include <RenderBase/ProxyManager.h>
#include <RenderBase/RenderFrontend.h>
#include <RenderBase/TextureProxy.h>

#include <HypImGui/ImGuiEnumWidgets.h>
#include <HypImGui/ImGuiPropertyWidgets.h>
#include <HypImGui/ImGuiShadedImage.h>

#include <imgui.h>

#include <algorithm>

namespace hyp::ed
{
    namespace
    {
        AssetEditorRegistrar<Texture2D, TextureEditorTab> gRegistrar;
        AssetEditorRegistrar<TextureCube, TextureEditorTab> gRegistrarCube;
    }

    TextureEditor::TextureEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorWindow(asset, undoStack),
        mTexture(asset.getOrLoadRootObject<Texture>()),
        mPreviewHandle(std::make_shared<ImGuiTextureHandle>())
    {
        if (!mTexture)
            return;

        mProxyHandle = mTexture->getOrCreateTextureProxy();
        refreshPreviewHandle();
    }

    void TextureEditor::refreshPreviewHandle()
    {
        TextureProxyHandle proxyHandle = mProxyHandle;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle, previewHandle = mPreviewHandle](FrameContext& context)
            {
                if (TextureProxy* proxy = context.getProxyManager().getProxy(proxyHandle))
                    previewHandle->update(proxy->getTextureView());
            }
        );
    }

    void TextureEditor::collectPrepareTextures(std::vector<TextureProxyHandle>& out)
    {
        if (mProxyHandle.isValid())
            out.push_back(mProxyHandle);
    }

    void TextureEditor::onDraw()
    {
        if (!mTexture)
        {
            ImGui::TextDisabled("Texture failed to load.");
            return;
        }

        drawPreview();
        ImGui::Spacing();
        drawInfo();
        ImGui::Spacing();
        drawTextureSettings();
        ImGui::Spacing();
        drawSamplerSettings();
        ImGui::Spacing();
        drawSave();
    }

    void TextureEditor::drawPreview()
    {
        ImGui::SeparatorText("Preview");

        ImTextureID id = mPreviewHandle->get();
        if (!id)
        {
            ImGui::TextDisabled("Preview not ready yet.");
            return;
        }

        const Texture::Description& desc = mTexture->getDescription();
        if (desc.width == 0 || desc.height == 0)
            return;
        // cube 按等距投影(equirectangular)展开显示, 固定 2:1——desc.width/height 是单个
        // face 的边长(正方形), 不是等距图本身的宽高比, 这里只用来推导缩放/居中, 不代表
        // 真实的预览像素尺寸。
        float texWidth = desc.isCube ? static_cast<float>(desc.width) * 2.0f : static_cast<float>(desc.width);
        float texHeight = static_cast<float>(desc.height);

        drawChannelMaskCheckboxes();

        // 用 ImGuiChildFlags_ResizeY 让这块预览区自带一条可拖拽的底边——尺寸由 ImGui 自己
        // 按这个 child 的 ID 记忆(存进 .ini), 不需要我们自己维护一个高度成员。child 本身
        // 宽度铺满整行(不然它不居中、拖拽条也只会跟着变窄, 看着像是残缺的), 图片默认显示
        // 宽度是可用宽度的 1/2——反推出对应高度传给 child, 里面 min(availW/texW, availH/texH)
        // 那套缩放逻辑会自然按高度封顶、水平居中, 效果就是"整行宽的 child 里摆一张默认 1/2
        // 宽的小图"。这只是"第一次打开时"的默认高度, 拖过一次之后就不再生效。
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImVec2 childSize(availWidth, (availWidth / 2.0f) * texHeight / texWidth);

        if (ImGui::BeginChild("PreviewChild", childSize, ImGuiChildFlags_ResizeY))
        {
            ImVec2 availSize = ImGui::GetContentRegionAvail();
            float scale = std::min(availSize.x / texWidth, availSize.y / texHeight);
            ImVec2 imageSize(texWidth * scale, texHeight * scale);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availSize.x - imageSize.x) * 0.5f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (availSize.y - imageSize.y) * 0.5f);

            if (desc.isCube)
                CubeImage(id, imageSize, mChannelMask);
            else if (mTexture->getCompressionPreset() == CompressionPreset::Normal_Map)
                NormalImage(id, imageSize, mChannelMask);
            else if (mTexture->getCompressionPreset() == CompressionPreset::Grayscale)
                GrayImage(id, imageSize, mChannelMask);
            else if (mTexture->isSRGB())
                SRGBImage(id, imageSize, mChannelMask);
            else
                Image(id, imageSize, mChannelMask);
        }
        ImGui::EndChild();
    }

    void TextureEditor::drawChannelMaskCheckboxes()
    {
        bool r = (mChannelMask & 0x1u) != 0;
        bool g = (mChannelMask & 0x2u) != 0;
        bool b = (mChannelMask & 0x4u) != 0;
        bool a = (mChannelMask & 0x8u) != 0;

        ImGui::Checkbox("R", &r);
        ImGui::SameLine();
        ImGui::Checkbox("G", &g);
        ImGui::SameLine();
        ImGui::Checkbox("B", &b);
        ImGui::SameLine();
        ImGui::Checkbox("A", &a);

        mChannelMask = (r ? 0x1u : 0u) | (g ? 0x2u : 0u) | (b ? 0x4u : 0u) | (a ? 0x8u : 0u);
    }

    void TextureEditor::drawInfo()
    {
        ImGui::SeparatorText("Info");
        if (!BeginPropertyTable("TextureInfo", ImGuiTableFlags_BordersInnerH))
            return;

        const Texture::Description& desc = mTexture->getDescription();

        BeginPropertyRow("Width");
        ImGui::Text("%u", desc.width);

        BeginPropertyRow("Height");
        ImGui::Text("%u", desc.height);

        BeginPropertyRow("Format");
        ImGui::TextUnformatted(
            Reflection::getEnumeration<PixelFormat>()->getNameByValue(static_cast<uint64_t>(desc.format)).toCString());

        BeginPropertyRow("Mip Levels");
        ImGui::Text("%u", desc.mipLevels);

        EndPropertyTable();
    }

    void TextureEditor::drawTextureSettings()
    {
        ImGui::SeparatorText("Texture");
        if (!BeginPropertyTable("TextureSettings", ImGuiTableFlags_BordersInnerH))
            return;

        BeginPropertyRow("sRGB");
        bool isSRGB = mTexture->isSRGB();
        if (ImGui::Checkbox("##sRGB", &isSRGB))
        {
            bool before = mTexture->isSRGB();
            undoStack().push("Change Texture sRGB"_hs,
                [this, before]() { mTexture->setSRGB(before); rebakeAndRefresh(); },
                [this, isSRGB]() { mTexture->setSRGB(isSRGB); rebakeAndRefresh(); });
            mTexture->setSRGB(isSRGB);
            rebakeAndRefresh();
        }

        if (mTexture->isSRGB() && (mTexture->getCompressionPreset() == CompressionPreset::Normal_Map ||
            mTexture->getCompressionPreset() == CompressionPreset::Grayscale))
        {
            BeginPropertyRow("");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
            ImGui::TextWrapped("sRGB has no effect on Normal Map / Grayscale textures; preview uses the Normal/Gray view instead.");
            ImGui::PopStyleColor();
        }

        BeginPropertyRow("Compression");
        CompressionPreset preset = mTexture->getCompressionPreset();
        if (EnumCombo("##Compression", &preset))
        {
            CompressionPreset before = mTexture->getCompressionPreset();
            undoStack().push("Change Texture Compression"_hs,
                [this, before]() { mTexture->setCompressionPreset(before); rebakeAndRefresh(); },
                [this, preset]() { mTexture->setCompressionPreset(preset); rebakeAndRefresh(); });
            mTexture->setCompressionPreset(preset);
            rebakeAndRefresh();
        }

        BeginPropertyRow("Flip Green Channel");
        bool flipGreenChannel = mTexture->getFlipGreenChannel();
        if (ImGui::Checkbox("##FlipGreenChannel", &flipGreenChannel))
        {
            bool before = mTexture->getFlipGreenChannel();
            undoStack().push("Change Texture Flip Green Channel"_hs,
                [this, before]() { mTexture->setFlipGreenChannel(before); rebakeAndRefresh(); },
                [this, flipGreenChannel]() { mTexture->setFlipGreenChannel(flipGreenChannel); rebakeAndRefresh(); });
            mTexture->setFlipGreenChannel(flipGreenChannel);
            rebakeAndRefresh();
        }

        EndPropertyTable();
    }

    void TextureEditor::drawSamplerSettings()
    {
        ImGui::SeparatorText("Sampler");
        if (!BeginPropertyTable("TextureSampler", ImGuiTableFlags_BordersInnerH))
            return;

        SampleState before = mTexture->getSampleState();
        SampleState state = before;
        bool changed = false;

        BeginPropertyRow("Filter");
        changed |= EnumCombo("##Filter", &state.filter);

        BeginPropertyRow("Address U");
        changed |= EnumCombo("##AddressU", &state.addressModeU);

        BeginPropertyRow("Address V");
        changed |= EnumCombo("##AddressV", &state.addressModeV);

        BeginPropertyRow("Address W");
        changed |= EnumCombo("##AddressW", &state.addressModeW);

        if (changed)
        {
            undoStack().push("Change Texture Sampler"_hs,
                [this, before]() { mTexture->setSampleState(before); getAsset().setDirty(true); },
                [this, state]() { mTexture->setSampleState(state); getAsset().setDirty(true); });
            mTexture->setSampleState(state);
            getAsset().setDirty(true);
        }

        EndPropertyTable();
    }

    void TextureEditor::drawSave()
    {
        ImGui::BeginDisabled(!getAsset().isDirty());
        if (ImGui::Button("Save"))
            getAsset().save();
        ImGui::EndDisabled();
    }

    void TextureEditor::rebakeAndRefresh()
    {
        mTexture->bakeMips();
        mTexture->uploadToGpu();
        refreshPreviewHandle();
        getAsset().setDirty(true);
    }
}
