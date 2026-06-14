#pragma once
#include <glm/glm.hpp>

namespace hyp
{
    template <typename T>
    class TRay
    {
    public:
        using Vector3 = glm::tvec3<T>;

        TRay();
        TRay(const Vector3& origin, const Vector3& direction);

        bool operator==(const TRay& other)
        {
            return (mOrigin == other.mOrigin) && (mDirection == other.mDirection);
        }



        void setOrigin(const Vector3& origin)
        {
            mOrigin = origin;
        }

        void setDirection(const Vector3& direction)
        {
            mDirection = glm::normalize(direction);
        }

        const Vector3& getOrigin() const
        {
            return mOrigin;
        }

        const Vector3& getDirection() const
        {
            return mDirection;
        }

        

    private:
        Vector3 mOrigin;
        Vector3 mDirection;
    };

    using Rayf = TRay<float>;
    using Rayd = TRay<double>;
}
