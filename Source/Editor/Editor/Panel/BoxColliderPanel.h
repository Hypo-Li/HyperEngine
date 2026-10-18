#pragma once

#include "ColliderPanel.h"

#include <Engine/Component/Collider/BoxCollider.h>

namespace hyp::ed
{
    class BoxColliderPanel final : public ColliderPanel
    {
    public:
        explicit BoxColliderPanel(BoxCollider& boxCollider) noexcept;

    protected:
        void onDraw() override;

    private:
        BoxCollider& mBoxCollider;
    };
}
