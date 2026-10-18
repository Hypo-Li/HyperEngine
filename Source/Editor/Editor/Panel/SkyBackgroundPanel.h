#pragma once

#include "ComponentPanel.h"

#include <Engine/Component/Environment/SkyBackground.h>

namespace hyp::ed
{
    class SkyBackgroundPanel final : public IComponentPanel
    {
    public:
        explicit SkyBackgroundPanel(SkyBackground& skyBackground) noexcept;

    protected:
        void onDraw() override;

    private:
        SkyBackground& mSkyBackground;
    };
}
