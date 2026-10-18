#pragma once
#include "Archive.h"
#include "SerializeContext.h"

namespace hyp
{
    class Serializer
    {
    public:
        Serializer(Archive& archive, SerializeContext& context);

        void serializeObject(Object*& object);

    private:
        void serializeType(Type* type, void* instance, size_t count = 1);

        // Cook_Only/Editor_Only 这两个 flag(见 Property.h)决定某个属性这次写入要不要包含进去,
        // 只在保存时用得到, 加载端不需要对应逻辑(见 Archive::isCooking() 的注释)
        bool shouldSerializeProperty(const Property* property) const;

        void serializeFundamental(Fundamental* fundamental, void* instance, size_t count);

        void serializeEnumeration(Enumeration* enumeration, void* instance, size_t count);

        void saveEnumeration(Enumeration* enumeration, void* instance, size_t count);

        void loadEnumeration(Enumeration* enumeration, void* instance, size_t count);

        // cook 路径: 按枚举底层原始整数值读写, 位标志/普通枚举统一处理, 不走名字查找
        void saveEnumerationCooked(Enumeration* enumeration, void* enums);

        void loadEnumerationCooked(Enumeration* enumeration, void* enums);

        void serializeStructure(Structure* structure, void* instance, size_t count);

        void saveStructure(Structure* structure, void* instance, size_t count);

        void loadStructure(Structure* structure, void* instance, size_t count);

        void saveStructureCooked(Structure* structure, void* instance);

        void loadStructureCooked(Structure* structure, void* instance);

        void serializeClass(Class* clazz, void* instance, size_t count);

        void saveClass(Class* clazz, void* instance, size_t count);

        void loadClass(Class* clazz, void* instance, size_t count);

        void saveClassCooked(Class* clazz, Object* object);

        void loadClassCooked(Class* clazz, Object* object);

        // cook 路径下参与序列化的属性集合, 按 shouldSerializeProperty 过滤后固定顺序返回
        // (Class 含 base class 链, 与 saveClass/loadClass 现有的遍历顺序一致)——
        // save/load 必须调用同一个函数取得同一个顺序, presence bitmask 才能对号入座
        std::vector<Property*> collectCookedProperties(Class* clazz) const;

        std::vector<Property*> collectCookedProperties(Structure* structure) const;

        void serializeSpecial(Special* special, void* instance, size_t count);

        void serializeStdArray(StdArray* stdArray, void* instance, size_t count);

        void saveStdArray(StdArray* stdArray, void* instance, size_t count);

        void loadStdArray(StdArray* stdArray, void* instance, size_t count);

        void serializeStdList(StdList* stdList, void* instance, size_t count);

        void saveStdList(StdList* stdList, void* instance, size_t count);

        void loadStdList(StdList* stdList, void* instance, size_t count);

        void serializeStdMap(StdMap* stdMap, void* instance, size_t count);

        void saveStdMap(StdMap* stdMap, void* instance, size_t count);

        void loadStdMap(StdMap* stdMap, void* instance, size_t count);

        void serializeStdOptional(StdOptional* stdOptional, void* instance, size_t count);

        void saveStdOptional(StdOptional* stdOptional, void* instance, size_t count);

        void loadStdOptional(StdOptional* stdOptional, void* instance, size_t count);

        void serializeStdPair(StdPair* stdPair, void* instance, size_t count);

        void saveStdPair(StdPair* stdPair, void* instance, size_t count);

        void loadStdPair(StdPair* stdPair, void* instance, size_t count);

        void serializeStdSet(StdSet* stdSet, void* instance, size_t count);

        void saveStdSet(StdSet* stdSet, void* instance, size_t count);

        void loadStdSet(StdSet* stdSet, void* instance, size_t count);

        void serializeStdString(StdString* stdString, void* instance, size_t count);

        void serializeStdUnorderedMap(StdUnorderedMap* stdUnorderedMap, void* instance, size_t count);

        void saveStdUnorderedMap(StdUnorderedMap* stdUnorderedMap, void* instance, size_t count);

        void loadStdUnorderedMap(StdUnorderedMap* stdUnorderedMap, void* instance, size_t count);

        void serializeStdUnorderedSet(StdUnorderedSet* stdUnorderedSet, void* instance, size_t count);

        void saveStdUnorderedSet(StdUnorderedSet* stdUnorderedSet, void* instance, size_t count);

        void loadStdUnorderedSet(StdUnorderedSet* stdUnorderedSet, void* instance, size_t count);

        void serializeStdVariant(StdVariant* stdVariant, void* instance, size_t count);

        void saveStdVariant(StdVariant* stdVariant, void* instance, size_t count);

        void loadStdVariant(StdVariant* stdVariant, void* instance, size_t count);

        void serializeStdVector(StdVector* stdVector, void* instance, size_t count);

        void saveStdVector(StdVector* stdVector, void* instance, size_t count);

        void loadStdVector(StdVector* stdVector, void* instance, size_t count);

        void serializeHypHString(HypHString* hypHString, void* instance, size_t count);

        void serializeBulkData(HypBulkData* hypBulkData, void* instance, size_t count);

        void serializeHypSoftRef(HypSoftRef* hypSoftRef, void* instance, size_t count);

    private:
        Archive& mArchive;
        SerializeContext& mContext;
    };
}
