#include "Reflection.h"

namespace hyp
{
    Type* Reflection::getType(HString typeName)
    {
        TypeNameMap& typeNameMap = getTypeNameMap();
        auto findResult = typeNameMap.find(typeName);
        if (findResult == typeNameMap.end())
            return nullptr;
        return findResult->second;
    }

    Class* Reflection::getClass(HString className)
    {
        Type* type = getType(className);
        if (type && type->getKind() == Type::Kind::Class)
            return static_cast<Class*>(type);
        return nullptr;
    }

    Reflection::TypeIdMap& Reflection::getTypeIdMap()
    {
        static TypeIdMap typeIdMap;
        return typeIdMap;
    }

    Reflection::TypeNameMap& Reflection::getTypeNameMap()
    {
        static TypeNameMap typeNameMap;
        return typeNameMap;
    }

    void collectPropertyTypes(std::unordered_set<Type*>& collectedTypes, std::span<Property* const> properties)
    {
        for (Property* property : properties)
        {
            Type* propertyType = property->getDeclaredType();
            if (!collectedTypes.contains(propertyType))
            {
                collectedTypes.emplace(propertyType);
                if (propertyType->getKind() == Type::Kind::Class)
                {
                    Class* clazz = static_cast<Class*>(propertyType);
                    collectPropertyTypes(collectedTypes, clazz->getProperties());
                }
                else if (propertyType->getKind() == Type::Kind::Structure)
                {
                    Structure* structure = static_cast<Structure*>(propertyType);
                    collectPropertyTypes(collectedTypes, structure->getProperties());
                }
            }
        }
    }

    void Reflection::collectTypes()
    {
        static bool collected = false;
        if (collected)
            return;

        std::unordered_set<Type*> collectedTypes;
        std::unordered_set<Class*> collectedClasses;
        TypeIdMap& typeIdMap = getTypeIdMap();

        // collect class types
        for (auto& [id, type] : typeIdMap)
        {
            assert(type->getKind() == Type::Kind::Class);
            Class* clazz = static_cast<Class*>(type);
            collectedClasses.emplace(clazz);
            collectedTypes.emplace(type);
        }

        for (Class* clazz : collectedClasses)
        {
            collectPropertyTypes(collectedTypes, clazz->getProperties());
        }

        collected = true;
    }

}
