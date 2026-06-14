#include "Pass.h"
#include "../RenderBackend.h"

#include <Core/Math/Vector.h>

namespace hyp::rg
{
    ExecuteContext::ExecuteContext(RenderGraph& renderGraph, RenderBackend& renderBackend, vk::CommandBuffer& commandBuffer, vk::Swapchain& swapchain, uint32_t frameIndex) :
        mRenderGraph(renderGraph),
        mRenderBackend(renderBackend),
        mCommandBuffer(commandBuffer),
        mSwapchain(swapchain),
        mFrameIndex(frameIndex)
    {

    }

    vk::Buffer* ExecuteContext::getBuffer(BufferHandle handle) const noexcept
    {
        return mRenderGraph.getBuffer(handle);
    }

    vk::ImageView* ExecuteContext::getTexture(TextureHandle handle) const noexcept
    {
        return mRenderGraph.getTexture(handle);
    }

    void ExecuteContext::beginRenderPass() const noexcept
    {
        mPass->beginRenderPass(mCommandBuffer);
    }

    void ExecuteContext::endRenderPass() const noexcept
    {
        mPass->endRenderPass(mCommandBuffer);
    }

    std::span<const VkBufferMemoryBarrier2> ExecuteContext::getPassPrevBufferBarriers(uint16_t begin, uint16_t count) const noexcept
    {
        return mRenderGraph.getTransientResources().getPassPrevBufferBarriers(begin, count);
    }

    std::span<const VkImageMemoryBarrier2> ExecuteContext::getPassPrevImageBarriers(uint16_t begin, uint16_t count) const noexcept
    {
        return mRenderGraph.getTransientResources().getPassPrevImageBarriers(begin, count);
    }

    std::span<const VkBufferMemoryBarrier2> ExecuteContext::getPassPostBufferBarriers(uint16_t begin, uint16_t count) const noexcept
    {
        return mRenderGraph.getTransientResources().getPassPostBufferBarriers(begin, count);
    }

    std::span<const VkImageMemoryBarrier2> ExecuteContext::getPassPostImageBarriers(uint16_t begin, uint16_t count) const noexcept
    {
        return mRenderGraph.getTransientResources().getPassPostImageBarriers(begin, count);
    }

    PassBase::PassBase(HString name, PassType type, PassFlags flags, Arena& arena)
        : mName(name), mType(type), mFlags(flags), mDependencyRecords(StlAllocator<PassDependencyRecord>(arena))
    {
        mDependencyRecords.reserve(8);
    }

    void PassBase::prevExecute(ExecuteContext& context)
    {
        context.setPass(this);

        auto& cmd = context.getCommandBuffer();

        static constexpr float4 kPassColor[] = {
            { 0.1765f, 0.6118f, 0.8588f, 1.0f },
            { 0.9216f, 0.3412f, 0.3412f, 1.0f },
            { 0.1529f, 0.6823f, 0.3765f, 1.0f }
        };

        cmd.beginDebugUtilsLabelExt(getName().toCString(), kPassColor[static_cast<uint8_t>(mType)]);

        if (mPrevBufferBarrierCount || mPrevImageBarrierCount)
        {
            cmd.pipelineBarrier2(
                context.getPassPrevBufferBarriers(mPrevBufferBarrierBegin, mPrevBufferBarrierCount),
                context.getPassPrevImageBarriers(mPrevImageBarrierBegin, mPrevImageBarrierCount)
            );
        }
    }

    void PassBase::postExecute(ExecuteContext& context)
    {
        auto& cmd = context.getCommandBuffer();

        if (mPostBufferBarrierCount || mPostImageBarrierCount)
        {
            cmd.pipelineBarrier2(
                context.getPassPostBufferBarriers(mPostBufferBarrierBegin, mPostBufferBarrierCount),
                context.getPassPostImageBarriers(mPostImageBarrierBegin, mPostImageBarrierCount)
            );
        }

        cmd.endDebugUtilsLabelExt();
    }

    void GraphicsPass::devirtualize(TransientResources& transientResources)
    {
        mRenderPass = transientResources.acquireRenderPass(mRenderPassKey);
        mFramebufferKey.renderPass = mRenderPass;
        mFramebuffer = transientResources.acquireFramebuffer(mFramebufferKey);
    }

    void GraphicsPass::resolveFramebufferKey(RenderGraph& renderGraph)
    {
        TextureHandle validTextureHandle = mColorAttachmentCount ? mColorAttachments[0] : mDepthStencilAttachment;
        vk::ImageView* validImageView = renderGraph.getTexture(validTextureHandle);
        uint32_t mipLevel = validImageView->getRange().baseMipLevel;
        VkExtent3D extent = validImageView->getImage().getExtent();
        extent.width = std::max(extent.width / (1 << mipLevel), 1u);
        extent.height = std::max(extent.height / (1 << mipLevel), 1u);
        uint32_t layerCount = validImageView->getRange().layerCount;
        uint8_t samples = validImageView->getImage().getSamples();

        mFramebufferKey.width = static_cast<uint16_t>(extent.width);
        mFramebufferKey.height = static_cast<uint16_t>(extent.height);
        mFramebufferKey.layers = static_cast<uint16_t>(layerCount);
        mFramebufferKey.samples = static_cast<uint16_t>(samples);

        if (mRenderArea.extent.width == 0 || mRenderArea.extent.height == 0)
        {
            mRenderArea.extent = { extent.width, extent.height };
        }

        for (uint32_t i = 0; i < mColorAttachmentCount; ++i)
        {
            mFramebufferKey.colors[i] = renderGraph.getTexture(mColorAttachments[i])->getHandle();
        }
        if (mDepthStencilAttachment.isValid())
            mFramebufferKey.depthStencil = renderGraph.getTexture(mDepthStencilAttachment)->getHandle();
    }

    void GraphicsPass::beginRenderPass(vk::CommandBuffer& cmd) noexcept
    {
        VkClearValue clearValues[kMaxColorAttachmentCount + 1];
        for (uint8_t i = 0; i < mColorAttachmentCount; ++i)
        {
            clearValues[i].color = convertToVkClearColorValue(mClearColorValues[i]);
        }
        clearValues[mColorAttachmentCount].depthStencil = convertToVkClearDepthStencilValue(mClearDepthStencilValue);

        cmd.beginRenderPass(
            mRenderPass->getHandle(),
            mFramebuffer->getHandle(),
            mRenderArea,
            std::span<const VkClearValue>(clearValues, mColorAttachmentCount + (mDepthStencilAttachment.isValid() ? 1 : 0))
        );
    }

    void GraphicsPass::endRenderPass(vk::CommandBuffer& cmd) noexcept
    {
        cmd.endRenderPass();
    }

    void GraphicsPass::addColorAttachment(TextureHandle color, VkAttachmentLoadOp loadOp, ClearColorValue clearColorValue)
    {
        mColorAttachments[mColorAttachmentCount] = color;
        mColorAttachmentLoadOps[mColorAttachmentCount] = convertFromVkAttachmentLoadOp(loadOp);
        mClearColorValues[mColorAttachmentCount] = clearColorValue;
        ++mColorAttachmentCount;
    }

    void GraphicsPass::addDepthStencilAttachment(TextureHandle depthStencil, VkAttachmentLoadOp depthLoadOp, VkAttachmentLoadOp stencilLoadOp, ClearDepthStencilValue clearDepthStencilValue)
    {
        mDepthStencilAttachment = depthStencil;
        mDepthAttachmentLoadOp = convertFromVkAttachmentLoadOp(depthLoadOp);
        mStencilAttachmentLoadOp = convertFromVkAttachmentLoadOp(stencilLoadOp);
        mClearDepthStencilValue = clearDepthStencilValue;
    }

    void GraphicsPass::setRenderArea(VkRect2D renderArea)
    {
        mRenderArea = renderArea;
    }

    void PresentPass::setSwapchain(vk::Swapchain* swapchain) noexcept
    {
        mSwapchain = swapchain;

        AttachmentKey& attachmentKey = mRenderPassKey.colorAttachments[0];
        attachmentKey.format = convertFromVkFormat(mSwapchain->getSurfaceFormat().format);
        attachmentKey.loadOp = AttachmentLoadOp::Clear;
        attachmentKey.storeOp = AttachmentStoreOp::Store;
        attachmentKey.layout = ImageLayout::Color_Attachment_Optimal;

        mRenderPassKey.flags |= RenderPassFlags::Present;
    }

    void PresentPass::devirtualize(TransientResources& transientResources)
    {
        mRenderPass = transientResources.acquireRenderPass(mRenderPassKey);

        VkExtent2D swapchainExtent = mSwapchain->getExtent();
        assert(mFramebuffers.size() == mSwapchain->getImageCount());
        for (uint32_t i = 0; i < mSwapchain->getImageCount(); ++i)
        {
            FramebufferKey framebufferKey;
            framebufferKey.renderPass = mRenderPass;
            framebufferKey.width = swapchainExtent.width;
            framebufferKey.height = swapchainExtent.height;
            framebufferKey.layers = 1;
            framebufferKey.samples = 1;
            framebufferKey.colors[0] = mSwapchain->getImageView(i)->getHandle();

            mFramebuffers[i] = transientResources.acquireFramebuffer(framebufferKey);
        }
    }

    void PresentPass::beginRenderPass(vk::CommandBuffer& cmd) noexcept
    {
        VkClearValue clearValue = { .color = {0.0f, 0.0f, 0.0f, 1.0f} };
        VkClearValue clearValues[] = { clearValue };
        cmd.beginRenderPass(
            mRenderPass->getHandle(),
            mFramebuffers[mSwapchain->getCurrentImageIndex()]->getHandle(),
            { {0, 0}, mSwapchain->getExtent() },
            clearValues
        );
    }

    void PresentPass::endRenderPass(vk::CommandBuffer& cmd) noexcept
    {
        cmd.endRenderPass();
    }

    PassNode::PassNode(DAG& dag, PassBase* pass) noexcept
        : Node(dag, Type::Pass), mPass(pass)
    {
        if (mPass->getType() == PassType::Present)
            sideEffect();
    }

    PassNode::~PassNode()
    {
        
    }

    void PassNode::execute(ExecuteContext& context) const
    {
        mPass->execute(context);
    }

}
