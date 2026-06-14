#pragma once
#include <algorithm>

#include "Vector.h"
#include "Matrix.h"

namespace hyp
{
    struct Math
    {
        static constexpr float PI = 3.1415926535897932f;
        static constexpr float SMALL_NUMBER = 1e-8f;
        static constexpr float KINDA_SMALL_NUMBER = 1e-4f;

        template <typename T>
        inline static T square(T x) noexcept
        {
            return x * x;
        }

        template <typename T>
        inline static T length(const TVector2<T>& v) noexcept
        {
            return glm::length(v);
        }

        template <typename T>
        inline static T length(const TVector3<T>& v) noexcept
        {
            return glm::length(v);
        }

        template <typename T>
        inline static T lengthSqr(const TVector2<T>& v) noexcept
        {
            return glm::dot(v, v);
        }

        template <typename T>
        inline static T lengthSqr(const TVector3<T>& v) noexcept
        {
            return glm::dot(v, v);
        }

        template <typename T, typename U>
        inline static T dot(const TVector2<T>& v1, const TVector2<U>& v2) noexcept
        {
            return glm::dot(v1, v2);
        }

        template <typename T, typename U>
        inline static T dot(const TVector3<T>& v1, const TVector3<U>& v2) noexcept
        {
            return glm::dot(v1, v2);
        }

        template <typename T>
        inline static TVector3<T> cross(const TVector3<T>& v1, const TVector3<T>& v2) noexcept
        {
            return glm::cross(v1, v2);
        }

        template <typename T>
        inline static TVector3<T> normalize(const TVector3<T>& v) noexcept
        {
            return glm::normalize(v);
        }

        template <typename T>
        inline static TVector3<T> safeNormalize(const TVector3<T>& v) noexcept
        {
            return glm::normalize(v + TVector3<T>(SMALL_NUMBER));
        }
    };
}
