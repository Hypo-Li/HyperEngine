#pragma once
#include "RenderProxy.h"
#include "VertexLayout.h"
#include "StagingAllocator.h"

#include <RHI/Buffer.h>
#include <RHI/CommandList.h>

namespace hyp
{
    class GeometryProxy : public RenderProxy
    {
    public:
        struct Section
        {
            uint32_t indexCount;
            uint32_t firstIndex;
            int32_t vertexOffset;
        };

        GeometryProxy(
            VertexLayout vertexLayout,
            std::vector<uint8_t> vertexData,
            std::vector<uint64_t>&& vertexBindingOffsets,
            std::vector<uint8_t> indexData,
            rhi::IndexType indexType,
            std::vector<Section>&& sections
        );

        ~GeometryProxy() noexcept;

        uint32_t getId() const noexcept
        {
            return mId;
        }

        VertexLayout getVertexLayout() const noexcept
        {
            return mVertexLayout;
        }

        uint32_t getSectionCount() const noexcept
        {
            return static_cast<uint32_t>(mSections.size());
        }

        void prepare(rhi::CommandList& cmdList);

        void bind(rhi::CommandList& cmdList) const noexcept;

        void draw(rhi::CommandList& cmdList, uint32_t sectionIdx, uint32_t instanceCount, uint32_t firstInstance) const noexcept;

    private:
        void flushUploads(rhi::CommandList& cmdList);

    private:
        uint32_t mId;
        VertexLayout mVertexLayout;
        TRef<rhi::Buffer> mBuffer;
        // 用于bindVertexBuffers，避免每次调用都创建临时数组
        std::vector<rhi::Buffer*> mCachedBuffers;
        std::vector<uint64_t> mVertexBindingOffsets;
        size_t mIndexOffset;
        rhi::IndexType mIndexType;
        std::vector<Section> mSections;

        struct PendingUpload
        {
            std::vector<uint8_t> data;
            size_t offset;
            std::optional<StagingAllocation> stagingAllocation;
        };
        std::vector<PendingUpload> mPendingUploads;
    };

    template <>
    struct TProxyTraits<ProxyType::Geometry>
    {
        using Proxy = GeometryProxy;
    };
}
