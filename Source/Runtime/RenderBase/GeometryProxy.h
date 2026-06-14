#pragma once
#include "RenderProxy.h"
#include "VertexLayout.h"
#include "Vulkan/Buffer.h"
#include "Vulkan/CommandBuffer.h"
#include "StagingAllocator.h"

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
            std::vector<VkDeviceSize>&& vertexBindingOffsets,
            std::vector<uint8_t> indexData,
            VkIndexType indexType,
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

        void prepare(vk::CommandBuffer& cmd);

        void bind(vk::CommandBuffer& cmd) const noexcept;

        void draw(vk::CommandBuffer& cmd, uint32_t sectionIdx, uint32_t instanceCount, uint32_t firstInstance) const noexcept;

    private:
        void flushUploads(vk::CommandBuffer& cmd);

    private:
        uint32_t mId;
        VertexLayout mVertexLayout;
        TRef<vk::Buffer> mBuffer;
        // 用于bindVertexBuffers，避免每次调用都创建临时数组
        std::vector<VkBuffer> mCachedBufferHandles;
        std::vector<VkDeviceSize> mVertexBindingOffsets;
        size_t mIndexOffset;
        VkIndexType mIndexType;
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
