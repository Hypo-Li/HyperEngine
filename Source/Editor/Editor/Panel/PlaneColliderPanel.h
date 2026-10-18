#pragma once

#include "ColliderPanel.h"

#include <Engine/Component/Collider/PlaneCollider.h>

namespace hyp::ed
{
    class PlaneColliderPanel final : public ColliderPanel
    {
    public:
        explicit PlaneColliderPanel(PlaneCollider& planeCollider) noexcept;

    protected:
        void onDraw() override;

    private:
        PlaneCollider& mPlaneCollider;
    };
}
