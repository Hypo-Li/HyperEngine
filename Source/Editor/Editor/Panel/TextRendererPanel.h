#pragma once

#include "QuadRendererPanel.h"

#include <Engine/Component/Renderer/TextRenderer.h>

namespace hyp::ed
{
    class TextRendererPanel final : public QuadRendererPanel
    {
    public:
        explicit TextRendererPanel(TextRenderer& textRenderer) noexcept;

    protected:
        void onDraw() override;

    private:
        TextRenderer& mTextRenderer;
    };
}
