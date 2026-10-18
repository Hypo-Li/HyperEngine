#pragma once
#include <Core/Object/Object.h>

namespace hyp
{
    // 不含任何 Jolt 类型, 由 PhysicsScene 负责翻译成 Jolt 的 Body/Shape 设置。
    class PhysicsMaterial : public Object
    {
        REFLECT_CLASS(PhysicsMaterial)
    public:
        // 声明顺序即优先级顺序(Average < Minimum < Multiply < Maximum), 不能打乱。
        enum class CombineMode
        {
            Average,
            Minimum,
            Multiply,
            Maximum
        };

        PhysicsMaterial() = default;

        float getFriction() const noexcept
        {
            return mFriction;
        }

        void setFriction(float friction) noexcept
        {
            mFriction = friction;
        }

        float getRestitution() const noexcept
        {
            return mRestitution;
        }

        void setRestitution(float restitution) noexcept
        {
            mRestitution = restitution;
        }

        // kg/m^3。
        float getDensity() const noexcept
        {
            return mDensity;
        }

        void setDensity(float density) noexcept
        {
            mDensity = density;
        }

        CombineMode getFrictionCombine() const noexcept
        {
            return mFrictionCombine;
        }

        void setFrictionCombine(CombineMode mode) noexcept
        {
            mFrictionCombine = mode;
        }

        CombineMode getRestitutionCombine() const noexcept
        {
            return mRestitutionCombine;
        }

        void setRestitutionCombine(CombineMode mode) noexcept
        {
            mRestitutionCombine = mode;
        }

    private:
        float mFriction = 0.6f;
        float mRestitution = 0.0f;
        float mDensity = 1000.0f;
        CombineMode mFrictionCombine = CombineMode::Average;
        CombineMode mRestitutionCombine = CombineMode::Average;
    };

    template <> Type* Reflection::createType<PhysicsMaterial>();
    template <> Type* Reflection::createType<PhysicsMaterial::CombineMode>();
}
