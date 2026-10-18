#pragma once

#include "ColliderPanel.h"

#include <Engine/Component/Collider/CompoundCollider.h>

namespace hyp::ed
{
    class CompoundColliderPanel final : public ColliderPanel
    {
    public:
        explicit CompoundColliderPanel(CompoundCollider& compoundCollider) noexcept;

    protected:
        void onDraw() override;

    private:
        CompoundCollider& mCompoundCollider;
    };
}
