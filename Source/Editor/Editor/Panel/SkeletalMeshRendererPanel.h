#pragma once

#include "ComponentPanel.h"

#include <Engine/Component/Renderer/SkeletalMeshRenderer.h>

namespace hyp::ed
{
    class SkeletalMeshRendererPanel final : public IComponentPanel
    {
    public:
        explicit SkeletalMeshRendererPanel(SkeletalMeshRenderer& meshRenderer) noexcept;

    protected:
        void onDraw() override;

    private:
        SkeletalMeshRenderer& mMeshRenderer;
    };
}
