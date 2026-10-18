#pragma once
#include "../Special.h"
#include "../../Common/SoftRef.h"

namespace hyp
{
    template <typename T>
    struct container_traits<TSoftRef<T>> {
        using type = T;
    };

    // 跟 StdOptional/TStdOptional<T> 一个套路: HypSoftRef 是不依赖 T 的抽象基类, 给 Serializer
    // 之类只知道 Case::HypSoftRef、不知道具体 T 的调用方用; THypSoftRef<T> 才是每个 T 各自的实现。
    class HypSoftRef : public Special
    {
    public:
        virtual Guid getGuid(const void* instance) const = 0;
        virtual Class* getClass(const void* instance) const = 0;
        virtual void set(void* instance, Guid guid, Class* clazz) const = 0;
        virtual Object* resolve(const void* instance) const = 0;

    protected:
        HypSoftRef(HString name, size_t size) : Special(name, size, Case::HypSoftRef) {}
    };

    template <ObjectType T>
    class THypSoftRef : public HypSoftRef
    {
        friend class Reflection;
    public:
        virtual void construct(void* instance) const override
        {
            std::construct_at(static_cast<TSoftRef<T>*>(instance));
        }

        virtual void destruct(void* instance) const override
        {
            std::destroy_at(static_cast<TSoftRef<T>*>(instance));
        }

        virtual bool compare(const void* lhs, const void* rhs) const override
        {
            return *static_cast<const TSoftRef<T>*>(lhs) == *static_cast<const TSoftRef<T>*>(rhs);
        }

        virtual Guid getGuid(const void* instance) const override
        {
            return static_cast<const TSoftRef<T>*>(instance)->getGuid();
        }

        virtual Class* getClass(const void* instance) const override
        {
            return static_cast<const TSoftRef<T>*>(instance)->getClass();
        }

        virtual void set(void* instance, Guid guid, Class* clazz) const override
        {
            static_cast<TSoftRef<T>*>(instance)->set(guid, clazz);
        }

        virtual Object* resolve(const void* instance) const override
        {
            return static_cast<const TSoftRef<T>*>(instance)->resolve();
        }

    private:
        THypSoftRef() : HypSoftRef(getName(), sizeof(TSoftRef<T>)) {}

        static HString getName()
        {
            std::string name = "TSoftRef<" + std::string(Reflection::getType<T>()->getName().toStringView()) + ">";
            return HString(name);
        }
    };
}
