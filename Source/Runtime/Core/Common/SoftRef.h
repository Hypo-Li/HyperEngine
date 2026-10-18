#pragma once
#include "Guid.h"
#include "HString.h"
#include "../Object/ObjectDefines.h"
#include "../Reflection/Reflection.h"

namespace hyp
{
    // 按身份(Guid)延迟解析的引用, 跟 TRef<T> 的区别是不要求目标在建立引用的这一刻就存在。
    // 只存 Guid + 目标对象自己的实际运行时类名(不是 T 本身——T 只是声明的引用类型上界,
    // 可能是抽象类, 比如 Collider; 实际对象永远是某个具体子类, 比如 BoxCollider, 各自的池子
    // 是分开的, 必须按实际类型才能查到)。
    template <ObjectType T>
    class TSoftRef
    {
    public:
        TSoftRef() = default;

        explicit TSoftRef(T* object)
        {
            if (object)
            {
                mGuid = object->getGuid();
                mClass = object->getClass();
            }
        }

        T* resolve() const
        {
            if (!mGuid.isValid() || !mClass)
                return nullptr;

            Object* object = mClass->findByGuid(mGuid);
            if (!object)
                return nullptr;

            return static_cast<T*>(object);
        }

        bool isValid() const noexcept { return mGuid.isValid(); }

        Guid getGuid() const noexcept { return mGuid; }

        Class* getClass() const noexcept { return mClass; }

        void set(const Guid& guid, Class* clazz)
        {
            mGuid = guid;
            mClass = clazz;
        }

        friend bool operator==(const TSoftRef& lhs, const TSoftRef& rhs)
        {
            return lhs.mGuid == rhs.mGuid && lhs.mClass == rhs.mClass;
        }

    private:
        Guid mGuid;
        Class* mClass;
    };
}
