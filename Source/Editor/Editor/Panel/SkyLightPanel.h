#pragma once

#include "ComponentPanel.h"

#include <Engine/Component/Light/SkyLight.h>

namespace hyp::ed
{
    class SkyLightPanel final : public IComponentPanel
    {
    public:
        explicit SkyLightPanel(SkyLight& skyLight) noexcept;

    protected:
        void onDraw() override;

    private:
        SkyLight& mSkyLight;
    };
}
