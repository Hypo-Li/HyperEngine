#pragma once

#include "ColliderPanel.h"

#include <Engine/Component/Collider/ConvexHullCollider.h>

namespace hyp::ed
{
    class ConvexHullColliderPanel final : public ColliderPanel
    {
    public:
        explicit ConvexHullColliderPanel(ConvexHullCollider& convexHullCollider) noexcept;

    protected:
        void onDraw() override;

    private:
        ConvexHullCollider& mConvexHullCollider;
    };
}
