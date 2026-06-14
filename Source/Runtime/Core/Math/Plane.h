#pragma once
#include "Vector.h"

namespace hyp
{
    template <typename T>
    class TPlane
    {
    public:
        TPlane() = default;
        TPlane(const float3& normal, T distance) : mNormal(normal), mDistance(distance)
        {
        }

        const float3& getNormal() const noexcept
        {
            return mNormal;
        }

        T getDistance() const noexcept
        {
            return mDistance;
        }

    private:
        float3 mNormal;
        T mDistance;
    };

    using Planef = TPlane<float>;
    using Planed = TPlane<double>;
}
