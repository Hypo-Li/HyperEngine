#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace hyp
{
    template <typename T>
    using TMatrix2 = glm::tmat2x2<T>;
    using float2x2 = TMatrix2<float>;
    using double2x2 = TMatrix2<double>;

    template <typename T>
    using TMatrix3 = glm::tmat3x3<T>;
    using float3x3 = TMatrix3<float>;
    using double3x3 = TMatrix3<double>;

    template <typename T>
    using TMatrix4 = glm::tmat4x4<T>;
    using float4x4 = TMatrix4<float>;
    using double4x4 = TMatrix4<double>;

    template <typename T>
    using TMatrix3x4 = glm::tmat3x4<T>;
    using float3x4 = TMatrix3x4<float>;
    using double3x4 = TMatrix3x4<double>;

    template <typename T>
    using TMatrix4x3 = glm::tmat4x3<T>;
    using float4x3 = TMatrix4x3<float>;
    using double4x3 = TMatrix4x3<double>;
}
