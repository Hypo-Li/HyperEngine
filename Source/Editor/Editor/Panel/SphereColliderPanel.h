#pragma once

#include "ColliderPanel.h"

#include <Engine/Component/Collider/SphereCollider.h>

namespace hyp::ed
{
    class SphereColliderPanel final : public ColliderPanel
    {
    public:
        explicit SphereColliderPanel(SphereCollider& sphereCollider) noexcept;

    protected:
        void onDraw() override;

    private:
        SphereCollider& mSphereCollider;
    };
}
