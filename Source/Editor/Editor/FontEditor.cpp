#include "FontEditor.h"
#include "FontEditorTab.h"
#include "AssetEditorRegistry.h"

#include <Core/Asset/Asset.h>
#include <Core/Reflection/Reflection.h>
#include <Engine/Context.h>

#include <RenderBase/DeferredTaskQueue.h>
#include <RenderBase/FrameContext.h>
#include <RenderBase/ProxyManager.h>
#include <RenderBase/RenderFrontend.h>
#include <RenderBase/TextureProxy.h>

#include <HypImGui/ImGuiPropertyWidgets.h>
#include <HypImGui/ImGuiShadedImage.h>

#include <imgui.h>

#include <algorithm>
#include <string>

namespace hyp::ed
{
    namespace
    {
        AssetEditorRegistrar<Font, FontEditorTab> gRegistrar;

        std::string toUtf8(uint32_t codepoint)
        {
            std::string text;
            if (codepoint < 0x80)
            {
                text.push_back(static_cast<char>(codepoint));
            }
            else if (codepoint < 0x800)
            {
                text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else if (codepoint < 0x10000)
            {
                text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else
            {
                text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            return text;
        }
    }

    FontEditor::FontEditor(Asset& asset, UndoStack& undoStack, ThumbnailProvider& iconProvider, AssetBrowser& assetBrowser) :
        AssetEditorWindow(asset, undoStack),
        mFont(asset.getOrLoadRootObject<Font>())
    {
        if (!mFont)
            return;

        mEditedSettings = mFont->getBakeSettings();
        refreshPageViews();
    }

    FontEditor::~FontEditor()
    {
        releasePageViews();
    }

    void FontEditor::releasePageViews()
    {
        if (!mPageViews)
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [pageViews = std::move(mPageViews), handles = std::move(mPageHandles)](FrameContext& context)
            {
                for (const std::shared_ptr<ImGuiTextureHandle>& handle : handles)
                    handle->reset();

                context.getDeferredTaskQueue().push(
                    [texture = std::move(pageViews->texture), views = std::move(pageViews->views)]() mutable
                    {
                        views.clear();
                        texture = nullptr;
                    },
                    kMaxFramesInFlight + kMaxQueuedFrames
                );
            }
        );

        mPageViews.reset();
        mPageHandles.clear();
        mPagesProxy = {};
    }

    void FontEditor::refreshPageViews()
    {
        releasePageViews();

        Texture2DArray* pages = mFont->getPages();
        const uint32_t pageCount = mFont->getPageCount();
        if (!pages || pageCount == 0)
        {
            mSelectedPage = 0;
            return;
        }

        mPagesProxy = pages->getOrCreateTextureProxy();
        mPageViews = std::make_shared<PageViews>();
        for (uint32_t i = 0; i < pageCount; ++i)
            mPageHandles.push_back(std::make_shared<ImGuiTextureHandle>());

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mPagesProxy, pageViews = mPageViews, handles = mPageHandles](FrameContext& context)
            {
                TextureProxy* proxy = context.getProxyManager().getProxy(proxyHandle);
                if (!proxy)
                    return;

                pageViews->texture = &proxy->getTexture();

                rhi::TextureViewDesc viewDesc = proxy->getTextureView().getDesc();
                viewDesc.type = rhi::TextureViewType::Texture_View_2D;
                viewDesc.range.layerCount = 1;
                for (uint32_t i = 0; i < handles.size(); ++i)
                {
                    viewDesc.range.baseLayer = i;
                    pageViews->views.push_back(proxy->getTexture().createView(viewDesc));
                    handles[i]->update(*pageViews->views.back());
                }
            }
        );

        mSelectedPage = std::clamp(mSelectedPage, 0, static_cast<int>(pageCount) - 1);
    }

    void FontEditor::collectPrepareTextures(std::vector<TextureProxyHandle>& out)
    {
        if (mPagesProxy.isValid())
            out.push_back(mPagesProxy);
    }

    void FontEditor::pollBakeJob()
    {
        if (!mJob || !mJob->isFinished())
            return;

        if (std::optional<FontBakeResult> result = mJob->takeResult())
        {
            mMissingCodepoints = result->missingCodepoints;
            mFont->applyBakeResult(std::move(*result), mJob->getSettings());
            refreshPageViews();
            getAsset().setDirty(true);
            mBakeFailed = false;
        }
        else
        {
            mBakeFailed = true;
        }
        mJob.reset();
    }

    void FontEditor::onDraw()
    {
        if (!mFont)
        {
            ImGui::TextDisabled("Font failed to load.");
            return;
        }

        pollBakeJob();

        drawPreview();
        ImGui::Spacing();
        drawInfo();
        ImGui::Spacing();
        drawBake();
        ImGui::Spacing();
        drawGlyphs();
        ImGui::Spacing();
        drawMissingCharacters();
        ImGui::Spacing();
        drawSave();
    }

    void FontEditor::drawPreview()
    {
        ImGui::SeparatorText("Pages");

        if (mPageHandles.empty())
        {
            ImGui::TextDisabled("No pages baked yet.");
            return;
        }

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        ImGui::SliderInt("Page", &mSelectedPage, 0, static_cast<int>(mPageHandles.size()) - 1);

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

        ImTextureID id = mPageHandles[mSelectedPage]->get();
        if (!id)
        {
            ImGui::TextDisabled("Preview not ready yet.");
            return;
        }

        float availWidth = ImGui::GetContentRegionAvail().x;
        if (ImGui::BeginChild("PageChild", ImVec2(availWidth, availWidth * 0.5f), ImGuiChildFlags_ResizeY))
        {
            ImVec2 availSize = ImGui::GetContentRegionAvail();
            float side = std::min(availSize.x, availSize.y);
            ImVec2 imageSize(side, side);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availSize.x - side) * 0.5f);

            if (mFont->getSdfType() == FontSdfType::Sdf)
                GrayImage(id, imageSize, mChannelMask);
            else
                Image(id, imageSize, mChannelMask);
        }
        ImGui::EndChild();
    }

    void FontEditor::drawInfo()
    {
        ImGui::SeparatorText("Info");
        if (!BeginPropertyTable("FontInfo", ImGuiTableFlags_BordersInnerH))
            return;

        BeginPropertyRow("SDF Type");
        ImGui::TextUnformatted(
            Reflection::getEnumeration<FontSdfType>()->getNameByValue(static_cast<uint64_t>(mFont->getSdfType())).toCString());

        BeginPropertyRow("Bake Ppem");
        ImGui::Text("%u", mFont->getBakePpem());

        BeginPropertyRow("Spread Texels");
        ImGui::Text("%.1f", mFont->getSpreadTexels());

        BeginPropertyRow("Pages");
        ImGui::Text("%u", mFont->getPageCount());

        BeginPropertyRow("Glyphs");
        ImGui::Text("%zu", mFont->getGlyphs().size());

        BeginPropertyRow("Kerning Pairs");
        ImGui::Text("%zu", mFont->getKerningPairs().size());

        BeginPropertyRow("Ascent");
        ImGui::Text("%.4f", mFont->getAscent());

        BeginPropertyRow("Descent");
        ImGui::Text("%.4f", mFont->getDescent());

        BeginPropertyRow("Line Gap");
        ImGui::Text("%.4f", mFont->getLineGap());

        EndPropertyTable();
    }

    void FontEditor::drawBake()
    {
        ImGui::SeparatorText("Bake");

        ImGui::BeginDisabled(mJob != nullptr);
        if (DrawFontBakeSettings(mEditedSettings))
        {
            mFont->setBakeSettings(mEditedSettings);
            getAsset().setDirty(true);
        }
        ImGui::EndDisabled();

        ImGui::Spacing();

        if (mJob)
        {
            ImGui::ProgressBar(mJob->getProgress(), ImVec2(ImGui::GetFontSize() * 24.0f, 0.0f));
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                mJob->cancel();
            return;
        }

        ImGui::BeginDisabled(!mFont->hasSourceFace());
        if (ImGui::Button("Bake"))
        {
            mJob = std::make_unique<FontBakeJob>(mFont->copySourceFace(), mEditedSettings);
            mBakeFailed = false;
        }
        ImGui::EndDisabled();

        if (mBakeFailed)
        {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextUnformatted("Bake failed or was cancelled, see the log.");
            ImGui::PopStyleColor();
        }
    }

    void FontEditor::drawGlyphs()
    {
        if (!ImGui::CollapsingHeader("Glyphs"))
            return;

        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable("FontGlyphs", 7, flags, ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 12.0f)))
            return;

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Char");
        ImGui::TableSetupColumn("Codepoint");
        ImGui::TableSetupColumn("Page");
        ImGui::TableSetupColumn("Rect");
        ImGui::TableSetupColumn("Bearing");
        ImGui::TableSetupColumn("Advance");
        ImGui::TableSetupColumn("Kerning");
        ImGui::TableHeadersRow();

        const std::vector<FontGlyph>& glyphs = mFont->getGlyphs();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(glyphs.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const FontGlyph& glyph = glyphs[row];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(toUtf8(glyph.codepoint).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("U+%04X", glyph.codepoint);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", glyph.page);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u,%u %ux%u", glyph.x, glyph.y, glyph.width, glyph.height);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.3f, %.3f", glyph.bearingX, glyph.bearingY);
                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%.3f", glyph.advance);
                ImGui::TableSetColumnIndex(6);
                const auto kerningBegin = std::lower_bound(mFont->getKerningPairs().begin(), mFont->getKerningPairs().end(), glyph.codepoint,
                    [](const FontKerningPair& pair, uint32_t value) { return pair.left < value; });
                size_t kerningCount = 0;
                for (auto it = kerningBegin; it != mFont->getKerningPairs().end() && it->left == glyph.codepoint; ++it)
                    ++kerningCount;
                ImGui::Text("%zu", kerningCount);
            }
        }
        clipper.End();

        ImGui::EndTable();
    }

    void FontEditor::drawMissingCharacters()
    {
        const std::string header = "Missing Characters (" + std::to_string(mMissingCodepoints.size()) + ")###FontMissing";
        if (!ImGui::CollapsingHeader(header.c_str()))
            return;

        if (mMissingCodepoints.empty())
        {
            ImGui::TextDisabled("None reported by the last bake in this session.");
            return;
        }

        if (!ImGui::BeginChild("FontMissingChild", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 8.0f), ImGuiChildFlags_Borders))
        {
            ImGui::EndChild();
            return;
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(mMissingCodepoints.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const uint32_t codepoint = mMissingCodepoints[row];
                ImGui::Text("U+%04X  %s", codepoint, toUtf8(codepoint).c_str());
            }
        }
        clipper.End();

        ImGui::EndChild();
    }

    void FontEditor::drawSave()
    {
        ImGui::BeginDisabled(!getAsset().isDirty() || mJob != nullptr);
        if (ImGui::Button("Save"))
            getAsset().save();
        ImGui::EndDisabled();
    }
}
