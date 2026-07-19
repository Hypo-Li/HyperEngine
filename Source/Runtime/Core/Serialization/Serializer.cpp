#include "Serializer.h"

namespace hyp
{
    Serializer::Serializer(Archive& archive, SerializeContext& context) :
        mArchive(archive),
        mContext(context)
    {
    }

    void Serializer::serializeObject(Object*& object)
    {
        serializeClass(nullptr, &object, 1);

        // 唯一入口收尾:整棵树(mExportTable)都已经读完、每个对象自己的 postLoad
        // 也都调过了,这里统一批量调 finalizeLoad——顺序保证见 SerializeContext::finalizeLoad
        if (mArchive.isLoading())
            mContext.finalizeLoad();
    }

    void Serializer::serializeType(Type* type, void* instance, size_t count)
    {
        switch (type->getKind())
        {
        case Type::Kind::Fundamental:
            serializeFundamental(static_cast<Fundamental*>(type), instance, count);
            break;
        case Type::Kind::Enumeration:
            serializeEnumeration(static_cast<Enumeration*>(type), instance, count);
            break;
        case Type::Kind::Structure:
            serializeStructure(static_cast<Structure*>(type), instance, count);
            break;
        case Type::Kind::Class:
            serializeClass(static_cast<Class*>(type), instance, count);
            break;
        case Type::Kind::Special:
            serializeSpecial(static_cast<Special*>(type), instance, count);
            break;
        }
    }

    void Serializer::serializeFundamental(Fundamental* fundamental, void* instance, size_t count)
    {
        if (fundamental == Reflection::getType<bool>())
            mArchive.serialize((bool*)instance, count);
        else if (fundamental == Reflection::getType<char>())
            mArchive.serialize((char*)instance, count);
        else if (fundamental == Reflection::getType<wchar_t>())
            mArchive.serialize((wchar_t*)instance, count);
        else if (fundamental == Reflection::getType<int8_t>())
            mArchive.serialize((int8_t*)instance, count);
        else if (fundamental == Reflection::getType<int16_t>())
            mArchive.serialize((int16_t*)instance, count);
        else if (fundamental == Reflection::getType<int32_t>())
            mArchive.serialize((int32_t*)instance, count);
        else if (fundamental == Reflection::getType<int64_t>())
            mArchive.serialize((int64_t*)instance, count);
        else if (fundamental == Reflection::getType<uint8_t>())
            mArchive.serialize((uint8_t*)instance, count);
        else if (fundamental == Reflection::getType<uint16_t>())
            mArchive.serialize((uint16_t*)instance, count);
        else if (fundamental == Reflection::getType<uint32_t>())
            mArchive.serialize((uint32_t*)instance, count);
        else if (fundamental == Reflection::getType<uint64_t>())
            mArchive.serialize((uint64_t*)instance, count);
        else if (fundamental == Reflection::getType<float>())
            mArchive.serialize((float*)instance, count);
        else if (fundamental == Reflection::getType<double>())
            mArchive.serialize((double*)instance, count);
    }

    void Serializer::serializeEnumeration(Enumeration* enumeration, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadEnumeration(enumeration, instance, count);
        else
            saveEnumeration(enumeration, instance, count);
    }

    void Serializer::saveEnumeration(Enumeration* enumeration, void* instance, size_t count)
    {
        size_t size = enumeration->getSize();
        uint8_t* enums = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            if (enumeration->isBitMask())
            {
                std::vector<HString> bitNames = enumeration->getFlagNames(enums);
                uint32_t bitCount = static_cast<uint32_t>(bitNames.size());
                mArchive.beginArray(bitCount);
                if (bitCount > 0)
                    mArchive.serialize(bitNames.data(), bitCount);
                mArchive.endArray();
            }
            else
            {
                HString name = enumeration->getNameByValue(enumeration->getValue(enums));
                mArchive.serialize(&name);
            }
            enums += size;
        }
    }

    void Serializer::loadEnumeration(Enumeration* enumeration, void* instance, size_t count)
    {
        size_t size = enumeration->getSize();
        uint8_t* enums = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            if (enumeration->isBitMask())
            {
                uint32_t bitCount = 0;
                mArchive.beginArray(bitCount);
                std::vector<HString> bitNames(bitCount);
                if (bitCount > 0)
                    mArchive.serialize(bitNames.data(), bitCount);
                mArchive.endArray();
                enumeration->setFlagsByNames(enums, bitNames);
            }
            else
            {
                HString name;
                mArchive.serialize(&name);
                enumeration->setValue(enums, enumeration->getValueByName(name));
            }
            enums += size;
        }
    }

    void Serializer::serializeStructure(Structure* structure, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadStructure(structure, instance, count);
        else
            saveStructure(structure, instance, count);
    }

    void Serializer::saveStructure(Structure* structure, void* instance, size_t count)
    {
        uint8_t* structs = static_cast<uint8_t*>(instance);
        size_t size = structure->getSize();
        HString name = structure->getName();
        uint32_t version = structure->getVersion();
        auto properties = structure->getProperties();
        uint32_t propCount = static_cast<uint32_t>(properties.size());
        for (size_t i = 0; i < count; ++i)
        {
            mArchive.beginStructure(name, version, propCount);
            for (Property* property : properties)
            {
                HString propName = property->getName();
                Type* propType = property->getDeclaredType();
                HString propTypeName = propType->getName();
                mArchive.beginProperty(propName, propTypeName);
                void* propPtr = property->getValuePtr(structs);
                serializeType(propType, propPtr);
                mArchive.endProperty();
            }
            mArchive.endStructure();
            structs += size;
        }
    }

    void Serializer::loadStructure(Structure* structure, void* instance, size_t count)
    {
        uint8_t* structs = static_cast<uint8_t*>(instance);
        size_t size = structure->getSize();

        for (size_t i = 0; i < count; ++i)
        {
            HString typeName;
            uint32_t version = 0;
            uint32_t propCount = 0;
            mArchive.beginStructure(typeName, version, propCount);

            for (uint32_t p = 0; p < propCount; ++p)
            {
                HString propName;
                HString propTypeName;
                mArchive.beginProperty(propName, propTypeName);

                Property* property = structure->getProperty(propName);
                Type* declaredType = property ? property->getDeclaredType() : nullptr;
                if (property && declaredType && declaredType->getName() == propTypeName)
                {
                    if (declaredType->getKind() == Type::Kind::Class)
                    {
                        Object* propObject = nullptr;
                        serializeClass(static_cast<Class*>(declaredType), &propObject, 1);
                        property->setValue(structs, &propObject);
                    }
                    else
                    {
                        void* propPtr = property->getValuePtr(structs);
                        serializeType(declaredType, propPtr);
                    }
                }
                mArchive.endProperty();
            }
            
            mArchive.endStructure();
            structs += size;
        }
    }

    void Serializer::serializeClass(Class* clazz, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadClass(clazz, instance, count);
        else
            saveClass(clazz, instance, count);
    }

    void Serializer::saveClass(Class* clazz, void* instance, size_t count)
    {
        Object** objects = static_cast<Object**>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            Object* object = objects[i];
            int32_t id;
            if (object == nullptr)
            {
                id = 0;
                mArchive.serializeObjectRef(id);
                continue;
            }

            bool isNew = false;
            id = mContext.getOrRegisterObject(object, isNew);
            mArchive.serializeObjectRef(id);

            // imported object or not new object
            if (id > 0 || !isNew)
                continue;

            // 逐对象、立即调用:写之前给对象一次机会规整自己的状态,
            // 不需要等其它对象也写完——写盘不涉及重建指针关系,没有这个顾虑
            object->preSave();

            Class* objectClass = object->getClass();
            assert(objectClass != nullptr);
            HString className = objectClass->getName();
            uint32_t version = objectClass->getVersion();

            const Object* defaultObject = objectClass->getDefaultObject();
            std::vector<Property*> diffProperties;
            Class* baseClass = objectClass;
            while (baseClass)
            {
                for (Property* property : baseClass->getProperties())
                {
                    if (!property->compare(defaultObject, object))
                        diffProperties.push_back(property);
                }
                baseClass = baseClass->getBaseClass();
            }

            uint32_t diffPropCount = static_cast<uint32_t>(diffProperties.size());
            mArchive.beginObject(className, version, diffPropCount);
            for (Property* property : diffProperties)
            {
                HString propName = property->getName();
                Type* propType = property->getDeclaredType();
                HString propTypeName = propType->getName();
                mArchive.beginProperty(propName, propTypeName);
                void* propPtr = property->getValuePtr(object);
                serializeType(propType, propPtr);
                mArchive.endProperty();
            }
            mArchive.endObject();

            object->postSave();
        }
    }

    void Serializer::loadClass(Class* clazz, void* instance, size_t count)
    {
        Object** objects = static_cast<Object**>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            int32_t id = 0;
            mArchive.serializeObjectRef(id);
            if (id == 0)
            {
                objects[i] = nullptr;
                continue;
            }

            bool isNew = false;
            objects[i] = mContext.getOrCreateObject(id, isNew);

            if (id > 0 || !isNew)
                continue;

            Class* objectClass = objects[i]->getClass();
            HString className;
            uint32_t version = 0;
            uint32_t diffPropCount = 0;
            mArchive.beginObject(className, version, diffPropCount);
            assert(objects[i]->getClass()->getName() == className);
            for (uint32_t p = 0; p < diffPropCount; ++p)
            {
                HString propName;
                HString propTypeName;
                mArchive.beginProperty(propName, propTypeName);
                Property* property = objectClass->getProperty(propName);
                Type* declaredType = property ? property->getDeclaredType() : nullptr;
                if (property && declaredType && declaredType->getName() == propTypeName)
                {
                    if (declaredType->getKind() == Type::Kind::Class)
                    {
                        Object* propObject = nullptr;
                        serializeClass(static_cast<Class*>(declaredType), &propObject, 1);
                        property->setValue(objects[i], &propObject);
                    }
                    else
                    {
                        void* propPtr = property->getValuePtr(objects[i]);
                        serializeType(declaredType, propPtr);
                    }
                }
                mArchive.endProperty();
            }
            mArchive.endObject();

            // 和 postSave 对称:立即调用,只保证"这个对象自己的数据"有效,
            // 不对其它对象(包括它引用的对象)的状态做任何假设——
            // 那部分留给 finalizeLoad,在整棵树都读完之后统一处理
            objects[i]->postLoad();
        }
    }

    void Serializer::serializeSpecial(Special* special, void* instance, size_t count)
    {
        switch (special->getCase())
        {
        case Special::Case::StdArray:
        {
            serializeStdArray(static_cast<StdArray*>(special), instance, count);
            break;
        }
        case Special::Case::StdList:
        {
            serializeStdList(static_cast<StdList*>(special), instance, count);
            break;
        }
        case Special::Case::StdMap:
        {
            serializeStdMap(static_cast<StdMap*>(special), instance, count);
            break;
        }
        case Special::Case::StdOptional:
        {
            serializeStdOptional(static_cast<StdOptional*>(special), instance, count);
            break;
        }
        case Special::Case::StdPair:
        {
            serializeStdPair(static_cast<StdPair*>(special), instance, count);
            break;
        }
        case Special::Case::StdSet:
        {
            serializeStdSet(static_cast<StdSet*>(special), instance, count);
            break;
        }
        case Special::Case::StdString:
        {
            serializeStdString(static_cast<StdString*>(special), instance, count);
            break;
        }
        case Special::Case::StdUnorderedMap:
        {
            serializeStdUnorderedMap(static_cast<StdUnorderedMap*>(special), instance, count);
            break;
        }
        case Special::Case::StdUnorderedSet:
        {
            serializeStdUnorderedSet(static_cast<StdUnorderedSet*>(special), instance, count);
            break;
        }
        case Special::Case::StdVariant:
        {
            serializeStdVariant(static_cast<StdVariant*>(special), instance, count);
            break;
        }
        case Special::Case::StdVector:
        {
            serializeStdVector(static_cast<StdVector*>(special), instance, count);
            break;
        }
        case Special::Case::HypHString:
        {
            serializeHypHString(static_cast<HypHString*>(special), instance, count);
            break;
        }
        case Special::Case::BulkData:
        {
            serializeBulkData(static_cast<HypBulkData*>(special), instance, count);
            break;
        }
        }
    }

    void Serializer::serializeStdArray(StdArray* stdArray, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadStdArray(stdArray, instance, count);
        else
            saveStdArray(stdArray, instance, count);
    }

    void Serializer::saveStdArray(StdArray* stdArray, void* instance, size_t count)
    {
        Type* elementType = stdArray->getElementType();
        uint32_t elementCount = static_cast<uint32_t>(stdArray->getElementCount());
        uint8_t* array = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            mArchive.beginArray(elementCount);
            serializeType(elementType, stdArray->getElementPtr(array, 0), elementCount);
            mArchive.endArray();
            array += stdArray->getSize();
        }
    }

    void Serializer::loadStdArray(StdArray* stdArray, void* instance, size_t count)
    {
        Type* elementType = stdArray->getElementType();
        uint8_t* array = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t elementCount = 0;
            mArchive.beginArray(elementCount);
            assert(elementCount == stdArray->getElementCount());
            if (elementType->getKind() == Type::Kind::Class)
            {
                std::vector<Object*> objects(elementCount);
                serializeClass(static_cast<Class*>(elementType), objects.data(), elementCount);
                for (uint32_t j = 0; j < elementCount; ++j)
                    stdArray->setElement(array, j, &objects[j]);
            }
            else
            {
                serializeType(elementType, stdArray->getElementPtr(array, 0), elementCount);
            }
            mArchive.endArray();
            array += stdArray->getSize();
        }
    }

    void Serializer::serializeStdList(StdList* stdList, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadStdList(stdList, instance, count);
        else
            saveStdList(stdList, instance, count);
    }

    void Serializer::saveStdList(StdList* stdList, void* instance, size_t count)
    {
        Type* elementType = stdList->getElementType();
        uint8_t* list = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t elementCount = static_cast<uint32_t>(stdList->getElementCount(list));
            mArchive.beginArray(elementCount);
            if (elementCount > 0)
            {
                std::vector<void*> elementPtrs = stdList->getElementPtrs(list);
                for (uint32_t j = 0; j < elementCount; ++j)
                    serializeType(elementType, elementPtrs[j]);
            }
            mArchive.endArray();
            list += stdList->getSize();
        }
    }

    void Serializer::loadStdList(StdList* stdList, void* instance, size_t count)
    {
        Type* elementType = stdList->getElementType();
        size_t elementSize = elementType->getKind() == Type::Kind::Class ?
            sizeof(Object*) : elementType->getSize();
        uint8_t* list = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t elementCount = 0;
            mArchive.beginArray(elementCount);
            if (elementCount > 0)
            {
                std::vector<uint8_t> elements(elementCount * elementSize, 0);
                if (elementType->getKind() != Type::Kind::Class)
                {
                    for (uint32_t j = 0; j < elementCount; ++j)
                    {
                        elementType->construct(elements.data() + j * elementSize);
                    }
                }
                serializeType(elementType, elements.data(), elementCount);
                for (uint32_t j = 0; j < elementCount; ++j)
                {
                    void* element = elements.data() + j * elementSize;
                    stdList->addElement(list, element);
                    if (elementType->getKind() != Type::Kind::Class)
                        elementType->destruct(element);
                }
            }
            mArchive.endArray();
            list += stdList->getSize();
        }
    }

    void Serializer::serializeStdMap(StdMap* stdMap, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadStdMap(stdMap, instance, count);
        else
            saveStdMap(stdMap, instance, count);
    }

    void Serializer::saveStdMap(StdMap* stdMap, void* instance, size_t count)
    {
        Type* keyType = stdMap->getKeyType();
        Type* valueType = stdMap->getValueType();
        uint8_t* map = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t keyValueCount = static_cast<uint32_t>(stdMap->getKeyValuePairCount(map));
            mArchive.beginArray(keyValueCount);
            if (keyValueCount > 0)
            {
                std::vector<const void*> keys = stdMap->getKeyPtrs(map);
                std::vector<void*> values = stdMap->getValuePtrs(map);
                for (uint32_t j = 0; j < keyValueCount; ++j)
                {
                    mArchive.beginPair();
                    serializeType(keyType, const_cast<void*>(keys[j]));
                    serializeType(valueType, values[j]);
                    mArchive.endPair();
                }
            }
            mArchive.endArray();
            map += stdMap->getSize();
        }
    }

    void Serializer::loadStdMap(StdMap* stdMap, void* instance, size_t count)
    {
        Type* keyType = stdMap->getKeyType();
        size_t keySize = keyType->getKind() == Type::Kind::Class ?
            sizeof(Object*) : keyType->getSize();
        Type* valueType = stdMap->getValueType();
        size_t valueSize = valueType->getKind() == Type::Kind::Class ?
            sizeof(Object*) : valueType->getSize();
        uint8_t* map = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t keyValueCount = 0;
            mArchive.beginArray(keyValueCount);

            if (keyValueCount > 0)
            {
                std::vector<uint8_t> keys(keyValueCount * keySize, 0);
                if (keyType->getKind() != Type::Kind::Class)
                {
                    for (uint32_t j = 0; j < keyValueCount; ++j)
                    {
                        keyType->construct(keys.data() + j * keySize);
                    }
                }
                std::vector<uint8_t> values(keyValueCount * valueSize, 0);
                if (valueType->getKind() != Type::Kind::Class)
                {
                    for (uint32_t j = 0; j < keyValueCount; ++j)
                    {
                        valueType->construct(values.data() + j * valueSize);
                    }
                }

                for (uint32_t j = 0; j < keyValueCount; ++j)
                {
                    mArchive.beginPair();
                    void* key = keys.data() + j * keySize;
                    serializeType(keyType, key);
                    void* value = values.data() + j * valueSize;
                    serializeType(valueType, value);
                    mArchive.endPair();
                    stdMap->insertKeyValuePair(map, key, value);
                    if (keyType->getKind() != Type::Kind::Class)
                        keyType->destruct(key);
                    if (valueType->getKind() != Type::Kind::Class)
                        valueType->destruct(value);
                }
            }

            mArchive.endArray();
            map += stdMap->getSize();
        }
    }

    void Serializer::serializeStdOptional(StdOptional* stdOptional, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadStdOptional(stdOptional, instance, count);
        else
            saveStdOptional(stdOptional, instance, count);
    }

    void Serializer::saveStdOptional(StdOptional* stdOptional, void* instance, size_t count)
    {
        Type* valueType = stdOptional->getValueType();
        uint8_t* optionals = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            bool hasValue = stdOptional->hasValue(optionals);
            uint32_t elementCount = hasValue ? 1 : 0;
            mArchive.beginArray(elementCount);
            if (hasValue)
                serializeType(valueType, stdOptional->getValuePtr(optionals));
            mArchive.endArray();
            optionals += stdOptional->getSize();
        }
    }

    void Serializer::loadStdOptional(StdOptional* stdOptional, void* instance, size_t count)
    {
        Type* valueType = stdOptional->getValueType();
        uint8_t* optionals = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t elementCount = 0;
            mArchive.beginArray(elementCount);
            assert(elementCount <= 1);
            if (elementCount == 1)
            {
                if (valueType->getKind() == Type::Kind::Class)
                {
                    Object* object = nullptr;
                    serializeClass(static_cast<Class*>(valueType), &object, 1);
                    stdOptional->setValue(optionals, &object);
                }
                else
                {
                    std::vector<uint8_t> value(valueType->getSize(), 0);
                    valueType->construct(value.data());
                    serializeType(valueType, value.data());
                    stdOptional->setValue(optionals, value.data());
                    valueType->destruct(value.data());
                }
            }
            else
            {
                stdOptional->reset(optionals);
            }
            mArchive.endArray();
            optionals += stdOptional->getSize();
        }
    }

    void Serializer::serializeStdPair(StdPair* stdPair, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadStdPair(stdPair, instance, count);
        else
            saveStdPair(stdPair, instance, count);
    }

    void Serializer::saveStdPair(StdPair* stdPair, void* instance, size_t count)
    {
        Type* firstType = stdPair->getFirstType();
        Type* secondType = stdPair->getSecondType();
        uint8_t* pair = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            mArchive.beginPair();
            serializeType(firstType, stdPair->getFirstPtr(pair));
            serializeType(secondType, stdPair->getSecondPtr(pair));
            mArchive.endPair();
            pair += stdPair->getSize();
        }
    }

    void Serializer::loadStdPair(StdPair* stdPair, void* instance, size_t count)
    {
        Type* firstType = stdPair->getFirstType();
        Type* secondType = stdPair->getSecondType();
        uint8_t* pair = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            mArchive.beginPair();
            if (firstType->getKind() == Type::Kind::Class)
            {
                Object* object = nullptr;
                serializeClass(static_cast<Class*>(firstType), &object, 1);
                stdPair->setFirstValue(pair, &object);
            }
            else
            {
                serializeType(firstType, stdPair->getFirstPtr(pair));
            }
            if (secondType->getKind() == Type::Kind::Class)
            {
                Object* object = nullptr;
                serializeClass(static_cast<Class*>(secondType), &object, 1);
                stdPair->setSecondValue(pair, &object);
            }
            else
            {
                serializeType(secondType, stdPair->getSecondPtr(pair));
            }
            mArchive.endPair();
            pair += stdPair->getSize();
        }
    }

    void Serializer::serializeStdSet(StdSet* stdSet, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadStdSet(stdSet, instance, count);
        else
            saveStdSet(stdSet, instance, count);
    }

    void Serializer::saveStdSet(StdSet* stdSet, void* instance, size_t count)
    {
        Type* elementType = stdSet->getElementType();
        uint8_t* set = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t elementCount = static_cast<uint32_t>(stdSet->getElementCount(set));
            mArchive.beginArray(elementCount);
            if (elementCount > 0)
            {
                std::vector<const void*> elements = stdSet->getElementPtrs(set);
                for (uint32_t j = 0; j < elementCount; ++j)
                {
                    serializeType(elementType, const_cast<void*>(elements[j]));
                }
            }
            mArchive.endArray();
            set += stdSet->getSize();
        }
    }

    void Serializer::loadStdSet(StdSet* stdSet, void* instance, size_t count)
    {
        Type* elementType = stdSet->getElementType();
        size_t elementSize = elementType->getKind() == Type::Kind::Class ?
            sizeof(Object*) : elementType->getSize();
        uint8_t* set = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t elementCount = 0;
            mArchive.beginArray(elementCount);
            if (elementCount > 0)
            {
                std::vector<uint8_t> elements(elementCount * elementSize, 0);
                if (elementType->getKind() != Type::Kind::Class)
                {
                    for (uint32_t j = 0; j < elementCount; ++j)
                    {
                        elementType->construct(elements.data() + j * elementSize);
                    }
                }
                serializeType(elementType, elements.data(), elementCount);
                for (uint32_t j = 0; j < elementCount; ++j)
                {
                    void* element = elements.data() + j * elementSize;
                    stdSet->insertElement(set, element);
                    if (elementType->getKind() != Type::Kind::Class)
                        elementType->destruct(element);
                }
            }
            mArchive.endArray();
            set += stdSet->getSize();
        }
    }

    void Serializer::serializeStdString(StdString* stdString, void* instance, size_t count)
    {
        mArchive.serialize(static_cast<std::string*>(instance), count);
    }

    void Serializer::serializeStdUnorderedMap(StdUnorderedMap* stdUnorderedMap, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadStdUnorderedMap(stdUnorderedMap, instance, count);
        else
            saveStdUnorderedMap(stdUnorderedMap, instance, count);
    }

    void Serializer::saveStdUnorderedMap(StdUnorderedMap* stdUnorderedMap, void* instance, size_t count)
    {
        Type* keyType = stdUnorderedMap->getKeyType();
        Type* valueType = stdUnorderedMap->getValueType();
        uint8_t* map = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t keyValueCount = static_cast<uint32_t>(stdUnorderedMap->getKeyValuePairCount(map));
            mArchive.beginArray(keyValueCount);
            if (keyValueCount > 0)
            {
                std::vector<const void*> keys = stdUnorderedMap->getKeyPtrs(map);
                std::vector<void*> values = stdUnorderedMap->getValuePtrs(map);
                for (uint32_t j = 0; j < keyValueCount; ++j)
                {
                    mArchive.beginPair();
                    serializeType(keyType, const_cast<void*>(keys[j]));
                    serializeType(valueType, values[j]);
                    mArchive.endPair();
                }
            }
            mArchive.endArray();
            map += stdUnorderedMap->getSize();
        }
    }

    void Serializer::loadStdUnorderedMap(StdUnorderedMap* stdUnorderedMap, void* instance, size_t count)
    {
        Type* keyType = stdUnorderedMap->getKeyType();
        size_t keySize = keyType->getKind() == Type::Kind::Class ?
            sizeof(Object*) : keyType->getSize();
        Type* valueType = stdUnorderedMap->getValueType();
        size_t valueSize = valueType->getKind() == Type::Kind::Class ?
            sizeof(Object*) : valueType->getSize();
        uint8_t* map = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t keyValueCount = 0;
            mArchive.beginArray(keyValueCount);

            if (keyValueCount > 0)
            {
                std::vector<uint8_t> keys(keyValueCount * keySize, 0);
                if (keyType->getKind() != Type::Kind::Class)
                {
                    for (uint32_t j = 0; j < keyValueCount; ++j)
                        keyType->construct(keys.data() + j * keySize);
                }
                std::vector<uint8_t> values(keyValueCount * valueSize, 0);
                if (valueType->getKind() != Type::Kind::Class)
                {
                    for (uint32_t j = 0; j < keyValueCount; ++j)
                        valueType->construct(values.data() + j * valueSize);
                }

                for (uint32_t j = 0; j < keyValueCount; ++j)
                {
                    mArchive.beginPair();
                    void* key = keys.data() + j * keySize;
                    serializeType(keyType, key);
                    void* value = values.data() + j * valueSize;
                    serializeType(valueType, value);
                    mArchive.endPair();
                    stdUnorderedMap->insertKeyValuePair(map, key, value);
                    if (keyType->getKind() != Type::Kind::Class)
                        keyType->destruct(key);
                    if (valueType->getKind() != Type::Kind::Class)
                        valueType->destruct(value);
                }
            }

            mArchive.endArray();
            map += stdUnorderedMap->getSize();
        }
    }

    void Serializer::serializeStdUnorderedSet(StdUnorderedSet* stdUnorderedSet, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadStdUnorderedSet(stdUnorderedSet, instance, count);
        else
            saveStdUnorderedSet(stdUnorderedSet, instance, count);
    }

    void Serializer::saveStdUnorderedSet(StdUnorderedSet* stdUnorderedSet, void* instance, size_t count)
    {
        Type* elementType = stdUnorderedSet->getElementType();
        uint8_t* set = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t elementCount = static_cast<uint32_t>(stdUnorderedSet->getElementCount(set));
            mArchive.beginArray(elementCount);
            if (elementCount > 0)
            {
                std::vector<const void*> elements = stdUnorderedSet->getElementPtrs(set);
                for (uint32_t j = 0; j < elementCount; ++j)
                {
                    serializeType(elementType, const_cast<void*>(elements[j]));
                }
            }
            mArchive.endArray();
            set += stdUnorderedSet->getSize();
        }
    }

    void Serializer::loadStdUnorderedSet(StdUnorderedSet* stdUnorderedSet, void* instance, size_t count)
    {
        Type* elementType = stdUnorderedSet->getElementType();
        size_t elementSize = elementType->getKind() == Type::Kind::Class ?
            sizeof(Object*) : elementType->getSize();
        uint8_t* set = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t elementCount = 0;
            mArchive.beginArray(elementCount);
            if (elementCount > 0)
            {
                std::vector<uint8_t> elements(elementCount * elementSize, 0);
                if (elementType->getKind() != Type::Kind::Class)
                {
                    for (uint32_t j = 0; j < elementCount; ++j)
                        elementType->construct(elements.data() + j * elementSize);
                }
                serializeType(elementType, elements.data(), elementCount);
                for (uint32_t j = 0; j < elementCount; ++j)
                {
                    void* element = elements.data() + j * elementSize;
                    stdUnorderedSet->insertElement(set, element);
                    if (elementType->getKind() != Type::Kind::Class)
                        elementType->destruct(element);
                }
            }
            mArchive.endArray();
            set += stdUnorderedSet->getSize();
        }
    }

    void Serializer::serializeStdVariant(StdVariant* stdVariant, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadStdVariant(stdVariant, instance, count);
        else
            saveStdVariant(stdVariant, instance, count);
    }

    void Serializer::saveStdVariant(StdVariant* stdVariant, void* instance, size_t count)
    {
        uint8_t* variant = static_cast<uint8_t*>(instance);
        auto types = stdVariant->getTypes();
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t typeIndex = static_cast<uint32_t>(stdVariant->getTypeIndex(variant));
            mArchive.beginPair();
            mArchive.serialize(&typeIndex);
            serializeType(types[typeIndex], stdVariant->getValuePtr(variant));
            mArchive.endPair();
            variant += stdVariant->getSize();
        }
    }

    void Serializer::loadStdVariant(StdVariant* stdVariant, void* instance, size_t count)
    {
        uint8_t* variant = static_cast<uint8_t*>(instance);
        auto types = stdVariant->getTypes();
        for (size_t i = 0; i < count; ++i)
        {
            mArchive.beginPair();
            uint32_t typeIndex = 0;
            mArchive.serialize(&typeIndex);
            assert(typeIndex < types.size());
            Type* type = types[typeIndex];
            if (type->getKind() == Type::Kind::Class)
            {
                Object* object = nullptr;
                serializeClass(static_cast<Class*>(type), &object, 1);
                stdVariant->setValue(variant, typeIndex, &object);
            }
            else
            {
                std::vector<uint8_t> value(type->getSize(), 0);
                type->construct(value.data());
                serializeType(type, value.data());
                stdVariant->setValue(variant, typeIndex, value.data());
                type->destruct(value.data());
            }
            mArchive.endPair();
            variant += stdVariant->getSize();
        }
    }

    void Serializer::serializeStdVector(StdVector* stdVector, void* instance, size_t count)
    {
        if (mArchive.isLoading())
            loadStdVector(stdVector, instance, count);
        else
            saveStdVector(stdVector, instance, count);
    }

    void Serializer::saveStdVector(StdVector* stdVector, void* instance, size_t count)
    {
        Type* elementType = stdVector->getElementType();
        uint8_t* vector = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t elementCount = static_cast<uint32_t>(stdVector->getElementCount(vector));
            mArchive.beginArray(elementCount);
            if (elementCount > 0)
            {
                serializeType(elementType, stdVector->getElementPtr(vector, 0), elementCount);
            }
            mArchive.endArray();
            vector += stdVector->getSize();
        }
    }

    void Serializer::loadStdVector(StdVector* stdVector, void* instance, size_t count)
    {
        Type* elementType = stdVector->getElementType();
        size_t elementSize = elementType->getKind() == Type::Kind::Class ?
            sizeof(Object*) : elementType->getSize();
        uint8_t* vector = static_cast<uint8_t*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t elementCount = 0;
            mArchive.beginArray(elementCount);
            if (elementCount > 0)
            {
                stdVector->resize(vector, elementCount);
                if (elementType->getKind() == Type::Kind::Class)
                {
                    for (uint32_t j = 0; j < elementCount; ++j)
                    {
                        Object* object = nullptr;
                        serializeClass(static_cast<Class*>(elementType), &object, 1);
                        stdVector->setElement(vector, j, &object);
                    }
                }
                else
                {
                    serializeType(elementType, stdVector->getElementPtr(vector, 0), elementCount);
                }
            }
            mArchive.endArray();
            vector += stdVector->getSize();
        }
    }

    void Serializer::serializeHypHString(HypHString* hypHString, void* instance, size_t count)
    {
        mArchive.serialize(static_cast<HString*>(instance), count);
    }

    void Serializer::serializeBulkData(HypBulkData* hypBulkData, void* instance, size_t count)
    {
        BulkData* bulkDataArray = static_cast<BulkData*>(instance);
        for (size_t i = 0; i < count; ++i)
        {
            BulkData& bulkData = bulkDataArray[i];
            if (mArchive.isLoading())
            {
                uint64_t size = 0;
                mArchive.serialize(&size);
                uint64_t offset = 0;
                if (size > 0)
                    mArchive.serialize(&offset);
                bulkData.bindSource(mContext.getBulkDataSource(), offset, size);
            }
            else
            {
                bool wasTemporarilyLoaded = false;
                std::span<const uint8_t> data = bulkData.acquireForSave(wasTemporarilyLoaded);
                uint64_t size = static_cast<uint64_t>(data.size());
                mArchive.serialize(&size);
                if (size > 0)
                {
                    uint64_t offset = mContext.appendBulkSection(data.data(), size);
                    mArchive.serialize(&offset);
                    mContext.recordBulkDataSave(&bulkData, offset, size, wasTemporarilyLoaded);
                }
            }
        }
    }

}
