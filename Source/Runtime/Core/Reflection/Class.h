#pragma once
#include <vector>
#include <span>
#include <memory>
#include "Property.h"
#include "../Object/ObjectMemoryPool.h"
#include "../Common/Refable.h"

namespace hyp
{
    // 前向声明, 不能整份 #include "../Common/Guid.h"——Guid.h 会拉进 Reflection.h,
    // Reflection.h 又会 #include "Class.h" 自己, 直接接 Guid.h 会形成头文件循环 include。
    struct Guid;
    class GuidRegistry;

    template <ObjectType T>
    TRef<T> createObject(Object* owner, ObjectFlags flags = ObjectFlags::None);

    class Reflection;

    class Class : public Type
    {
        friend class Reflection;
    public:
        virtual ~Class();

        virtual Object* allocate(ObjectHandle& handle) = 0;

        virtual void free(ObjectHandle handle) = 0;

        virtual bool isValid(ObjectHandle handle) const noexcept = 0;

        // 按 Guid 在这个类型范围内查——所有 TClass<T> 共享这一份实现(不需要每个 T 各自
        // override 一遍), 具体存储用 Pimpl 藏在 Class.cpp 里, 理由见上面 Guid 前向声明的注释。
        // 只会查到这个具体类型自己范围内的对象, 不会连派生类/基类一起查——TSoftRef<T> 存的
        // 类名必须是对象自己的实际运行时类型(比如 BoxCollider), 不能是声明的引用类型上界
        // (比如 Collider), 因为 Collider 是抽象类, 它自己范围内永远查不到东西。
        Object* findByGuid(Guid guid) const;

        // registerGuid()/unregisterGuid() 不能合并进 allocate()/free()——allocate() 那一刻
        // 对象的 Guid 还没被赋值(要等 construct() 里的构造函数跑完), 只能由 createObject()/
        // restoreObject() 在拿到构造完成的对象之后另外调用; unregisterGuid() 同理要在
        // destruct() 之前调用, 这时候 Guid 还能读。
        void registerGuid(Guid guid, Object* object);

        void unregisterGuid(Guid guid);

        virtual bool isAbstract() const noexcept = 0;

        Class* getBaseClass() const noexcept
        {
            return mBaseClass;
        }

        Property* getProperty(HString name) const;

        std::span<Property* const> getProperties() const noexcept
        {
            return mProperties;
        }

        const Object* getDefaultObject() const
        {
            return mDefaultObject;
        }

        bool isDerivedFrom(const Class* clazz) const;

        bool isBaseOf(const Class* clazz) const;

        std::span<Class* const> getDerivedClasses() const noexcept
        {
            return mDerivedClasses;
        }

        void setVersion(uint32_t version) noexcept
        {
            mVersion = version;
        }

        uint32_t getVersion() const noexcept
        {
            return mVersion;
        }

        // 没显式调用过 setDisplayName() 时默认就是 getName()(构造时已经这样初始化)——多单词的
        // 类名(PointLight)才需要显式设置成带空格的可读版本(Point Light), 给 Inspector "添加
        // 组件"这类下拉框用。不影响 getName()(反射查找/序列化用的还是原名, 两者是分开的概念)。
        HString getDisplayName() const noexcept
        {
            return mDisplayName;
        }

        void setDisplayName(HString displayName) noexcept
        {
            mDisplayName = displayName;
        }

    protected:
        Class(HString name, size_t size, Class* baseClass, TRef<Object> defaultObject);

        static void addDerivedToBase(Class* baseClass, Class* derivedClass);

        std::vector<Property*> mProperties;

    private:
        Class* mBaseClass;
        std::vector<Class*> mDerivedClasses;
        TRef<Object> mDefaultObject;
        uint32_t mVersion = 0;
        HString mDisplayName;
        // 惰性分配——大多数 Class(纯抽象基类、从不需要按 Guid 查的类型)永远用不上这张表。
        std::unique_ptr<GuidRegistry> mGuidRegistry;
    };

    template <ObjectType T, ObjectType Base = Object, AllocateFrequency ALLOC_FREQ = AllocateFrequency::Medium>
    class TClass : public Class
    {
        friend class Reflection;
    public:
        virtual void construct(void* instance) const override;

        virtual void destruct(void* instance) const override;

        virtual bool compare(const void* instance1, const void* instance2) const override;

        virtual T* allocate(ObjectHandle& handle) override;

        virtual void free(ObjectHandle handle) override;

        virtual bool isValid(ObjectHandle handle) const noexcept override;

        virtual bool isAbstract() const noexcept override;

        // 返回具体的 TProperty<Declared, T>*(不是擦除过的 Property*)——需要挂自定义 setter
        // 的调用点可以直接链式调 ->setCustomSetter(...), 不用另外查表。丢弃返回值/存进
        // Property* 的既有调用点不受影响, 隐式转换自动处理。
        template <typename Declared>
        TProperty<Declared, T>* addProperty(HString name, Declared T::* memberPtr, CopyPolicy copyPolicy = CopyPolicy::Shallow, PropertyFlags flags = PropertyFlags::None);

    private:
        TClass(HString name);

        static Class* findBaseClass();

        static TRef<Object> createDefaultObject();

    private:
        ObjectMemoryPool<T, ALLOC_FREQ> mObjectMemoryPool;
    };
}
