#include "SpriteRendererPanel.h"
#include "ComponentPanelRegistry.h"
#include "../ComponentRef.h"

#include <imgui.h>

namespace hyp::ed
{
    namespace
    {
        ComponentPanelRegistrar<SpriteRenderer, SpriteRendererPanel> gRegistrar;
    }

    SpriteRendererPanel::SpriteRendererPanel(SpriteRenderer& spriteRenderer) noexcept :
        QuadRendererPanel(spriteRenderer),
        mSpriteRenderer(spriteRenderer)
    {
    }

    void SpriteRendererPanel::onDraw()
    {
        if (!BeginPropertyTable("SpriteRendererProperties", ImGuiTableFlags_BordersInnerH))
            return;

        ComponentRef ref = ComponentRef::capture(mSpriteRenderer);
        const auto& defaults = *static_cast<const SpriteRenderer*>(SpriteRenderer::staticClass()->getDefaultObject());

        drawCommonRows();

        drawProperty<float2>("Size", mSpriteRenderer.getSize(), defaults.getSize(),
            [](float2& value) { return ImGui::DragFloat2("##Size", &value.x, 0.01f, 0.0f, 0.0f, "%.3f"); },
            [ref](const float2& v) { ref.resolveAs<SpriteRenderer>()->setSize(v); });

        drawProperty<float2>("UV Min", mSpriteRenderer.getUvMin(), defaults.getUvMin(),
            [](float2& value) { return ImGui::DragFloat2("##UvMin", &value.x, 0.005f, 0.0f, 1.0f, "%.3f"); },
            [ref](const float2& v) { ref.resolveAs<SpriteRenderer>()->setUvMin(v); });

        drawProperty<float2>("UV Max", mSpriteRenderer.getUvMax(), defaults.getUvMax(),
            [](float2& value) { return ImGui::DragFloat2("##UvMax", &value.x, 0.005f, 0.0f, 1.0f, "%.3f"); },
            [ref](const float2& v) { ref.resolveAs<SpriteRenderer>()->setUvMax(v); });

        drawProperty<bool>("Flip X", mSpriteRenderer.isFlipX(), defaults.isFlipX(),
            [](bool& value) { return ImGui::Checkbox("##FlipX", &value); },
            [ref](bool v) { ref.resolveAs<SpriteRenderer>()->setFlipX(v); });

        drawProperty<bool>("Flip Y", mSpriteRenderer.isFlipY(), defaults.isFlipY(),
            [](bool& value) { return ImGui::Checkbox("##FlipY", &value); },
            [ref](bool v) { ref.resolveAs<SpriteRenderer>()->setFlipY(v); });

        EndPropertyTable();
    }
}
