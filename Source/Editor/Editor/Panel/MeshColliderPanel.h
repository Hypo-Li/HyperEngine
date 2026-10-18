#pragma once

#include "ColliderPanel.h"

#include <Engine/Component/Collider/MeshCollider.h>

namespace hyp::ed
{
    class MeshColliderPanel final : public ColliderPanel
    {
    public:
        explicit MeshColliderPanel(MeshCollider& meshCollider) noexcept;

    protected:
        void onDraw() override;

    private:
        MeshCollider& mMeshCollider;
    };
}
