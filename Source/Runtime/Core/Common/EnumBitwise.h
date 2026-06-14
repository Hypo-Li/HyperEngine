#pragma once
#include <type_traits>

namespace hyp
{
#define ENABLE_ENUM_CLASS_BITWISE(Enum) \
	inline           Enum& operator|=(Enum& lhs, Enum rhs) { return lhs = (Enum)((std::underlying_type_t<Enum>)lhs | (std::underlying_type_t<Enum>)rhs); } \
	inline           Enum& operator&=(Enum& lhs, Enum rhs) { return lhs = (Enum)((std::underlying_type_t<Enum>)lhs & (std::underlying_type_t<Enum>)rhs); } \
	inline           Enum& operator^=(Enum& lhs, Enum rhs) { return lhs = (Enum)((std::underlying_type_t<Enum>)lhs ^ (std::underlying_type_t<Enum>)rhs); } \
	inline constexpr Enum  operator| (Enum  lhs, Enum rhs) { return (Enum)((std::underlying_type_t<Enum>)lhs | (std::underlying_type_t<Enum>)rhs); } \
	inline constexpr Enum  operator& (Enum  lhs, Enum rhs) { return (Enum)((std::underlying_type_t<Enum>)lhs & (std::underlying_type_t<Enum>)rhs); } \
	inline constexpr Enum  operator^ (Enum  lhs, Enum rhs) { return (Enum)((std::underlying_type_t<Enum>)lhs ^ (std::underlying_type_t<Enum>)rhs); } \
	inline constexpr bool  operator! (Enum  e)             { return !(std::underlying_type_t<Enum>)e; } \
	inline constexpr Enum  operator~ (Enum  e)             { return (Enum)~(std::underlying_type_t<Enum>)e; }

#define ENABLE_ENUM_CLASS_BITWISE_FRIEND(Enum) \
	friend           Enum& operator|=(Enum& lhs, Enum rhs); \
	friend           Enum& operator&=(Enum& lhs, Enum rhs); \
	friend           Enum& operator^=(Enum& lhs, Enum rhs); \
	friend constexpr Enum  operator| (Enum  lhs, Enum rhs); \
	friend constexpr Enum  operator& (Enum  lhs, Enum rhs); \
	friend constexpr Enum  operator^ (Enum  lhs, Enum rhs); \
	friend constexpr bool  operator! (Enum  e); \
	friend constexpr Enum  operator~ (Enum  e);

    template<typename Enum>
    constexpr bool enumHasAllFlags(Enum Flags, Enum Contains)
    {
        using UnderlyingType = std::underlying_type_t<Enum>;
        return ((UnderlyingType)Flags & (UnderlyingType)Contains) == (UnderlyingType)Contains;
    }

    template<typename Enum>
    constexpr bool enumHasAnyFlags(Enum Flags, Enum Contains)
    {
        using UnderlyingType = std::underlying_type_t<Enum>;
        return ((UnderlyingType)Flags & (UnderlyingType)Contains) != 0;
    }

    template<typename Enum>
    void enumAddFlags(Enum& Flags, Enum FlagsToAdd)
    {
        using UnderlyingType = std::underlying_type_t<Enum>;
        Flags = (Enum)((UnderlyingType)Flags | (UnderlyingType)FlagsToAdd);
    }

    template<typename Enum>
    void enumRemoveFlags(Enum& Flags, Enum FlagsToRemove)
    {
        using UnderlyingType = std::underlying_type_t<Enum>;
        Flags = (Enum)((UnderlyingType)Flags & ~(UnderlyingType)FlagsToRemove);
    }
}
