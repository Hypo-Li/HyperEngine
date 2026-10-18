#pragma once
#include "Memory.h"

#include <Core/Common/HString.h>
#include <Core/Common/Refable.h>
#include <Core/Common/EnumBitwise.h>

namespace hyp::rhi
{
    enum class BufferUsage
    {
        None            = 0,
        Transfer_Src    = 1 << 0,
        Transfer_Dst    = 1 << 1,
        Uniform         = 1 << 2,
        Storage         = 1 << 3,
        Index           = 1 << 4,
        Vertex          = 1 << 5,
        Indirect        = 1 << 6,
    };
    ENABLE_ENUM_CLASS_BITWISE(BufferUsage)

    static constexpr uint64_t kWholeBuffer = ~0ull;

    struct BufferDesc
    {
        uint64_t size = 0;
        BufferUsage usage = BufferUsage::None;
    };

    class Device;
    class Buffer : public Refable
    {
        friend class Device;
    public:
        Buffer(const Device& device, const BufferDesc& desc, MemoryType memoryType) :
            mDevice(device), mDesc(desc), mMemoryType(memoryType) {}

        virtual ~Buffer() = default;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        const BufferDesc& getDesc() const noexcept
        {
            return mDesc;
        }

        MemoryType getMemoryType() const noexcept
        {
            return mMemoryType;
        }

        virtual void setDebugName(HString name)
        {
            mDebugName = std::move(name);
        }

        HString getDebugName() const noexcept
        {
            return mDebugName;
        }

        // Device::createBuffer() 创建完之后赋值一次(setUniqueId 是私有的, 只有 Device 能调)——
        // 代替裸指针比较, 用于 BindGroupCache 这类增量更新判断: 对象销毁后分配器可能把新对象
        // 分配到同一个地址上, 裸指针会把"新建的资源"误判成"还是原来那个没变过", unique id
        // 只增不减不会有这个问题
        uint64_t getUniqueId() const noexcept
        {
            return mUniqueId;
        }

        virtual void* map() = 0;

        virtual void unmap() = 0;

    private:
        void setUniqueId(uint64_t uniqueId) noexcept
        {
            mUniqueId = uniqueId;
        }

    private:
        const Device& mDevice;
        BufferDesc mDesc;
        MemoryType mMemoryType;
        HString mDebugName;
        uint64_t mUniqueId = 0;
    };
}
