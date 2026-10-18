#pragma once
#include "Collider.h"
#include "../../Physics/CompoundCollision.h"

namespace hyp
{
    class CompoundCollider : public Collider
    {
        REFLECT_CLASS(CompoundCollider)
    public:
        CompoundCollider();

        CompoundCollision* getCompoundCollision() const noexcept
        {
            return mCompoundCollision;
        }

        void setCompoundCollision(CompoundCollision* compoundCollision) noexcept
        {
            mCompoundCollision = compoundCollision;
        }

        bool supportsDynamicMotion() const noexcept override
        {
            return !mCompoundCollision || !mCompoundCollision->requiresStatic();
        }

        // mCompoundCollision 为空或没有子形状时跳过建 Body。
        void onRegister() override;

    protected:
        ShapeDesc getShapeDesc() const override
        {
            return CompoundShapeDesc{ mCompoundCollision };
        }

    private:
        TRef<CompoundCollision> mCompoundCollision;
    };

    template <> Type* Reflection::createType<CompoundCollider>();
}
