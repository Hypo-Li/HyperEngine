#pragma once
#include "Device.h"
#include "Semaphore.h"
#include "Fence.h"

#include <Core/Math/Vector.h>

namespace hyp::vk
{
    class CommandPool;
    class CommandBuffer : public Refable
    {
        friend class CommandPool;
    public:
        ~CommandBuffer() noexcept;

        inline CommandPool& getPool() const noexcept
        {
            return mPool;
        }

        inline VkCommandBuffer getHandle() const noexcept
        {
            return mCommandBuffer;
        }

        enum class State
        {
            Initial,
            Recording,
            Executable,
            Pending,
            Invalid,
        };

        State getState() const noexcept
        {
            return mState;
        }

        inline const Fence& getSignalFence() const noexcept
        {
            return *mSignalFence;
        }

        void reset() noexcept;

        void onSubmit() noexcept;

        void onExecuted() noexcept;

        void onPoolReset() noexcept;

        void waitSignalFence(uint64_t timeout = UINT64_MAX) noexcept;

        void resetSignalFence() noexcept;

        inline bool isInRenderPass() const noexcept
        {
            return mIsInRenderPass;
        }

        void beginRecord(VkCommandBufferUsageFlags flags) noexcept;

        void endRecord() noexcept;

        void pipelineBarrier(
            const VkImageMemoryBarrier& imageBarrier,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask
        ) const noexcept;

        void pipelineBarrier(
            std::span<const VkImageMemoryBarrier> imageBarriers,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask
        ) const noexcept;

        void pipelineBarrier(
            const VkBufferMemoryBarrier& bufferBarrier,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask
        ) const noexcept;

        void pipelineBarrier(
            std::span<const VkBufferMemoryBarrier> bufferBarriers,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask
        ) const noexcept;

        void pipelineBarrier2(
            std::span<const VkBufferMemoryBarrier2> bufferBarriers,
            std::span<const VkImageMemoryBarrier2> imageBarriers,
            std::span<const VkMemoryBarrier2> memoryBarriers = {}
        ) const noexcept;

        void pipelineBarrier2(std::span<const VkBufferMemoryBarrier2> bufferBarriers) const noexcept;

        void pipelineBarrier2(std::span<const VkImageMemoryBarrier2> imageBarriers) const noexcept;

        void pipelineBarrier2(std::span<const VkMemoryBarrier2> memoryBarriers) const noexcept;

        inline void fillBuffer(VkBuffer dstBuffer, VkDeviceSize offset, VkDeviceSize size, uint32_t data) const noexcept
        {
            vkCmdFillBuffer(mCommandBuffer, dstBuffer, offset, size, data);
        }

        inline void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, std::span<const VkBufferCopy> copyRegions) const noexcept
        {
            vkCmdCopyBuffer(mCommandBuffer, srcBuffer, dstBuffer, static_cast<uint32_t>(copyRegions.size()), copyRegions.data());
        }

        inline void copyBuffer2(VkBuffer srcBuffer, VkBuffer dstBuffer, std::span<const VkBufferCopy2> copyRegions) const noexcept
        {
            VkCopyBufferInfo2 copyInfo{};
            copyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
            copyInfo.srcBuffer = srcBuffer;
            copyInfo.dstBuffer = dstBuffer;
            copyInfo.regionCount = static_cast<uint32_t>(copyRegions.size());
            copyInfo.pRegions = copyRegions.data();
            vkCmdCopyBuffer2(mCommandBuffer, &copyInfo);
        }

        inline void copyBufferToImage(VkBuffer srcBuffer, VkImage dstImage, VkImageLayout imageLayout, std::span<const VkBufferImageCopy> copyRegion) const noexcept
        {
            vkCmdCopyBufferToImage(mCommandBuffer, srcBuffer, dstImage, imageLayout, static_cast<uint32_t>(copyRegion.size()), copyRegion.data());
        }

        inline void updateBuffer(VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* data) const noexcept
        {
            vkCmdUpdateBuffer(mCommandBuffer, dstBuffer, dstOffset, dataSize, data);
        }

        inline void blitImage(VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, std::span<const VkImageBlit> regions, VkFilter filter) const noexcept
        {
            vkCmdBlitImage(mCommandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, static_cast<uint32_t>(regions.size()), regions.data(), filter);
        }

        void beginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer, VkRect2D renderArea, std::span<const VkClearValue> clearValues) noexcept;

        void endRenderPass() noexcept;

        inline void bindPipeline(VkPipelineBindPoint bindPoint, VkPipeline pipeline) const noexcept
        {
            vkCmdBindPipeline(mCommandBuffer, bindPoint, pipeline);
        }

        inline void bindDescriptorSets(VkPipelineBindPoint bindPoint, VkPipelineLayout layout, uint32_t firstSet, std::span<const VkDescriptorSet> descriptorSets, std::span<const uint32_t> dynamicOffsets) const noexcept
        {
            vkCmdBindDescriptorSets(
                mCommandBuffer,
                bindPoint,
                layout,
                firstSet,
                static_cast<uint32_t>(descriptorSets.size()),
                descriptorSets.data(),
                static_cast<uint32_t>(dynamicOffsets.size()),
                dynamicOffsets.data()
            );
        }

        inline void pushConstants(VkPipelineLayout layout, VkShaderStageFlags shaderStages, uint32_t offset, uint32_t size, const void* values) const noexcept
        {
            vkCmdPushConstants(mCommandBuffer, layout, shaderStages, offset, size, values);
        }

        inline void bindVertexBuffers(uint32_t firstBinding, std::span<const VkBuffer> buffers, std::span<const VkDeviceSize> offsets) const noexcept
        {
            vkCmdBindVertexBuffers(mCommandBuffer, firstBinding, static_cast<uint32_t>(buffers.size()), buffers.data(), offsets.data());
        }

        inline void bindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) const noexcept
        {
            vkCmdBindIndexBuffer(mCommandBuffer, buffer, offset, indexType);
        }

        inline void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) const noexcept
        {
            vkCmdDispatch(mCommandBuffer, groupCountX, groupCountY, groupCountZ);
        }

        inline void dispatchIndirect(VkBuffer buffer, VkDeviceSize offset) const noexcept
        {
            vkCmdDispatchIndirect(mCommandBuffer, buffer, offset);
        }

        inline void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) const noexcept
        {
            vkCmdDraw(mCommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
        }

        inline void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) const noexcept
        {
            vkCmdDrawIndexed(mCommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        }

        inline void drawIndirect(VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) const noexcept
        {
            vkCmdDrawIndirect(mCommandBuffer, buffer, offset, drawCount, stride);
        }

        inline void drawIndexedIndirect(VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) const noexcept
        {
            vkCmdDrawIndexedIndirect(mCommandBuffer, buffer, offset, drawCount, stride);
        }

        inline void drawIndirectCount(VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) const noexcept
        {
            vkCmdDrawIndirectCount(mCommandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
        }

        inline void drawIndexedIndirectCount(VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) const noexcept
        {
            vkCmdDrawIndexedIndirectCount(mCommandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
        }

        inline void setViewport(uint32_t firstViewport, std::span<const VkViewport> viewports) const noexcept
        {
            vkCmdSetViewport(mCommandBuffer, firstViewport, static_cast<uint32_t>(viewports.size()), viewports.data());
        }

        inline void setScissor(uint32_t firstScissor, std::span<const VkRect2D> scissors) const noexcept
        {
            vkCmdSetScissor(mCommandBuffer, firstScissor, static_cast<uint32_t>(scissors.size()), scissors.data());
        }

        inline void setCullMode(VkCullModeFlags cullMode) const noexcept
        {
            vkCmdSetCullMode(mCommandBuffer, cullMode);
        }

        inline void setPolygonMode(VkPolygonMode polygonMode) const noexcept
        {
            vkCmdSetPolygonModeEXT(mCommandBuffer, polygonMode);
        }

        inline void buildAccelerationStructures(
            std::span<const VkAccelerationStructureBuildGeometryInfoKHR> buildInfos,
            std::span<const VkAccelerationStructureBuildRangeInfoKHR* const> buildRangeInfos
        ) const noexcept
        {
            assert(buildInfos.size() == buildRangeInfos.size());
            vkCmdBuildAccelerationStructuresKHR(
                mCommandBuffer,
                static_cast<uint32_t>(buildInfos.size()),
                buildInfos.data(),
                buildRangeInfos.data()
            );
        }

        inline void traceRays(
            const VkStridedDeviceAddressRegionKHR& raygenRegion,
            const VkStridedDeviceAddressRegionKHR& missRegion,
            const VkStridedDeviceAddressRegionKHR& hitRegion,
            const VkStridedDeviceAddressRegionKHR& callableRegion,
            uint32_t width,
            uint32_t height,
            uint32_t depth
        ) const noexcept
        {
            vkCmdTraceRaysKHR(
                mCommandBuffer,
                &raygenRegion,
                &missRegion,
                &hitRegion,
                &callableRegion,
                width,
                height,
                depth
            );
        }

        void beginDebugUtilsLabelExt(const char* labelName, const float4& color) const noexcept;

        void endDebugUtilsLabelExt() const noexcept;

        void insertDebugUtilsLabelExt(const char* labelName, const float4& color) const noexcept;

    private:
        CommandBuffer(CommandPool& pool, VkCommandBuffer commandBuffer) noexcept;

    private:
        CommandPool& mPool;
        VkCommandBuffer mCommandBuffer{ VK_NULL_HANDLE };
        TRef<Fence> mSignalFence;

        VkCommandBufferUsageFlags mBeginFlags;
        State mState{ State::Initial };
        bool mIsInRenderPass{ false };
    };

    class CommandPool : public Refable
    {
    public:
        CommandPool(const Device& device, uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags) noexcept;

        ~CommandPool() noexcept;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        VkCommandPool getHandle() const noexcept
        {
            return mCommandPool;
        }

        void reset() noexcept;

        inline bool canResetCommandBuffer() const noexcept
        {
            return mCreateFlags & VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        }

        CommandBuffer* allocateCommandBuffer(VkCommandBufferLevel level);

    private:
        const Device& mDevice;
        VkCommandPool mCommandPool{ VK_NULL_HANDLE };
        VkCommandPoolCreateFlags mCreateFlags;
        std::vector<TRef<CommandBuffer>> mCommandBuffers;
    };
}
