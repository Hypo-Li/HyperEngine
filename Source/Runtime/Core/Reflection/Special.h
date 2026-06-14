#pragma once
#include "Type.h"

#include <type_traits>
#include <array>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <string>

#include "../Common/HString.h"

namespace hyp
{
    template <class T, template<class...> class U>
    constexpr bool is_instance_of_v = std::false_type{};

    template <template <class...> class U, class... Vs>
    constexpr bool is_instance_of_v<U<Vs...>, U> = std::true_type{};

    template <class>
    constexpr bool is_std_array_v = std::false_type{};

    template <class T, size_t Size>
    constexpr bool is_std_array_v<std::array<T, Size>> = std::true_type{};

    template <typename T>
    struct container_traits;

    template <typename T>
    using container_traits_t = typename container_traits<T>::type;
    template <typename T>
    using container_traits_t1 = typename container_traits<T>::type1;
    template <typename T>
    using container_traits_t2 = typename container_traits<T>::type2;

    template <typename T>
    concept IsSpecialType =
        is_std_array_v<T> ||
        is_instance_of_v<T, std::list> ||
        is_instance_of_v<T, std::map> ||
        is_instance_of_v<T, std::pair> ||
        is_instance_of_v<T, std::set> ||
        std::is_same_v<T, std::string> ||
        is_instance_of_v<T, std::unordered_map> ||
        is_instance_of_v<T, std::unordered_set> ||
        is_instance_of_v<T, std::variant> ||
        is_instance_of_v<T, std::vector> ||
        std::is_same_v<T, HString>;

    class Special : public Type
    {
    public:
        enum class Case
        {
            StdArray,
            StdList,
            StdMap,
            StdPair,
            StdSet,
            StdString,
            StdUnorderedMap,
            StdUnorderedSet,
            StdVariant,
            StdVector,

            HyperHString,
        };

        Case getCase() const
        {
            return mCase;
        }

    protected:
        Special(HString name, size_t size, Case caze) :
            Type(name, size, Kind::Special), mCase(caze) {}

        Case mCase;
    };
}
