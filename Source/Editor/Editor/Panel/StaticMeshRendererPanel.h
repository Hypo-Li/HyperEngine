#pragma once

#include "ComponentPanel.h"

#include <Engine/Component/Renderer/StaticMeshRenderer.h>

namespace hyp::ed
{
    class StaticMeshRendererPanel final : public IComponentPanel
    {
    public:
        explicit StaticMeshRendererPanel(StaticMeshRenderer& meshRenderer) noexcept;

    protected:
        void onDraw() override;

    private:
        StaticMeshRenderer& mMeshRenderer;
    };
}
