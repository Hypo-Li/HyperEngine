#pragma once

#include "ColliderPanel.h"

#include <Engine/Component/Collider/CylinderCollider.h>

namespace hyp::ed
{
    class CylinderColliderPanel final : public ColliderPanel
    {
    public:
        explicit CylinderColliderPanel(CylinderCollider& cylinderCollider) noexcept;

    protected:
        void onDraw() override;

    private:
        CylinderCollider& mCylinderCollider;
    };
}
