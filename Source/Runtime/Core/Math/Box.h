#pragma once
#include <array>

#include "Vector.h"
#include "Matrix.h"

namespace hyp
{
    template <typename T>
    class TBox
    {
        friend class Reflection;
    public:
        using Vector3 = TVector3<T>;

        TBox() noexcept :
            mMin(std::numeric_limits<T>::max()),
            mMax(std::numeric_limits<T>::lowest())
        {
        }

        template <typename U>
        TBox(const TVector3<U>& min, const TVector3<U>& max) noexcept
            : mMin(min), mMax(max)
        {
        }

        template <typename U>
        TBox(const TBox<U>& other) noexcept
            : mMin(other.getMin()), mMax(other.getMax())
        {
        }

        bool operator==(const TBox& other) const noexcept
        {
            return mMin == other.mMin && mMax == other.mMax;
        }

        TBox operator+(const TBox& other) const noexcept
        {
            return TBox(*this) += other;
        }

        TBox operator+(const Vector3& point) const noexcept
        {
            return TBox(*this) += point;
        }

        TBox& operator+=(const TBox& other) noexcept
        {
            mMin = glm::min(mMin, other.mMin);
            mMax = glm::max(mMax, other.mMax);
            return *this;
        }

        TBox& operator+=(const Vector3& point) noexcept
        {
            mMin = glm::min(mMin, point);
            mMax = glm::max(mMax, point);
            return *this;
        }

        void setMin(const Vector3& min) noexcept
        {
            mMin = min;
        }

        void setMax(const Vector3& max) noexcept
        {
            mMax = max;
        }

        const Vector3& getMin() const noexcept
        {
            return mMin;
        }

        const Vector3& getMax() const noexcept
        {
            return mMax;
        }

        Vector3 getCenter() const noexcept
        {
            return (mMin + mMax) * static_cast<T>(0.5);
        }

        Vector3 getExtent() const noexcept
        {
            return (mMax - mMin) * static_cast<T>(0.5);
        }

        Vector3 getSize() const noexcept
        {
            return mMax - mMin;
        }

        T getVolume() const noexcept
        {
            return (mMax.x - mMin.x) * (mMax.y - mMin.y) * (mMax.z - mMin.z);
        }

        bool intersect(const TBox& other) const noexcept
        {
            if (mMin.x > other.mMax.x || other.mMin.x > mMax.x)
                return false;

            if (mMin.y > other.mMax.y || other.mMin.y > mMax.y)
                return false;

            if (mMin.z > other.mMax.z || other.mMin.z > mMax.z)
                return false;

            return true;
        }

        bool contains(const Vector3& point) const noexcept
        {
            return point.x >= mMin.x && point.x <= mMax.x &&
                point.y >= mMin.y && point.y <= mMax.y &&
                point.z >= mMin.z && point.z <= mMax.z;
        }

        bool contains(const TBox& other) const noexcept
        {
            return contains(other.mMin) && contains(other.mMax);
        }

        void getVertices(std::array<Vector3, 8>& vertices) const noexcept
        {
            vertices[0] = Vector3(mMin);
            vertices[1] = Vector3(mMin.x, mMin.y, mMax.z);
            vertices[2] = Vector3(mMin.x, mMax.y, mMin.z);
            vertices[3] = Vector3(mMax.x, mMin.y, mMin.z);
            vertices[4] = Vector3(mMax.x, mMax.y, mMin.z);
            vertices[5] = Vector3(mMax.x, mMin.y, mMax.z);
            vertices[6] = Vector3(mMin.x, mMax.y, mMax.z);
            vertices[7] = Vector3(mMax);
        }

        TBox transformBy(const TMatrix4<T>& matrix) const noexcept
        {
            if (mMin.x > mMax.x ||
                mMin.y > mMax.y ||
                mMin.z > mMax.z)
                return *this;

            TBox result;

            Vector3 newMin = matrix[3];
            Vector3 newMax = matrix[3];

            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 3; ++j)
                {
                    T a = matrix[i][j] * mMin[i];
                    T b = matrix[i][j] * mMax[i];

                    if (a < b)
                    {
                        newMin[j] += a;
                        newMax[j] += b;
                    }
                    else
                    {
                        newMin[j] += b;
                        newMax[j] += a;
                    }
                }
            }

            result.setMin(newMin);
            result.setMax(newMax);
            return result;
        }

    private:
        Vector3 mMin;
        Vector3 mMax;
    };

    using Boxf = TBox<float>;
    template <> Type* Reflection::createType<Boxf>();
    using Boxd = TBox<double>;
    template <> Type* Reflection::createType<Boxd>();
}
