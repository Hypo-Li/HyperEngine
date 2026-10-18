#pragma once
#include "Type.h"
#include "../Common/EnumBitwise.h"

#include <functional>

namespace hyp
{
    enum class CopyPolicy
    {
        Ignore,
        Shallow,
        Deep,
    };

    // 只影响某个属性要不要被写进存档, 不影响读取(读取按存档里实际写了哪些属性来解析,
    // 天然兼容这两个 flag 的产物, 见 Serializer::loadStructure/loadClass)。
    enum class PropertyFlags : uint32_t
    {
        None        = 0,
        // 只在 Archive::isCooking() 为真时才写——cook 阶段才产出的派生数据(平时编辑期
        // 这个字段是空的/用不上, 不需要出现在编辑资产里)
        Cook_Only   = 1 << 0,
        // 只在 Archive::isCooking() 为假时才写——只供编辑期使用的数据, 不进 cook 产物
        Editor_Only = 1 << 1,
    };
    ENABLE_ENUM_CLASS_BITWISE(PropertyFlags)

    class Property
    {
    public:
        HString getName() const noexcept
        {
            return mName;
        }

        CopyPolicy getCopyPolicy() const noexcept
        {
            return mCopyPolicy;
        }

        PropertyFlags getFlags() const noexcept
        {
            return mFlags;
        }

        virtual Type* getDeclaredType() const = 0;

        // 纯字节写入, 不触发这个属性可能挂着的真正 setter 的任何副作用——Copier/Serializer/
        // PrefabInstance 同步这些地方要的就是这个保证(对象这时候往往还没准备好被"正常调用"),
        // 必须显式调这个, 不能用下面的 setValue()。
        virtual void setRawValue(void* instance, const void* value) const = 0;

        // 交互式编辑场景用: 这个属性如果通过 TProperty::setCustomSetter() 挂了真正的 setter,
        // 就调它(该起的副作用照起); 没挂就是 setRawValue() 的同义词。基类默认就是退化成
        // setRawValue(), 只有 TProperty 在挂了自定义 setter 时才会覆盖出不一样的行为。
        virtual void setValue(void* instance, const void* value) const
        {
            setRawValue(instance, value);
        }

        virtual void getValue(const void* instance, void* value) const = 0;
        virtual void* getValuePtr(void* instance) const = 0;
        virtual const void* getValuePtr(const void* instance) const = 0;
        virtual bool compare(const void* instance1, const void* instance2) const = 0;

    protected:
        Property(HString name, CopyPolicy copyPolicy, PropertyFlags flags = PropertyFlags::None) :
            mName(name), mCopyPolicy(copyPolicy), mFlags(flags) {}

    private:
        HString mName;
        CopyPolicy mCopyPolicy;
        PropertyFlags mFlags;
    };

    template <typename Declared, typename Owner>
    class TProperty : public Property
    {
        using MemberPtr = Declared Owner::*;
    public:
        // 真正的 setter, 类型已经是具体的(Owner/Declared 都是这个类自己的模板参数), 不需要
        // 再擦除一层——存成这个固定签名的 std::function 就够, 跟存 MemberPtr 没有本质区别。
        using SetterFn = std::function<void(Owner&, const Declared&)>;

        TProperty(HString name, MemberPtr memberPtr, CopyPolicy copyPolicy, PropertyFlags flags = PropertyFlags::None);

        // 没挂过就是空的, operator bool() 天然就是"有没有自定义 setter"的判断, 不需要额外的
        // 查询方法。
        void setCustomSetter(SetterFn setter) noexcept
        {
            mSetter = std::move(setter);
        }

        virtual Type* getDeclaredType() const override;

        virtual void setRawValue(void* instance, const void* value) const override;

        virtual void setValue(void* instance, const void* value) const override;

        virtual void getValue(const void* instance, void* value) const override;

        virtual void* getValuePtr(void* instance) const override;

        virtual const void* getValuePtr(const void* instance) const override;

        virtual bool compare(const void* instance1, const void* instance2) const override;

    private:
        MemberPtr mMemberPtr;
        SetterFn mSetter;
    };
}
