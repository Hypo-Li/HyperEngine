#pragma once
#include <type_traits>
#include "../Common/EnumBitwise.h"

namespace hyp
{
    class Object;
    class Class;

    template <typename T>
    concept ObjectType = std::is_base_of_v<Object, T>;

    enum class ObjectFlags : uint32_t
    {
        None = 0u,
        Default_Object = 1u,
    };
    ENABLE_ENUM_CLASS_BITWISE(ObjectFlags)

    struct ObjectHandle
    {
        Class* clazz;
        uint32_t index;
        uint32_t version;
    };
}
