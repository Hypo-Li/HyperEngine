namespace hyp
{
    template <typename Declared, typename Owner>
    TProperty<Declared, Owner>::TProperty(HString name, MemberPtr memberPtr, CopyPolicy copyPolicy)
        : Property(name, copyPolicy), mMemberPtr(memberPtr) {
    }

    template <typename Declared, typename Owner>
    Type* TProperty<Declared, Owner>::getDeclaredType() const
    {
        return Reflection::getType<Declared>();
    }

    template <typename Declared, typename Owner>
    void TProperty<Declared, Owner>::setValue(void* instance, const void* value) const
    {
        static_cast<Owner*>(instance)->*mMemberPtr = *static_cast<const Declared*>(value);
    }

    template <typename Declared, typename Owner>
    void TProperty<Declared, Owner>::getValue(const void* instance, void* value) const
    {
        *static_cast<Declared*>(value) = static_cast<const Owner*>(instance)->*mMemberPtr;
    }

    template <typename Declared, typename Owner>
    void* TProperty<Declared, Owner>::getValuePtr(void* instance) const
    {
        return &(static_cast<Owner*>(instance)->*mMemberPtr);
    }

    template <typename Declared, typename Owner>
    const void* TProperty<Declared, Owner>::getValuePtr(const void* instance) const
    {
        return &(static_cast<const Owner*>(instance)->*mMemberPtr);
    }

    template <typename Declared, typename Owner>
    bool TProperty<Declared, Owner>::compare(const void* instance1, const void* instance2) const
    {
        if constexpr (Comparable<Declared, Declared> && !IsSpecialType<Declared>)
            return *static_cast<const Declared*>(getValuePtr(instance1)) == *static_cast<const Declared*>(getValuePtr(instance2));
        else
            return Reflection::getType<Declared>()->compare(getValuePtr(instance1), getValuePtr(instance2));
    }

}
