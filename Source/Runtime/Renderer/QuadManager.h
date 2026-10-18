#pragma once
#include <RenderBase/RangeTable.h>
#include <RenderBase/StagingAllocator.h>
#include <RenderBase/VertexLayout.h>

#include <Core/Common/Refable.h>
#include <Core/Math/Vector.h>
#include <RHI/Buffer.h>
#include <RHI/CommandList.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace hyp
{
    class RenderBackend;

    // 文字字形和精灵共用的四边形元素, 布局必须跟 Shader/VertexLayout/Quad.slang 里的同名结构一致
    struct QuadElement
    {
        float2 position;
        float2 size;
        uint32_t uvMin; // unorm16x2
        uint32_t uvMax; // unorm16x2
        uint32_t pageAndFlags; // 低 16 位是页号
        uint32_t color; // RGBA8
    };
    static_assert(sizeof(QuadElement) == 32);

    // 每个 QuadRenderer 一条, 布局必须跟 Shader/VertexLayout/Quad.slang 里的同名结构一致
    struct QuadInstanceData
    {
        uint32_t instanceIndex;
        uint32_t elementBase;
        uint32_t elementCount;
        uint32_t tint; // RGBA8
        float2 blockSize;
        float2 anchor;
        float depthBias;
        float autoScaleStrength;
        float referenceDistance;
        float minScale;
        float maxScale;
        float reserved;
    };
    static_assert(sizeof(QuadInstanceData) == 56);

    // 绑定组由 SceneProxy 持有, 因为它还要包含 InstanceData 和全局重映射缓冲。
    class QuadManager
    {
    public:
        static VertexLayout getVertexLayout();

        QuadManager(RenderBackend& renderBackend, uint32_t elementCapacity, uint32_t instanceCapacity);

        ~QuadManager();

        QuadManager(const QuadManager&) = delete;
        QuadManager& operator=(const QuadManager&) = delete;

        // 找不到足够大的连续空闲区间返回 nullopt
        std::optional<uint32_t> allocateElementRange(uint32_t elementCount);

        // 同时丢弃这段区间上还没 flush 的上传, 避免写进之后被别人重新分配走的区间
        void freeElementRange(uint32_t offset, uint32_t elementCount) noexcept;

        // 整段区间重新上传; 同一个 offset 上还没 flush 的旧上传会被替换
        void queueElementUpload(uint32_t offset, std::vector<QuadElement>&& elements);

        // 容量耗尽返回 nullopt
        std::optional<uint32_t> acquireInstanceIndex();

        // 同时丢弃这一条还没 flush 的上传
        void releaseInstanceIndex(uint32_t instanceIndex) noexcept;

        // 同一个下标上还没 flush 的旧上传会被替换
        void setInstanceData(uint32_t instanceIndex, const QuadInstanceData& data);

        // 每帧调用一次, 元素上传排在实例旁表之前, 保证实例条目生效时它指向的元素已经就位
        void prepare(rhi::CommandList& cmdList);

        bool hasPendingUploads() const noexcept
        {
            return mElementTable.hasPendingUploads() || !mPendingInstanceUploads.empty();
        }

        const rhi::Buffer& getElementBuffer() const noexcept
        {
            return *mElementTable.getBuffer();
        }

        const rhi::Buffer& getInstanceDataBuffer() const noexcept
        {
            return *mInstanceDataBuffer;
        }

    private:
        void flushInstanceUploads(rhi::CommandList& cmdList);

    private:
        RenderBackend& mRenderBackend;

        TRangeTable<QuadElement> mElementTable;

        uint32_t mInstanceCapacity = 0;
        TRef<rhi::Buffer> mInstanceDataBuffer;
        bool mInstanceDataBufferInitialized = false;
        std::vector<uint32_t> mFreeInstanceIndices;
        uint32_t mInstanceIndexCount = 0;

        struct PendingInstanceUpload
        {
            uint32_t instanceIndex;
            QuadInstanceData data;
            std::optional<StagingAllocation> stagingAllocation;
        };
        std::vector<PendingInstanceUpload> mPendingInstanceUploads;
    };
}
