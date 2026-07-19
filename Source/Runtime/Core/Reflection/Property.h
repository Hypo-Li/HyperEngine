#pragma once
#include "Type.h"

namespace hyp
{
    enum class CopyPolicy
    {
        Ignore,
        Shallow,
        Deep,
    };

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

        virtual Type* getDeclaredType() const = 0;
        virtual void setValue(void* instance, const void* value) const = 0;
        virtual void getValue(const void* instance, void* value) const = 0;
        virtual void* getValuePtr(void* instance) const = 0;
        virtual const void* getValuePtr(const void* instance) const = 0;
        virtual bool compare(const void* instance1, const void* instance2) const = 0;

    protected:
        Property(HString name, CopyPolicy copyPolicy) :
            mName(name), mCopyPolicy(copyPolicy) {}

    private:
        HString mName;
        CopyPolicy mCopyPolicy;
    };

    template <typename Declared, typename Owner>
    class TProperty : public Property
    {
        using MemberPtr = Declared Owner::*;
    public:
        TProperty(HString name, MemberPtr memberPtr, CopyPolicy copyPolicy);

        virtual Type* getDeclaredType() const override;

        virtual void setValue(void* instance, const void* value) const override;

        virtual void getValue(const void* instance, void* value) const override;

        virtual void* getValuePtr(void* instance) const override;

        virtual const void* getValuePtr(const void* instance) const override;

        virtual bool compare(const void* instance1, const void* instance2) const override;

    private:
        MemberPtr mMemberPtr;
    };
}
