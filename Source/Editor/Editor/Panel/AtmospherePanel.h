#pragma once

#include "ComponentPanel.h"

#include <Engine/Component/Environment/Atmosphere.h>

namespace hyp::ed
{
    class AtmospherePanel final : public IComponentPanel
    {
    public:
        explicit AtmospherePanel(Atmosphere& atmosphere) noexcept;

    protected:
        void onDraw() override;

    private:
        Atmosphere& mAtmosphere;
    };
}
