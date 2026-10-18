#include "Class.h"
#include "Reflection.h"
#include "../Object/Object.h"

#include <unordered_map>

namespace hyp
{
    // Class.h 里只能前向声明 Guid(整份 #include "../Common/Guid.h" 会经 Reflection.h 绕回
    // Class.h 自己, 见 Class.h 里那条注释), 真正的存储只能挪到这里——Class.cpp 是叶子文件,
    // 不会被谁反过来 #include, 用 Object.h 已经带进来的完整 Guid 定义没有循环风险。
    class GuidRegistry
    {
    public:
        Object* find(Guid guid) const
        {
            auto it = mObjects.find(guid);
            return it != mObjects.end() ? it->second : nullptr;
        }

        void add(Guid guid, Object* object)
        {
            mObjects[guid] = object;
        }

        void remove(Guid guid)
        {
            mObjects.erase(guid);
        }

    private:
        std::unordered_map<Guid, Object*> mObjects;
    };

    Class::~Class()
    {
    }

    Object* Class::findByGuid(Guid guid) const
    {
        return mGuidRegistry ? mGuidRegistry->find(guid) : nullptr;
    }

    void Class::registerGuid(Guid guid, Object* object)
    {
        if (!mGuidRegistry)
            mGuidRegistry = std::make_unique<GuidRegistry>();
        mGuidRegistry->add(guid, object);
    }

    void Class::unregisterGuid(Guid guid)
    {
        if (mGuidRegistry)
            mGuidRegistry->remove(guid);
    }

    Property* Class::getProperty(HString name) const
    {
        for (auto prop : mProperties)
            if (prop->getName() == name)
                return prop;
        if (mBaseClass)
        {
            return mBaseClass->getProperty(name);
        }
        return nullptr;
    }

    bool Class::isDerivedFrom(const Class* clazz) const
    {
        // 含自身: X->isDerivedFrom(X) 为 true, 跟 std::is_base_of<Base, Derived>/instanceof
        // 这类语言内建的"is-a"语义保持一致——调用方要判断"这是不是一个 X(可能就是 X 自己)"
        // 时不需要再额外补一句 == 判断。
        for (const Class* current = this; current; current = current->mBaseClass)
        {
            if (current == clazz)
                return true;
        }
        return false;
    }

    bool Class::isBaseOf(const Class* clazz) const
    {
        return clazz->isDerivedFrom(this);
    }

    Class::Class(HString name, size_t size, Class* baseClass, TRef<Object> defaultObject)
        : Type(name, size, Kind::Class), mBaseClass(baseClass), mDefaultObject(defaultObject), mDisplayName(name)
    {
    }

    void Class::addDerivedToBase(Class* baseClass, Class* derivedClass)
    {
        baseClass->mDerivedClasses.push_back(derivedClass);
    }

}
