#pragma once
#include "Collider.h"
#include "../../Physics/ConvexHull.h"

namespace hyp
{
    class ConvexHullCollider : public Collider
    {
        REFLECT_CLASS(ConvexHullCollider)
    public:
        ConvexHullCollider();

        ConvexHull* getHull() const noexcept
        {
            return mHull;
        }

        void setHull(ConvexHull* hull) noexcept
        {
            mHull = hull;
        }

        // mHull 为空时跳过建 Body。
        void onRegister() override;

    protected:
        ShapeDesc getShapeDesc() const override
        {
            return ConvexHullShapeDesc{ mHull };
        }

    private:
        TRef<ConvexHull> mHull;
    };

    template <> Type* Reflection::createType<ConvexHullCollider>();
}
