#include "Copier.h"

#include <cassert>

namespace hyp
{
    Object* Copier::copy(Object* original, Object* newOwner)
    {
        if (!original)
            return nullptr;

        scan(original, newOwner);
        return mContext.findCopy(original);
    }

    void Copier::finalizeAll()
    {
        // takeNewlyAllocated() 只应该在这里调一次——只要 finalizeAll() 本身只调一次(它的
        // 公开约定), 这一份就是本次操作分配过的全部对象, 用同一份列表先 populate 一遍、
        // 再 finalizeCopy() 一遍。
        std::span<Object* const> allocated = mContext.takeNewlyAllocated();

        for (Object* originalObject : allocated)
            populate(originalObject, mContext.findCopy(originalObject));

        for (Object* originalObject : allocated)
            mContext.findCopy(originalObject)->finalizeCopy();
    }

    void Copier::scan(Object* original, Object* newOwner)
    {
        bool isNew = false;
        Object* copy = mContext.getOrCreateCopy(original, newOwner, isNew);
        if (!isNew)
            return; // 本次操作里已经发现过(环引用/共享引用), 不再往下扫

        for (Property* property : collectAllProperties(original->getClass()))
        {
            CopyPolicy policy = property->getCopyPolicy();
            if (policy == CopyPolicy::Ignore)
                continue;

            scanValue(property->getDeclaredType(), policy, property->getValuePtr(original), copy);
        }
    }

    void Copier::scanValue(Type* type, CopyPolicy policy, const void* src, Object* newOwner)
    {
        if (type->getKind() == Type::Kind::Class)
        {
            if (policy != CopyPolicy::Deep)
                return; // Shallow 不需要在扫描阶段分配任何东西, 填充阶段查表重定向即可

            Object* target = *static_cast<Object* const*>(src);
            if (target)
                scan(target, newOwner);
            return;
        }

        // 只有 Deep 才需要继续往容器/结构体内部发现更多待分配的对象——Shallow 整份值在
        // 填充阶段按元素查表即可, 不需要提前分配
        if (policy != CopyPolicy::Deep)
            return;

        if (type->getKind() == Type::Kind::Structure)
        {
            Structure* structure = static_cast<Structure*>(type);
            for (Property* property : structure->getProperties())
            {
                if (property->getCopyPolicy() == CopyPolicy::Ignore)
                    continue;
                scanValue(property->getDeclaredType(), property->getCopyPolicy(), property->getValuePtr(src), newOwner);
            }
            return;
        }

        if (type->getKind() != Type::Kind::Special)
            return;

        Special* special = static_cast<Special*>(type);
        switch (special->getCase())
        {
        case Special::Case::StdArray:
        {
            StdArray* array = static_cast<StdArray*>(special);
            Type* elementType = array->getElementType();
            if (!containsObjectReference(elementType))
                break;
            size_t count = array->getElementCount();
            for (size_t i = 0; i < count; ++i)
                scanValue(elementType, policy, array->getElementPtr(src, i), newOwner);
            break;
        }
        case Special::Case::StdList:
        {
            StdList* list = static_cast<StdList*>(special);
            Type* elementType = list->getElementType();
            if (!containsObjectReference(elementType))
                break;
            for (const void* elementPtr : list->getElementPtrs(src))
                scanValue(elementType, policy, elementPtr, newOwner);
            break;
        }
        case Special::Case::StdSet:
        {
            StdSet* set = static_cast<StdSet*>(special);
            Type* elementType = set->getElementType();
            if (!containsObjectReference(elementType))
                break;
            for (const void* elementPtr : set->getElementPtrs(src))
                scanValue(elementType, policy, elementPtr, newOwner);
            break;
        }
        case Special::Case::StdUnorderedSet:
        {
            StdUnorderedSet* set = static_cast<StdUnorderedSet*>(special);
            Type* elementType = set->getElementType();
            if (!containsObjectReference(elementType))
                break;
            for (const void* elementPtr : set->getElementPtrs(src))
                scanValue(elementType, policy, elementPtr, newOwner);
            break;
        }
        case Special::Case::StdVector:
        {
            StdVector* vector = static_cast<StdVector*>(special);
            Type* elementType = vector->getElementType();
            if (!containsObjectReference(elementType))
                break;
            size_t count = vector->getElementCount(src);
            for (size_t i = 0; i < count; ++i)
                scanValue(elementType, policy, vector->getElementPtr(src, i), newOwner);
            break;
        }
        case Special::Case::StdOptional:
        {
            StdOptional* optional = static_cast<StdOptional*>(special);
            Type* valueType = optional->getValueType();
            if (containsObjectReference(valueType) && optional->hasValue(src))
                scanValue(valueType, policy, optional->getValuePtr(src), newOwner);
            break;
        }
        case Special::Case::StdMap:
        {
            // key/value 两侧独立判断、独立递归——同一个属性的 policy 统一套用到两侧各自能
            // 找到的 Object 引用上, 两侧都含引用也按同一策略各自处理, 不是二选一。
            StdMap* map = static_cast<StdMap*>(special);
            Type* keyType = map->getKeyType();
            Type* valueType = map->getValueType();
            if (containsObjectReference(keyType))
            {
                for (const void* keyPtr : map->getKeyPtrs(src))
                    scanValue(keyType, policy, keyPtr, newOwner);
            }
            if (containsObjectReference(valueType))
            {
                for (const void* valuePtr : map->getValuePtrs(src))
                    scanValue(valueType, policy, valuePtr, newOwner);
            }
            break;
        }
        case Special::Case::StdUnorderedMap:
        {
            StdUnorderedMap* map = static_cast<StdUnorderedMap*>(special);
            Type* keyType = map->getKeyType();
            Type* valueType = map->getValueType();
            if (containsObjectReference(keyType))
            {
                for (const void* keyPtr : map->getKeyPtrs(src))
                    scanValue(keyType, policy, keyPtr, newOwner);
            }
            if (containsObjectReference(valueType))
            {
                for (const void* valuePtr : map->getValuePtrs(src))
                    scanValue(valueType, policy, valuePtr, newOwner);
            }
            break;
        }
        case Special::Case::StdPair:
        {
            StdPair* pair = static_cast<StdPair*>(special);
            Type* firstType = pair->getFirstType();
            Type* secondType = pair->getSecondType();
            if (containsObjectReference(firstType))
                scanValue(firstType, policy, pair->getFirstPtr(src), newOwner);
            if (containsObjectReference(secondType))
                scanValue(secondType, policy, pair->getSecondPtr(src), newOwner);
            break;
        }
        case Special::Case::StdVariant:
        {
            StdVariant* variant = static_cast<StdVariant*>(special);
            std::vector<Type*> types = variant->getTypes();
            size_t activeIndex = variant->getTypeIndex(src);
            Type* activeType = types[activeIndex];
            if (containsObjectReference(activeType))
                scanValue(activeType, policy, variant->getValuePtr(src), newOwner);
            break;
        }
        case Special::Case::StdString:
        case Special::Case::HypHString:
        case Special::Case::BulkData:
            break; // 叶子类型, 不含 Object 引用
        case Special::Case::HypSoftRef:
            // 软引用永远不拥有目标, 不需要在扫描阶段为目标分配副本——即使属性声明的
            // policy 是 Deep, 填充阶段也只按 Shallow 语义处理(见 copyContainerContent)
            break;
        }
    }

    void Copier::populate(Object* original, Object* copy)
    {
        for (Property* property : collectAllProperties(original->getClass()))
        {
            if (property->getCopyPolicy() == CopyPolicy::Ignore)
                continue;
            copyProperty(property, original, copy, copy);
        }

        // 和 postLoad 对称: 只保证"这个对象自己的数据"有效, 不对其它对象(包括它引用的对象)
        // 的状态做任何假设——那部分留给 finalizeCopy, 等整批副本都填充完之后统一处理
        copy->postCopy();
    }

    void Copier::copyProperty(Property* property, const void* srcInstance, void* dstInstance, Object* newOwner)
    {
        Type* declaredType = property->getDeclaredType();

        if (!containsObjectReference(declaredType))
        {
            // 不含任何 Object 引用: 用真实的 C++ 赋值语义整体拷贝一次——vector/HString/
            // 结构体的 operator= 已经会正确处理内部堆内存, 不需要逐层手动搬
            property->setRawValue(dstInstance, property->getValuePtr(srcInstance));
            return;
        }

        if (declaredType->getKind() == Type::Kind::Class)
        {
            Object* originalRef = *static_cast<Object* const*>(property->getValuePtr(srcInstance));
            Object* resolvedRef = resolveReference(originalRef, property->getCopyPolicy(), newOwner);
            property->setRawValue(dstInstance, &resolvedRef);
            return;
        }

        // Structure 或者含 Object 引用的容器: 下探处理, 目标位置是这个属性在 dstInstance
        // 里的实际存储
        copyContainerContent(
            declaredType,
            property->getCopyPolicy(),
            property->getValuePtr(srcInstance),
            property->getValuePtr(dstInstance),
            newOwner);
    }

    void Copier::copyContainerContent(Type* type, CopyPolicy policy, const void* src, void* dst, Object* newOwner)
    {
        if (type->getKind() == Type::Kind::Structure)
        {
            Structure* structure = static_cast<Structure*>(type);
            for (Property* property : structure->getProperties())
            {
                if (property->getCopyPolicy() == CopyPolicy::Ignore)
                    continue;
                copyProperty(property, src, dst, newOwner);
            }
            return;
        }

        assert(type->getKind() == Type::Kind::Special &&
            "Copier::copyContainerContent: 只有 Structure/Special 会走到这里, Class 在 "
            "copyProperty 里已经单独处理过了");

        Special* special = static_cast<Special*>(type);
        switch (special->getCase())
        {
        case Special::Case::StdArray:
        {
            StdArray* array = static_cast<StdArray*>(special);
            Type* elementType = array->getElementType();
            size_t count = array->getElementCount();
            for (size_t i = 0; i < count; ++i)
            {
                copySlot(elementType, policy, array->getElementPtr(src, i), newOwner,
                    [&](const void* value) { array->setElement(dst, i, value); });
            }
            break;
        }
        case Special::Case::StdList:
        {
            StdList* list = static_cast<StdList*>(special);
            Type* elementType = list->getElementType();
            for (const void* elementPtr : list->getElementPtrs(src))
            {
                copySlot(elementType, policy, elementPtr, newOwner,
                    [&](const void* value) { list->addElement(dst, value); });
            }
            break;
        }
        case Special::Case::StdSet:
        {
            StdSet* set = static_cast<StdSet*>(special);
            Type* elementType = set->getElementType();
            for (const void* elementPtr : set->getElementPtrs(src))
            {
                copySlot(elementType, policy, elementPtr, newOwner,
                    [&](const void* value) { set->insertElement(dst, value); });
            }
            break;
        }
        case Special::Case::StdUnorderedSet:
        {
            StdUnorderedSet* set = static_cast<StdUnorderedSet*>(special);
            Type* elementType = set->getElementType();
            for (const void* elementPtr : set->getElementPtrs(src))
            {
                copySlot(elementType, policy, elementPtr, newOwner,
                    [&](const void* value) { set->insertElement(dst, value); });
            }
            break;
        }
        case Special::Case::StdVector:
        {
            StdVector* vector = static_cast<StdVector*>(special);
            Type* elementType = vector->getElementType();
            size_t count = vector->getElementCount(src);
            vector->resize(dst, count);
            for (size_t i = 0; i < count; ++i)
            {
                copySlot(elementType, policy, vector->getElementPtr(src, i), newOwner,
                    [&](const void* value) { vector->setElement(dst, i, value); });
            }
            break;
        }
        case Special::Case::StdOptional:
        {
            StdOptional* optional = static_cast<StdOptional*>(special);
            if (!optional->hasValue(src))
            {
                optional->reset(dst);
                break;
            }
            Type* valueType = optional->getValueType();
            copySlot(valueType, policy, optional->getValuePtr(src), newOwner,
                [&](const void* value) { optional->setValue(dst, value); });
            break;
        }
        case Special::Case::StdMap:
        {
            // key/value 各自独立按 copySlot 解析(两侧都可能含 Object 引用, 都按同一个 policy
            // 处理)——先解析出 key 的最终值, 在它仍然有效的这层作用域里再解析 value, 最后
            // 两者一起插入; copySlot 内部如果发现某一侧是"结构体/容器又套了一层"的嵌套引用,
            // 会自己构造临时实例递归处理, 这里不需要关心嵌套深度。
            StdMap* map = static_cast<StdMap*>(special);
            Type* keyType = map->getKeyType();
            Type* valueType = map->getValueType();
            std::vector<const void*> keys = map->getKeyPtrs(src);
            std::vector<const void*> values = map->getValuePtrs(src);
            for (size_t i = 0; i < keys.size(); ++i)
            {
                copySlot(keyType, policy, keys[i], newOwner,
                    [&](const void* finalKey)
                    {
                        copySlot(valueType, policy, values[i], newOwner,
                            [&](const void* finalValue) { map->insertKeyValuePair(dst, finalKey, finalValue); });
                    });
            }
            break;
        }
        case Special::Case::StdUnorderedMap:
        {
            StdUnorderedMap* map = static_cast<StdUnorderedMap*>(special);
            Type* keyType = map->getKeyType();
            Type* valueType = map->getValueType();
            std::vector<const void*> keys = map->getKeyPtrs(src);
            std::vector<const void*> values = map->getValuePtrs(src);
            for (size_t i = 0; i < keys.size(); ++i)
            {
                copySlot(keyType, policy, keys[i], newOwner,
                    [&](const void* finalKey)
                    {
                        copySlot(valueType, policy, values[i], newOwner,
                            [&](const void* finalValue) { map->insertKeyValuePair(dst, finalKey, finalValue); });
                    });
            }
            break;
        }
        case Special::Case::StdPair:
        {
            StdPair* pair = static_cast<StdPair*>(special);
            copySlot(pair->getFirstType(), policy, pair->getFirstPtr(src), newOwner,
                [&](const void* value) { pair->setFirstValue(dst, value); });
            copySlot(pair->getSecondType(), policy, pair->getSecondPtr(src), newOwner,
                [&](const void* value) { pair->setSecondValue(dst, value); });
            break;
        }
        case Special::Case::StdVariant:
        {
            StdVariant* variant = static_cast<StdVariant*>(special);
            std::vector<Type*> types = variant->getTypes();
            size_t activeIndex = variant->getTypeIndex(src);
            Type* activeType = types[activeIndex];
            copySlot(activeType, policy, variant->getValuePtr(src), newOwner,
                [&](const void* value) { variant->setValue(dst, activeIndex, value); });
            break;
        }
        case Special::Case::StdString:
        case Special::Case::HypHString:
        case Special::Case::BulkData:
            // 这几种叶子类型不含 Object 引用, copyProperty 已经在上一层用整体赋值语义处理掉了,
            // 不会走到这里——列出分支只是为了让 switch 覆盖所有 case, 避免编译器警告。
            break;
        case Special::Case::HypSoftRef:
        {
            HypSoftRef* softRef = static_cast<HypSoftRef*>(special);
            Object* originalTarget = softRef->resolve(src);
            if (!originalTarget)
            {
                // 目标现在解析不出来(还没加载/已经不存在了): 原样带走存的 Guid/ClassName,
                // 没有活对象可比对, 没有信息可用来判断要不要重定向
                softRef->set(dst, softRef->getGuid(src), softRef->getClass(src));
                break;
            }

            // 软引用永远不拥有目标, 不管属性声明的 CopyPolicy 是什么, 都只有 Shallow 那一种
            // 行为: 目标这次操作里也被复制过就跟着重定向, 否则保持指向原对象——这正是原来
            // Joint 用裸 Guid 字段时丢失的那部分行为
            Object* resolvedTarget = resolveReference(originalTarget, CopyPolicy::Shallow, newOwner);
            softRef->set(dst, resolvedTarget->getGuid(), resolvedTarget->getClass());
            break;
        }
        }
    }

    void Copier::copySlot(
        Type* slotType,
        CopyPolicy policy,
        const void* srcSlotPtr,
        Object* newOwner,
        const std::function<void(const void*)>& setter)
    {
        if (slotType->getKind() == Type::Kind::Class)
        {
            Object* originalRef = *static_cast<Object* const*>(srcSlotPtr);
            Object* resolvedRef = resolveReference(originalRef, policy, newOwner);
            setter(&resolvedRef);
            return;
        }

        if (!containsObjectReference(slotType))
        {
            // 完全不含 Object 引用: 原始值指针原样交给 setter, 它内部走真实的 C++ 赋值语义,
            // 不需要在这里额外处理
            setter(srcSlotPtr);
            return;
        }

        // 这个槽位本身是"结构体/容器又套了一层、内部含 Object 引用"的嵌套场景: 构造一个同类型
        // 的临时实例, 递归把内部的引用按同一个 policy 解析填充好, 再整体交给 setter——setter
        // (insertKeyValuePair/setElement 等)内部会用真实赋值语义把这份临时数据"吸"进目标容器,
        // 之后这个临时实例正常析构, 不会有额外的悬挂引用。
        std::vector<uint8_t> scratch(slotType->getSize());
        slotType->construct(scratch.data());
        copyContainerContent(slotType, policy, srcSlotPtr, scratch.data(), newOwner);
        setter(scratch.data());
        slotType->destruct(scratch.data());
    }

    Object* Copier::resolveReference(Object* original, CopyPolicy policy, Object* newOwner)
    {
        if (!original)
            return nullptr;

        if (policy == CopyPolicy::Deep)
        {
            Object* existing = mContext.findCopy(original);
            assert(existing &&
                "Copier: Deep 引用的对象在扫描阶段应该已经被分配好副本, 检查 scan()/scanValue() "
                "是否漏掉了这条路径");
            return existing;
        }

        // Shallow: 如果这个引用的目标恰好也在本次操作范围内被复制过(比如复制一整棵子树时,
        // 子节点的 mParent 指向的父节点也在这次操作里), 重定向到它的副本; 否则保留指向原对象,
        // 这正是"只复制单个对象"时应该表现出的别名行为——两种情况用的是同一条判断逻辑,
        // 不需要区分"复制范围"是大是小。
        if (Object* existing = mContext.findCopy(original))
            return existing;
        return original;
    }

    bool Copier::containsObjectReference(Type* type)
    {
        switch (type->getKind())
        {
        case Type::Kind::Fundamental:
        case Type::Kind::Enumeration:
            return false;

        case Type::Kind::Class:
            return true;

        case Type::Kind::Structure:
        {
            Structure* structure = static_cast<Structure*>(type);
            for (Property* property : structure->getProperties())
            {
                if (containsObjectReference(property->getDeclaredType()))
                    return true;
            }
            return false;
        }

        case Type::Kind::Special:
        {
            Special* special = static_cast<Special*>(type);
            switch (special->getCase())
            {
            case Special::Case::StdArray:
                return containsObjectReference(static_cast<StdArray*>(special)->getElementType());
            case Special::Case::StdList:
                return containsObjectReference(static_cast<StdList*>(special)->getElementType());
            case Special::Case::StdSet:
                return containsObjectReference(static_cast<StdSet*>(special)->getElementType());
            case Special::Case::StdUnorderedSet:
                return containsObjectReference(static_cast<StdUnorderedSet*>(special)->getElementType());
            case Special::Case::StdVector:
                return containsObjectReference(static_cast<StdVector*>(special)->getElementType());
            case Special::Case::StdOptional:
                return containsObjectReference(static_cast<StdOptional*>(special)->getValueType());
            case Special::Case::StdMap:
            {
                StdMap* map = static_cast<StdMap*>(special);
                return containsObjectReference(map->getKeyType()) || containsObjectReference(map->getValueType());
            }
            case Special::Case::StdUnorderedMap:
            {
                StdUnorderedMap* map = static_cast<StdUnorderedMap*>(special);
                return containsObjectReference(map->getKeyType()) || containsObjectReference(map->getValueType());
            }
            case Special::Case::StdPair:
            {
                StdPair* pair = static_cast<StdPair*>(special);
                return containsObjectReference(pair->getFirstType()) || containsObjectReference(pair->getSecondType());
            }
            case Special::Case::StdVariant:
            {
                for (Type* alternativeType : static_cast<StdVariant*>(special)->getTypes())
                {
                    if (containsObjectReference(alternativeType))
                        return true;
                }
                return false;
            }
            case Special::Case::StdString:
            case Special::Case::HypHString:
            case Special::Case::BulkData:
                return false;
            case Special::Case::HypSoftRef:
                return true;
            }
            return false;
        }
        }
        return false;
    }

    std::vector<Property*> Copier::collectAllProperties(Class* clazz)
    {
        std::vector<Property*> properties;
        for (Class* current = clazz; current != nullptr; current = current->getBaseClass())
        {
            std::span<Property* const> currentProperties = current->getProperties();
            properties.insert(properties.end(), currentProperties.begin(), currentProperties.end());
        }
        return properties;
    }
}
