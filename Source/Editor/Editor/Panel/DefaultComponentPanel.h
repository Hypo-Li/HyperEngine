#pragma once

#include "ComponentPanel.h"

namespace hyp::ed
{
    class DefaultComponentPanel final : public IComponentPanel
    {
    public:
        explicit DefaultComponentPanel(Component& component);

    protected:
        void onDraw() override;
    };
}
