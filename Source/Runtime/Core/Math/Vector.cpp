#include "Vector.h"

namespace hyp
{
    template <> Type* Reflection::createType<float2>()
    {
        auto s = new TStructure<float2>("float2");
        s->addProperty("x", &float2::x);
        s->addProperty("y", &float2::y);
        return s;
    }

    template <> Type* Reflection::createType<double2>()
    {
        auto s = new TStructure<double2>("double2");
        s->addProperty("x", &double2::x);
        s->addProperty("y", &double2::y);
        return s;
    }

    template <> Type* Reflection::createType<int2>()
    {
        auto s = new TStructure<int2>("int2");
        s->addProperty("x", &int2::x);
        s->addProperty("y", &int2::y);
        return s;
    }

    template <> Type* Reflection::createType<uint2>()
    {
        auto s = new TStructure<uint2>("uint2");
        s->addProperty("x", &uint2::x);
        s->addProperty("y", &uint2::y);
        return s;
    }

    template <> Type* Reflection::createType<float3>()
    {
        auto s = new TStructure<float3>("float3");
        s->addProperty("x", &float3::x);
        s->addProperty("y", &float3::y);
        s->addProperty("z", &float3::z);
        return s;
    }

    template <> Type* Reflection::createType<double3>()
    {
        auto s = new TStructure<double3>("double3");
        s->addProperty("x", &double3::x);
        s->addProperty("y", &double3::y);
        s->addProperty("z", &double3::z);
        return s;
    }

    template <> Type* Reflection::createType<int3>()
    {
        auto s = new TStructure<int3>("int3");
        s->addProperty("x", &int3::x);
        s->addProperty("y", &int3::y);
        s->addProperty("z", &int3::z);
        return s;
    }

    template <> Type* Reflection::createType<uint3>()
    {
        auto s = new TStructure<uint3>("uint3");
        s->addProperty("x", &uint3::x);
        s->addProperty("y", &uint3::y);
        s->addProperty("z", &uint3::z);
        return s;
    }

    template <> Type* Reflection::createType<float4>()
    {
        auto s = new TStructure<float4>("float4");
        s->addProperty("x", &float4::x);
        s->addProperty("y", &float4::y);
        s->addProperty("z", &float4::z);
        s->addProperty("w", &float4::w);
        return s;
    }

    template <> Type* Reflection::createType<double4>()
    {
        auto s = new TStructure<double4>("double4");
        s->addProperty("x", &double4::x);
        s->addProperty("y", &double4::y);
        s->addProperty("z", &double4::z);
        s->addProperty("w", &double4::w);
        return s;
    }

    template <> Type* Reflection::createType<int4>()
    {
        auto s = new TStructure<int4>("int4");
        s->addProperty("x", &int4::x);
        s->addProperty("y", &int4::y);
        s->addProperty("z", &int4::z);
        s->addProperty("w", &int4::w);
        return s;
    }

    template <> Type* Reflection::createType<uint4>()
    {
        auto s = new TStructure<uint4>("uint4");
        s->addProperty("x", &uint4::x);
        s->addProperty("y", &uint4::y);
        s->addProperty("z", &uint4::z);
        s->addProperty("w", &uint4::w);
        return s;
    }
}
