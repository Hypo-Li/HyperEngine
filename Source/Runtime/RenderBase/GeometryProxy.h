#pragma once
#include "ResourceProxy.h"
#include "VertexLayout.h"
#include "StagingAllocator.h"

#include <RHI/Buffer.h>
#include <RHI/CommandList.h>

namespace hyp
{
    class GeometryProxy : public ResourceProxy
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

        const Section& getSection(uint32_t sectionIdx) const noexcept
        {
            assert(sectionIdx < mSections.size());
            return mSections[sectionIdx];
        }

        bool prepare(rhi::CommandList& cmdList) override;

        void bind(rhi::CommandList& cmdList) const noexcept;

        void draw(rhi::CommandList& cmdList, uint32_t sectionIdx, uint32_t instanceCount, uint32_t firstInstance) const noexcept;

        // 给 Visualization 的 wireframeGeometry 用: 不走 bind()/draw() 那条固定管线顶点抓取路径,
        // 而是把这同一块 buffer 当只读 storage buffer, 在 shader 里手动按下标反查三角形边画线框
        // (原因见 VisualizationRenderData.cpp 的注释——三角形 index buffer 不能直接当 Line_List
        // 数据用)。只支持 Uint32 索引, 调用方需要自己 assert
        rhi::Buffer& getBuffer() const noexcept
        {
            return *mBuffer;
        }

        // Position 属性的字节偏移——VertexLayout 保证只要 attributeMask 包含 Position 就一定是
        // 第一个 binding(见 VertexLayout::writeVertexLayoutBindingsAndAttributes), 调用方需要
        // 自己保证这份 geometry 确实带 Position
        uint64_t getPositionByteOffset() const noexcept
        {
            return mVertexBindingOffsets[0];
        }

        uint64_t getIndexByteOffset() const noexcept
        {
            return static_cast<uint64_t>(mIndexOffset);
        }

        rhi::IndexType getIndexType() const noexcept
        {
            return mIndexType;
        }

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
