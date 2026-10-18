#include "Special/StdArray.h"
#include "Special/StdList.h"
#include "Special/StdMap.h"
#include "Special/StdOptional.h"
#include "Special/StdPair.h"
#include "Special/StdSet.h"
#include "Special/StdString.h"
#include "Special/StdUnorderedMap.h"
#include "Special/StdUnorderedSet.h"
#include "Special/StdVariant.h"
#include "Special/StdVector.h"
#include "Special/HypHString.h"
#include "Special/HypBulkData.h"
#include "Special/HypSoftRef.h"

#include "Property.inl"
#include "Class.inl"

#include <Core/Common/Refable.h>
#include "Reflection.h"

namespace hyp
{
    template <typename T>
    struct container_traits<TRef<T>> {
        using type = T;
    };

    template <typename T>
    Type* Reflection::getType()
    {
        using T_Traits = remove_cvrp_t<T>;
        if constexpr (is_instance_of_v<T_Traits, TRef>)
        {
            return getTypeImpl<container_traits_t<T_Traits>>();
        }
        else
        {
            return getTypeImpl<T_Traits>();
        }
    }

    template<typename T>
    Type* Reflection::getOrCreateType()
    {
        TypeIdMap& typeIdMap = getTypeIdMap();
        auto findResult = typeIdMap.find(typeid(T).hash_code());
        if (findResult != typeIdMap.end())
        {
            return findResult->second;
        }
        Type* type = createType<T>();
        typeIdMap[typeid(T).hash_code()] = type;
        TypeNameMap& typeNameMap = getTypeNameMap();
        typeNameMap[type->getName()] = type;
        assert(typeIdMap.size() == typeNameMap.size());
        return type;
    }

    template <typename T>
    Type* Reflection::getTypeImpl()
    {
        static Type* type = getOrCreateType<T>();
        return type;
    }

    template <typename T>
    inline Type* Reflection::createType()
    {
        if constexpr (is_std_array_v<T>)
            return new TStdArray<T>;
        else if constexpr (is_instance_of_v<T, std::list>)
            return new TStdList<T>;
        else if constexpr (is_instance_of_v<T, std::map>)
            return new TStdMap<T>;
        else if constexpr (is_instance_of_v<T, std::optional>)
            return new TStdOptional<T>;
        else if constexpr (is_instance_of_v<T, std::pair>)
            return new TStdPair<T>;
        else if constexpr (is_instance_of_v<T, std::set>)
            return new TStdSet<T>;
        else if constexpr (std::is_same_v<T, std::string>)
            return new StdString;
        else if constexpr (is_instance_of_v<T, std::unordered_map>)
            return new TStdUnorderedMap<T>;
        else if constexpr (is_instance_of_v<T, std::unordered_set>)
            return new TStdUnorderedSet<T>;
        else if constexpr (is_instance_of_v<T, std::variant>)
            return new TStdVariant<T>;
        else if constexpr (is_instance_of_v<T, std::vector>)
            return new TStdVector<T>;
        else if constexpr (std::is_same_v<T, HString>)
            return new HypHString;
        else if constexpr (std::is_same_v<T, BulkData>)
            return new HypBulkData;
        else if constexpr (is_instance_of_v<T, TSoftRef>)
            return new THypSoftRef<container_traits_t<T>>();
        else
            static_assert(!sizeof(T*), "T is an unreflectable type.");
    }

#define GET_TYPE(kind) \
    template <typename T> \
    kind* Reflection::get##kind() \
    { \
        return static_cast<kind*>(getType<T>()); \
    }

    GET_TYPE(Fundamental)
    GET_TYPE(Enumeration)
    GET_TYPE(Structure)
    GET_TYPE(Class)
    GET_TYPE(Special)

#define CREATE_FUNDAMENTAL_TYPE(type) \
    template <> \
    inline Type* Reflection::createType<type>() \
    { \
        return new TFundamental<type>(#type); \
    }

    CREATE_FUNDAMENTAL_TYPE(void)
    CREATE_FUNDAMENTAL_TYPE(nullptr_t)
    CREATE_FUNDAMENTAL_TYPE(bool)
    CREATE_FUNDAMENTAL_TYPE(char)
    CREATE_FUNDAMENTAL_TYPE(wchar_t)
    CREATE_FUNDAMENTAL_TYPE(int8_t)
    CREATE_FUNDAMENTAL_TYPE(int16_t)
    CREATE_FUNDAMENTAL_TYPE(int32_t)
    CREATE_FUNDAMENTAL_TYPE(int64_t)
    CREATE_FUNDAMENTAL_TYPE(uint8_t)
    CREATE_FUNDAMENTAL_TYPE(uint16_t)
    CREATE_FUNDAMENTAL_TYPE(uint32_t)
    CREATE_FUNDAMENTAL_TYPE(uint64_t)
    CREATE_FUNDAMENTAL_TYPE(float)
    CREATE_FUNDAMENTAL_TYPE(double)
}
