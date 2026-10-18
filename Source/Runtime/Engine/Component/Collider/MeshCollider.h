#pragma once
#include "Collider.h"
#include "../../Mesh/StaticMesh.h"

namespace hyp
{
    // 只能是 Static/Kinematic(Jolt 的 MeshShape::MustBeStatic())。
    class MeshCollider : public Collider
    {
        REFLECT_CLASS(MeshCollider)
    public:
        MeshCollider();

        StaticMesh* getMesh() const noexcept
        {
            return mMesh;
        }

        void setMesh(StaticMesh* mesh) noexcept
        {
            mMesh = mesh;
        }

        void setLODLevel(uint32_t lodLevel) noexcept
        {
            mLODLevel = lodLevel;
        }

        uint32_t getLODLevel() const noexcept
        {
            return mLODLevel;
        }

        bool supportsDynamicMotion() const noexcept override
        {
            return false;
        }

        // mMesh 为空时跳过建 Body。
        void onRegister() override;

    protected:
        ShapeDesc getShapeDesc() const override
        {
            return MeshShapeDesc{ mMesh, mLODLevel };
        }

    private:
        TRef<StaticMesh> mMesh;
        uint32_t mLODLevel = 0;
    };

    template <> Type* Reflection::createType<MeshCollider>();
}
