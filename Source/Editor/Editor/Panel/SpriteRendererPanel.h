#pragma once

#include "QuadRendererPanel.h"

#include <Engine/Component/Renderer/SpriteRenderer.h>

namespace hyp::ed
{
    class SpriteRendererPanel final : public QuadRendererPanel
    {
    public:
        explicit SpriteRendererPanel(SpriteRenderer& spriteRenderer) noexcept;

    protected:
        void onDraw() override;

    private:
        SpriteRenderer& mSpriteRenderer;
    };
}
