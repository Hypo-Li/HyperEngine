#pragma once
#include "Math.h"

namespace hyp
{
    template <typename T>
    class TSphere
    {
    public:
        using Vector3 = TVector3<T>;

        TSphere() :
            mCenter(static_cast<T>(0)),
            mRadius(std::numeric_limits<T>::lowest())
        {

        }

        TSphere(const Vector3& center, T radius) :
            mCenter(center),
            mRadius(radius)
        {

        }

        template <typename U>
        TSphere(const TSphere<U>& other) :
            mCenter(other.getCenter()),
            mRadius(other.getRadius())
        {

        }

        bool operator==(const TSphere& other)
        {
            return mCenter == other.mCenter && mRadius == other.mRadius;
        }

        TSphere operator+(const TSphere& other) const
        {
            return TSphere(*this) += other;
        }

        TSphere& operator+=(const TSphere& other)
        {
            if (mRadius == static_cast<T>(0))
            {
                *this = other;
                return *this;
            }

            Vector3 toOther = other.mCenter - mCenter;
            T distSqr = Math::dot(toOther, toOther);

            if (Math::square(mRadius - other.mRadius) + 1e-4f >= distSqr)
            {
                if (mRadius < other.mRadius)
                {
                    *this = other;
                }
            }
            else
            {
                T dist = std::sqrt(distSqr);

                TSphere newSphere(mCenter, (dist + other.mRadius + mRadius) * static_cast<T>(0.5));
                if (dist > 1e-8f)
                {
                    newSphere.mCenter += toOther * ((newSphere.mRadius - mRadius) / dist);
                }

                *this = newSphere;
            }

            return *this;
        }

        void setCenter(const Vector3& center)
        {
            mCenter = center;
        }

        void setRadius(T radius)
        {
            mRadius = std::max(radius, static_cast<T>(0));
        }

        const Vector3& getCenter() const
        {
            return mCenter;
        }

        T getRadius() const
        {
            return mRadius;
        }

        T getVolume() const
        {
            return (4.0 / 3.0) * Math::PI * (mRadius * mRadius * mRadius);
        }

        bool intersect(const TSphere& other, T tolerance = 1e-4f) const
        {
            auto diff = mCenter - other.mCenter;
            return Math::dot(diff, diff) <= Math::square(std::max(static_cast<T>(0), other.mRadius + mRadius + tolerance));
        }

        bool contains(const Vector3& point, T tolerance = 1e-4f) const
        {
            auto diff = mCenter - point;
            return Math::dot(diff, diff) <= Math::square(mRadius + tolerance);
        }

        bool contains(const TSphere& other, T tolerance = 1e-4f) const
        {
            auto diff = mCenter - other.mCenter;
            return Math::dot(diff, diff) <= Math::square(std::max(static_cast<T>(0), other.mRadius + mRadius + tolerance));
        }

        TSphere transformBy(const TMatrix4<T>& matrix) const noexcept
        {
            TSphere result;
            result.setCenter(matrix * TVector4<T>(mCenter, 1.0));
            T sx = Math::length(Vector3(matrix[0]));
            T sy = Math::length(Vector3(matrix[1]));
            T sz = Math::length(Vector3(matrix[2]));
            result.setRadius(mRadius * std::max(std::max(sx, sy), sz));
        }

    private:
        Vector3 mCenter;
        T mRadius;
    };

    using Spheref = TSphere<float>;
    using Sphered = TSphere<double>;
}
