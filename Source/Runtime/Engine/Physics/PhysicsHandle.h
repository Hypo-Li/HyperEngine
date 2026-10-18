#pragma once
#include <cassert>
#include <cstdint>
#include <vector>

namespace hyp
{
    // 加新的物理资源类型只需要在这里加一行, 不需要新建类型。
    enum class PhysicsResourceType
    {
        Body,
        Constraint,
    };

    // 不同 RESOURCE_TYPE 实例化出来的是完全不同的 C++ 类型, 不能互相隐式转换——跟
    // RenderBase/ProxyHandle.h 的 TProxyHandle<ProxyType> 同一个思路(用非类型模板参数
    // 做类型区分), 少了它那层"一个分配器管所有类型、放进 std::array"的部分, 那是它自己
    // 不能反向依赖 Renderer 才需要的, 这里没有这个约束, 直接给每种资源单独一个 Pool 成员。
    template <PhysicsResourceType RESOURCE_TYPE>
    class TPhysicsHandle
    {
    public:
        TPhysicsHandle() = default;

        TPhysicsHandle(uint32_t index, uint32_t version) noexcept :
            mIndex(index), mVersion(version)
        {
        }

        bool operator==(const TPhysicsHandle&) const noexcept = default;

        uint32_t getIndex() const noexcept
        {
            return mIndex;
        }

        uint32_t getVersion() const noexcept
        {
            return mVersion;
        }

        bool isValid() const noexcept
        {
            return mIndex != UINT32_MAX;
        }

    private:
        uint32_t mIndex = UINT32_MAX;
        uint32_t mVersion = 0;
    };

    template <typename T, PhysicsResourceType RESOURCE_TYPE>
    class TPhysicsHandlePool
    {
    public:
        TPhysicsHandle<RESOURCE_TYPE> insert(T value)
        {
            uint32_t index;
            if (!mFreeIndices.empty())
            {
                index = mFreeIndices.back();
                mFreeIndices.pop_back();
                mValues[index] = std::move(value);
            }
            else
            {
                index = static_cast<uint32_t>(mValues.size());
                mValues.push_back(std::move(value));
                mVersions.push_back(1);
            }
            return TPhysicsHandle<RESOURCE_TYPE>(index, mVersions[index]);
        }

        void remove(TPhysicsHandle<RESOURCE_TYPE> handle)
        {
            if (!isValid(handle))
                return;

            mValues[handle.getIndex()] = T{};
            mVersions[handle.getIndex()]++;
            mFreeIndices.push_back(handle.getIndex());
        }

        bool isValid(TPhysicsHandle<RESOURCE_TYPE> handle) const noexcept
        {
            return handle.getIndex() < mVersions.size() && handle.getVersion() == mVersions[handle.getIndex()];
        }

        T& get(TPhysicsHandle<RESOURCE_TYPE> handle)
        {
            assert(isValid(handle));
            return mValues[handle.getIndex()];
        }

        const T& get(TPhysicsHandle<RESOURCE_TYPE> handle) const
        {
            assert(isValid(handle));
            return mValues[handle.getIndex()];
        }

    private:
        std::vector<T> mValues;
        std::vector<uint32_t> mVersions;
        std::vector<uint32_t> mFreeIndices;
    };
}
