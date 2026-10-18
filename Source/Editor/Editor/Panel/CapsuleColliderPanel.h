#pragma once

#include "ColliderPanel.h"

#include <Engine/Component/Collider/CapsuleCollider.h>

namespace hyp::ed
{
    class CapsuleColliderPanel final : public ColliderPanel
    {
    public:
        explicit CapsuleColliderPanel(CapsuleCollider& capsuleCollider) noexcept;

    protected:
        void onDraw() override;

    private:
        CapsuleCollider& mCapsuleCollider;
    };
}
