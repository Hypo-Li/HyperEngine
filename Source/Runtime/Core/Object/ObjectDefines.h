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
        None = 0,
        Default_Object = 1 << 0,
        Load = 1 << 1,
    };
    ENABLE_ENUM_CLASS_BITWISE(ObjectFlags)

    struct ObjectHandle
    {
        Class* clazz;
        uint32_t index;
        uint32_t version;
    };

    // version 在每次 free() 时递增(见 ObjectMemoryPool::free()), 所以哪怕 index/clazz 因为
    // slot 复用而撞了, 只要背后是析构重建过的不同对象, version 就一定不同——用来判断"这是不是
    // 我之前附着的那个还活着的对象", 比裸指针比较更可靠。
    inline bool operator==(const ObjectHandle& lhs, const ObjectHandle& rhs) noexcept
    {
        return lhs.clazz == rhs.clazz && lhs.index == rhs.index && lhs.version == rhs.version;
    }

    inline bool operator!=(const ObjectHandle& lhs, const ObjectHandle& rhs) noexcept
    {
        return !(lhs == rhs);
    }
}
