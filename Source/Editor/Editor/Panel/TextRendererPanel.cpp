#include "TextRendererPanel.h"
#include "ComponentPanelRegistry.h"
#include "../AssetComboEd.h"
#include "../ComponentRef.h"
#include "../ThumbnailProvider.h"

#include <Core/Asset/Asset.h>
#include <Core/Asset/AssetManager.h>
#include <Engine/Context.h>

#include <HypImGui/ImGuiEnumWidgets.h>

#include <imgui.h>
#include <imgui_stdlib.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<TextRenderer, TextRendererPanel> gRegistrar;

        TRef<Font> resolveFontAsset(AssetManager& assetManager, Guid guid)
        {
            Asset* asset = guid.isValid() ? assetManager.getAsset(guid) : nullptr;
            return asset ? asset->getOrLoadRootObject<Font>() : TRef<Font>();
        }
    }

    TextRendererPanel::TextRendererPanel(TextRenderer& textRenderer) noexcept :
        QuadRendererPanel(textRenderer),
        mTextRenderer(textRenderer)
    {
    }

    void TextRendererPanel::onDraw()
    {
        if (!BeginPropertyTable("TextRendererProperties", ImGuiTableFlags_BordersInnerH))
            return;

        AssetManager& assetManager = Context::get().getAssetManager();
        ComponentRef ref = ComponentRef::capture(mTextRenderer);
        const auto& defaults = *static_cast<const TextRenderer*>(TextRenderer::staticClass()->getDefaultObject());

        drawCommonRows();

        BeginPropertyRow("Font", 2.0f);
        Font* font = mTextRenderer.getFont();
        Guid fontGuidBefore = font ? font->getGuid() : Guid();
        Guid fontGuid = fontGuidBefore;
        bool fontChanged = AssetComboEd(
            "##Font", assetManager, Font::staticClass(), false, fontGuid, getAssetBrowser(), getIconProvider());
        fontChanged |= DrawPropertyReset("Font", fontGuid, Guid());
        if (fontChanged)
        {
            mTextRenderer.setFont(resolveFontAsset(assetManager, fontGuid).get());
            getUndoStack().push("Change Font"_hs,
                [ref, guid = fontGuidBefore]()
                {
                    ref.resolveAs<TextRenderer>()->setFont(resolveFontAsset(Context::get().getAssetManager(), guid).get());
                },
                [ref, guid = fontGuid]()
                {
                    ref.resolveAs<TextRenderer>()->setFont(resolveFontAsset(Context::get().getAssetManager(), guid).get());
                });
        }

        drawProperty<std::string>("Text", mTextRenderer.getText(), defaults.getText(),
            [](std::string& value)
            {
                return ImGui::InputTextMultiline("##Text", &value, ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 3.0f));
            },
            [ref](const std::string& v) { ref.resolveAs<TextRenderer>()->setText(v); },
            3.0f);

        drawProperty<float>("Font Size", mTextRenderer.getFontSize(), defaults.getFontSize(),
            [](float& value) { return ImGui::DragFloat("##FontSize", &value, 0.01f, 0.001f, 1000.0f, "%.3f"); },
            [ref](float v) { ref.resolveAs<TextRenderer>()->setFontSize(v); });

        drawProperty<float>("Line Spacing", mTextRenderer.getLineSpacing(), defaults.getLineSpacing(),
            [](float& value) { return ImGui::DragFloat("##LineSpacing", &value, 0.01f, 0.0f, 10.0f, "%.3f"); },
            [ref](float v) { ref.resolveAs<TextRenderer>()->setLineSpacing(v); });

        drawProperty<float>("Letter Spacing", mTextRenderer.getLetterSpacing(), defaults.getLetterSpacing(),
            [](float& value) { return ImGui::DragFloat("##LetterSpacing", &value, 0.005f, -1.0f, 4.0f, "%.3f"); },
            [ref](float v) { ref.resolveAs<TextRenderer>()->setLetterSpacing(v); });

        drawProperty<TextHorizontalAlignment>("Horizontal Alignment", mTextRenderer.getHorizontalAlignment(), defaults.getHorizontalAlignment(),
            [](TextHorizontalAlignment& value) { return EnumCombo("##HorizontalAlignment", &value); },
            [ref](TextHorizontalAlignment v) { ref.resolveAs<TextRenderer>()->setHorizontalAlignment(v); });

        drawProperty<float>("Max Width", mTextRenderer.getMaxWidth(), defaults.getMaxWidth(),
            [](float& value) { return ImGui::DragFloat("##MaxWidth", &value, 0.01f, 0.0f, 10000.0f, "%.3f"); },
            [ref](float v) { ref.resolveAs<TextRenderer>()->setMaxWidth(v); });

        EndPropertyTable();
    }
}
