#include "CommandBuffer.h"

namespace hyp::vk
{
    CommandBuffer::CommandBuffer(CommandPool& pool, VkCommandBuffer commandBuffer) noexcept
        : mPool(pool), mCommandBuffer(commandBuffer)
    {
        mSignalFence = new Fence(pool.getDevice(), VK_FENCE_CREATE_SIGNALED_BIT);
    }

    CommandBuffer::~CommandBuffer() noexcept
    {
        vkFreeCommandBuffers(mPool.getDevice().getHandle(), mPool.getHandle(), 1, &mCommandBuffer);
    }

    void CommandBuffer::reset() noexcept
    {
        assert(mPool.canResetCommandBuffer() && mState != State::Pending);
        vkResetCommandBuffer(mCommandBuffer, 0);
        mState = State::Initial;
    }

    void CommandBuffer::onSubmit() noexcept
    {
        assert(mState == State::Executable);
        mState = State::Pending;
    }

    void CommandBuffer::onExecuted() noexcept
    {
        assert(mState == State::Pending);
        if (mBeginFlags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
            mState = State::Executable;
        else
            mState = State::Initial;
    }

    void CommandBuffer::onPoolReset() noexcept
    {
        assert(mState == State::Invalid);
        mState = State::Initial;
    }

    void CommandBuffer::waitSignalFence(uint64_t timeout) noexcept
    {
        mSignalFence->wait(timeout);
        if (mState == State::Pending)
        {
            onExecuted();
        }
    }

    void CommandBuffer::resetSignalFence() noexcept
    {
        mSignalFence->reset();
    }

    void CommandBuffer::beginRecord(VkCommandBufferUsageFlags flags) noexcept
    {
        assert(mState == State::Initial);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = flags;
        beginInfo.pInheritanceInfo = nullptr;

        mBeginFlags = flags;

        vkBeginCommandBuffer(mCommandBuffer, &beginInfo);
        mState = State::Recording;
    }

    void CommandBuffer::endRecord() noexcept
    {
        assert(mState == State::Recording);
        vkEndCommandBuffer(mCommandBuffer);
        mState = State::Executable;
    }

    void CommandBuffer::pipelineBarrier(
        const VkImageMemoryBarrier& imageBarrier,
        VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask
    ) const noexcept
    {
        vkCmdPipelineBarrier(mCommandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
    }

    void CommandBuffer::pipelineBarrier(
        std::span<const VkImageMemoryBarrier> imageBarriers,
        VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask
    ) const noexcept
    {
        vkCmdPipelineBarrier(mCommandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(imageBarriers.size()), imageBarriers.data());
    }

    void CommandBuffer::pipelineBarrier(
        const VkBufferMemoryBarrier& bufferBarrier,
        VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask
    ) const noexcept
    {
        vkCmdPipelineBarrier(mCommandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 1, &bufferBarrier, 0, nullptr);
    }

    void CommandBuffer::pipelineBarrier(
        std::span<const VkBufferMemoryBarrier> bufferBarriers,
        VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask
    ) const noexcept
    {
        vkCmdPipelineBarrier(mCommandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, static_cast<uint32_t>(bufferBarriers.size()), bufferBarriers.data(), 0, nullptr);
    }

    void CommandBuffer::pipelineBarrier2(
        std::span<const VkBufferMemoryBarrier2> bufferBarriers,
        std::span<const VkImageMemoryBarrier2> imageBarriers,
        std::span<const VkMemoryBarrier2> memoryBarriers
    ) const noexcept
    {
        VkDependencyInfo dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
        dependencyInfo.pBufferMemoryBarriers = bufferBarriers.data();
        dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
        dependencyInfo.pImageMemoryBarriers = imageBarriers.data();
        dependencyInfo.memoryBarrierCount = static_cast<uint32_t>(memoryBarriers.size());
        dependencyInfo.pMemoryBarriers = memoryBarriers.data();
        vkCmdPipelineBarrier2(mCommandBuffer, &dependencyInfo);
    }

    void CommandBuffer::pipelineBarrier2(std::span<const VkBufferMemoryBarrier2> bufferBarriers) const noexcept
    {
        VkDependencyInfo dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
        dependencyInfo.pBufferMemoryBarriers = bufferBarriers.data();
        vkCmdPipelineBarrier2(mCommandBuffer, &dependencyInfo);
    }

    void CommandBuffer::pipelineBarrier2(std::span<const VkImageMemoryBarrier2> imageBarriers) const noexcept
    {
        VkDependencyInfo dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
        dependencyInfo.pImageMemoryBarriers = imageBarriers.data();
        vkCmdPipelineBarrier2(mCommandBuffer, &dependencyInfo);
    }

    void CommandBuffer::pipelineBarrier2(std::span<const VkMemoryBarrier2> memoryBarriers) const noexcept
    {
        VkDependencyInfo dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.memoryBarrierCount = static_cast<uint32_t>(memoryBarriers.size());
        dependencyInfo.pMemoryBarriers = memoryBarriers.data();
        vkCmdPipelineBarrier2(mCommandBuffer, &dependencyInfo);
    }

    void CommandBuffer::beginRenderPass(
        VkRenderPass renderPass,
        VkFramebuffer framebuffer,
        VkRect2D renderArea,
        std::span<const VkClearValue> clearValues
    ) noexcept
    {
        VkRenderPassBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        beginInfo.renderPass = renderPass;
        beginInfo.framebuffer = framebuffer;
        beginInfo.renderArea = renderArea;
        beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        beginInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(mCommandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
        mIsInRenderPass = true;
    }

    void CommandBuffer::endRenderPass() noexcept
    {
        mIsInRenderPass = false;
        vkCmdEndRenderPass(mCommandBuffer);
    }

    void CommandBuffer::beginDebugUtilsLabelExt(const char* labelName, const float4& color) const noexcept
    {
        if (!vkCmdBeginDebugUtilsLabelEXT)
            return;

        VkDebugUtilsLabelEXT labelInfo = {};
        labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        labelInfo.pLabelName = labelName;
        labelInfo.color[0] = color[0];
        labelInfo.color[1] = color[1];
        labelInfo.color[2] = color[2];
        labelInfo.color[3] = color[3];
        vkCmdBeginDebugUtilsLabelEXT(mCommandBuffer, &labelInfo);
    }

    void CommandBuffer::endDebugUtilsLabelExt() const noexcept
    {
        if (!vkCmdEndDebugUtilsLabelEXT)
            return;

        vkCmdEndDebugUtilsLabelEXT(mCommandBuffer);
    }

    void CommandBuffer::insertDebugUtilsLabelExt(const char* labelName, const float4& color) const noexcept
    {
        if (!vkCmdInsertDebugUtilsLabelEXT)
            return;

        VkDebugUtilsLabelEXT labelInfo = {};
        labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        labelInfo.pLabelName = labelName;
        labelInfo.color[0] = color[0];
        labelInfo.color[1] = color[1];
        labelInfo.color[2] = color[2];
        labelInfo.color[3] = color[3];
        vkCmdInsertDebugUtilsLabelEXT(mCommandBuffer, &labelInfo);
    }

    CommandPool::CommandPool(const Device& device, uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags) noexcept
        : mDevice(device)
    {
        VkCommandPoolCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        createInfo.queueFamilyIndex = queueFamilyIndex;
        createInfo.flags = flags;
        mCreateFlags = flags;

        if (vkCreateCommandPool(mDevice.getHandle(), &createInfo, nullptr, &mCommandPool) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create vulkan command pool");
        }
    }

    CommandPool::~CommandPool() noexcept
    {
        mCommandBuffers.clear();

        vkDestroyCommandPool(mDevice.getHandle(), mCommandPool, nullptr);
    }

    void CommandPool::reset() noexcept
    {
        vkResetCommandPool(mDevice.getHandle(), mCommandPool, 0);

        for (auto& cmd : mCommandBuffers)
        {
            cmd->onPoolReset();
        }
    }

    CommandBuffer* CommandPool::allocateCommandBuffer(VkCommandBufferLevel level)
    {
        for (CommandBuffer* cmd : mCommandBuffers)
        {
            if (cmd->getRefCount() > 1)
                continue; // 已被外部引用

            CommandBuffer* result = nullptr;
            if (cmd->getState() == CommandBuffer::State::Pending &&
                cmd->getSignalFence().getStatus() == VK_SUCCESS)
            {
                cmd->onExecuted();
            }

            if (cmd->getState() == CommandBuffer::State::Initial)
            {
                result = cmd;
            }

            if (cmd->getState() == CommandBuffer::State::Executable &&
                canResetCommandBuffer())
            {
                cmd->reset();
                result = cmd;
            }

            if (result)
            {
                return result;
            }
        }

        // 如果已分配的CommandBuffer全部被占用, 新创建新的CommandBuffer
        VkCommandBufferAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = mCommandPool;
        allocateInfo.level = level;
        allocateInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(mDevice.getHandle(), &allocateInfo, &commandBuffer);

        CommandBuffer* newCommandBuffer = new CommandBuffer(*this, commandBuffer);
        mCommandBuffers.push_back(newCommandBuffer);
        return newCommandBuffer;
    }
}
