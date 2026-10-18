#pragma once
#include "Mesh.h"
#include "../Skeleton/Skeleton.h"

namespace hyp
{
    // 不带 DistanceFieldData/CardData/MeshShapeProxy: 那些数据假设形状固定不变, 对姿势会变化的
    // 骨架网格不成立
    class SkeletalMesh : public Mesh
    {
        REFLECT_CLASS(SkeletalMesh)
    public:
        SkeletalMesh() = default;

        Skeleton* getSkeleton() const noexcept
        {
            return mSkeleton.get();
        }

        void setSkeleton(Skeleton* skeleton) noexcept
        {
            mSkeleton = skeleton;
        }

    private:
        TRef<Skeleton> mSkeleton;
    };

    template <> Type* Reflection::createType<SkeletalMesh>();
}
