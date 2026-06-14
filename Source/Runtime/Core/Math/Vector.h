#pragma once
#include <glm/glm.hpp>
#include "../Reflection/Reflection.h"

namespace hyp
{
    template <typename T>
    using TVector2 = glm::tvec2<T>;
    using float2 = TVector2<float>;
    using double2 = TVector2<double>;
    using int2 = TVector2<int>;
    using uint2 = TVector2<unsigned int>;

    template <typename T>
    using TVector3 = glm::tvec3<T>;
    using float3 = TVector3<float>;
    using double3 = TVector3<double>;
    using int3 = TVector3<int>;
    using uint3 = TVector3<unsigned int>;

    template <typename T>
    using TVector4 = glm::tvec4<T>;
    using float4 = TVector4<float>;
    using double4 = TVector4<double>;
    using int4 = TVector4<int>;
    using uint4 = TVector4<unsigned int>;

    template <> Type* Reflection::createType<float2>();
    template <> Type* Reflection::createType<double2>();
    template <> Type* Reflection::createType<int2>();
    template <> Type* Reflection::createType<uint2>();

    template <> Type* Reflection::createType<float3>();
    template <> Type* Reflection::createType<double3>();
    template <> Type* Reflection::createType<int3>();
    template <> Type* Reflection::createType<uint3>();

    template <> Type* Reflection::createType<float4>();
    template <> Type* Reflection::createType<double4>();
    template <> Type* Reflection::createType<int4>();
    template <> Type* Reflection::createType<uint4>();
}
