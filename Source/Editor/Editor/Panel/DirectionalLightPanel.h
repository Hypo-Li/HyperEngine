#pragma once

#include "ComponentPanel.h"

#include <Engine/Component/Light/DirectionalLight.h>

namespace hyp::ed
{
    class DirectionalLightPanel final : public IComponentPanel
    {
    public:
        explicit DirectionalLightPanel(DirectionalLight& light) noexcept;

    protected:
        void onDraw() override;

    private:
        DirectionalLight& mLight;
    };
}
