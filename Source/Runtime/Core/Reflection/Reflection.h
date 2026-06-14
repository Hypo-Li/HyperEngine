#pragma once
#include <unordered_map>
#include <string>

#include "Type.h"
#include "Class.h"
#include "Fundamental.h"
#include "Enumeration.h"
#include "Structure.h"
#include "Special.h"

namespace hyp
{
    template <typename T>
    using remove_cvrp_t = std::remove_cv_t<std::remove_reference_t<std::remove_pointer_t<T>>>;

    class Reflection
    {
    public:
        template <typename T>
        static Type* getType();

        static Type* getType(HString typeName);

        template <typename T>
        static Fundamental* getFundamental();

        template <typename T>
        static Enumeration* getEnumeration();

        template <typename T>
        static Structure* getStructure();

        template <typename T>
        static Class* getClass();

        static Class* getClass(HString className);

        template <typename T>
        static Special* getSpecial();

        static void collectTypes();

    private:
        template <typename T>
        static Type* getOrCreateType();

        template <typename T>
        static Type* getTypeImpl();

        template <typename T>
        static Type* createType();

        using TypeIdMap = std::unordered_map<size_t, Type*>;
        static TypeIdMap& getTypeIdMap();

        using TypeNameMap = std::unordered_map<HString, Type*>;
        static TypeNameMap& getTypeNameMap();

    };
}

#include "Reflection.inl"
