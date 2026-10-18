#pragma once
#include <RenderBase/RenderBackend.h>

#include <span>

namespace hyp
{
    // draw-call 内实例号(SV_InstanceID)到 DrawItem::instanceIndex 的重映射表(shader 里的 sIndices),
    // 同时也给 GlobalDistanceFieldCompositePass 传合成候选数据——固定容量, 没有扩容机制
    class IndicesBuffer
    {
    public:
        explicit IndicesBuffer(RenderBackend& renderBackend);

        const rhi::Buffer& getBuffer() const noexcept
        {
            return *mBuffer;
        }

        // 单位: uint32 个数, 不是字节
        static constexpr size_t getCapacity() noexcept
        {
            return kCapacity;
        }

        // 从 buffer 开头整体覆盖写入, 必须在 beginPass() 之前调用; 超出容量时记录错误、不写入并返回 false
        bool upload(rhi::CommandList& cmdList, std::span<const uint32_t> indices);

    private:
        static constexpr size_t kCapacity = 65536;

        RenderBackend& mRenderBackend;
        TRef<rhi::Buffer> mBuffer;
    };
}
