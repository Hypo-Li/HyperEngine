#include "Pass.h"
#include "../FrameContext.h"
#include "../RenderBackend.h"

#include <Core/Math/Vector.h>

#include <cassert>

namespace hyp::rg
{
    ExecuteContext::ExecuteContext(
        RenderGraph& renderGraph,
        RenderBackend& renderBackend,
        rhi::CommandList& commandList,
        rhi::Swapchain& swapchain,
        uint32_t frameIndex
    ) : mRenderGraph(renderGraph),
        mRenderBackend(renderBackend),
        mCommandList(commandList),
        mSwapchain(swapchain),
        mFrameIndex(frameIndex)
    {

    }

    rhi::Buffer* ExecuteContext::getBuffer(BufferHandle handle) const noexcept
    {
        return mRenderGraph.getBuffer(handle);
    }

    rhi::TextureView* ExecuteContext::getTexture(TextureHandle handle) const noexcept
    {
        return mRenderGraph.getTexture(handle);
    }

    void ExecuteContext::beginPass() const noexcept
    {
        mPass->beginPass(mCommandList);
    }

    void ExecuteContext::endPass() const noexcept
    {
        mPass->endPass(mCommandList);
    }

    std::span<const rhi::BufferBarrier> ExecuteContext::getPrePassBufferBarriers(uint16_t begin, uint16_t count) const noexcept
    {
        return mRenderGraph.getTransientResources().getPrePassBufferBarriers(begin, count);
    }

    std::span<const rhi::TextureBarrier> ExecuteContext::getPrePassTextureBarriers(uint16_t begin, uint16_t count) const noexcept
    {
        return mRenderGraph.getTransientResources().getPrePassTextureBarriers(begin, count);
    }

    std::span<const rhi::BufferBarrier> ExecuteContext::getPostPassBufferBarriers(uint16_t begin, uint16_t count) const noexcept
    {
        return mRenderGraph.getTransientResources().getPostPassBufferBarriers(begin, count);
    }

    std::span<const rhi::TextureBarrier> ExecuteContext::getPostPassTextureBarriers(uint16_t begin, uint16_t count) const noexcept
    {
        return mRenderGraph.getTransientResources().getPostPassTextureBarriers(begin, count);
    }

    PassBase::PassBase(HString name, PassType type, PassFlags flags, Arena& arena)
        : mName(name), mType(type), mFlags(flags), mDependencyRecords(StlAllocator<PassDependencyRecord>(arena))
    {
        mDependencyRecords.reserve(8);
    }

    void PassBase::preExecute(ExecuteContext& context)
    {
        context.setPass(this);

        auto& cmdList = context.getCommandList();

        // 按 PassType 的声明顺序(Compute/Graphics/Present/Transfer)一一对应, 供 GPU 调试工具
        // (RenderDoc/PIX/Xcode GPU Frame Capture 等)在 timeline 上按 pass 类型区分颜色。
        // Transfer 选用暖色调的琥珀/金色 —— 一是与已有的蓝(Compute)/红(Graphics)/绿(Present)
        // 在色相上离得足够远(现有三者集中在 0°~202° 这段, 琥珀色在 ~45°, 是从红色顺时针方向
        // 最容易分辨的下一个色相), 二是很多 GPU profiler 习惯用暖黄/橙色标记纯数据搬运
        // (copy/upload)类操作, 符合直觉。
        static constexpr float4 kPassColor[] = {
            { 0.1765f, 0.6118f, 0.8588f, 1.0f }, // Compute: 蓝
            { 0.9216f, 0.3412f, 0.3412f, 1.0f }, // Graphics: 红
            { 0.1529f, 0.6823f, 0.3765f, 1.0f }, // Present: 绿
            { 1.0000f, 0.7020f, 0.0000f, 1.0f }  // Transfer: 琥珀/金
        };

        cmdList.beginDebugGroup(getName().toCString(), kPassColor[static_cast<uint8_t>(mType)]);

        if (mPreBufferBarrierCount || mPreTextureBarrierCount)
        {
            cmdList.insertBarriers(
                context.getPrePassBufferBarriers(mPreBufferBarrierBegin, mPreBufferBarrierCount),
                context.getPrePassTextureBarriers(mPreTextureBarrierBegin, mPreTextureBarrierCount)
            );
        }
    }

    void PassBase::postExecute(ExecuteContext& context)
    {
        auto& cmdList = context.getCommandList();

        if (mPostBufferBarrierCount || mPostTextureBarrierCount)
        {
            cmdList.insertBarriers(
                context.getPostPassBufferBarriers(mPostBufferBarrierBegin, mPostBufferBarrierCount),
                context.getPostPassTextureBarriers(mPostTextureBarrierBegin, mPostTextureBarrierCount)
            );
        }

        cmdList.endDebugGroup();
    }

    void ComputePass::beginPass(rhi::CommandList& cmdList) noexcept
    {
        cmdList.beginComputePass();
    }

    void ComputePass::endPass(rhi::CommandList& cmdList) noexcept
    {
        cmdList.endComputePass();
    }

    void TransferPass::beginPass(rhi::CommandList&) noexcept
    {
        // 正常情况下不会走到这里 —— TTransferPass::execute() 从不调用 beginPass()/endPass(),
        // 见其注释。这里断言而不是悄悄地什么都不做, 是为了让"execute 回调里误调用了
        // context.beginPass()"这种误用在调试期就暴露出来, 而不是表现为一次诡异的空操作。
        assert(false && "TransferPass never enters a pass scope; use ComputePass/GraphicsPass if the execute callback needs to dispatch/draw");
    }

    void TransferPass::endPass(rhi::CommandList&) noexcept
    {
        assert(false && "TransferPass never enters a pass scope; use ComputePass/GraphicsPass if the execute callback needs to dispatch/draw");
    }

    void GraphicsPass::beginPass(rhi::CommandList& cmdList) noexcept
    {
        cmdList.beginRenderPass(
            mRenderArea,
            mDepthStencilAttachmentInfo,
            { mColorAttachmentInfos.data(), mColorAttachmentCount}
        );
    }

    void GraphicsPass::endPass(rhi::CommandList& cmdList) noexcept
    {
        cmdList.endRenderPass();
    }

    void GraphicsPass::addColorAttachment(TextureHandle color, rhi::AttachmentLoadOp loadOp, rhi::ClearColorValue clearColorValue)
    {
        mColorAttachments[mColorAttachmentCount] = color;
        mColorAttachmentInfos[mColorAttachmentCount].loadOp = loadOp;
        mColorAttachmentInfos[mColorAttachmentCount].clearColor = clearColorValue;
        ++mColorAttachmentCount;
    }

    void GraphicsPass::addDepthStencilAttachment(
        TextureHandle depthStencil,
        rhi::AttachmentLoadOp depthLoadOp,
        rhi::AttachmentLoadOp stencilLoadOp,
        rhi::ClearDepthStencilValue clearDepthStencilValue
    )
    {
        mDepthStencilAttachment = depthStencil;
        mDepthStencilAttachmentInfo.depthLoadOp = depthLoadOp;
        mDepthStencilAttachmentInfo.stencilLoadOp = stencilLoadOp;
        mDepthStencilAttachmentInfo.clearDepthStencil = clearDepthStencilValue;
    }

    void GraphicsPass::setRenderArea(rhi::RenderArea renderArea)
    {
        mRenderArea = renderArea;
    }

    void PresentPass::setSwapchain(rhi::Swapchain* swapchain) noexcept
    {
        mSwapchain = swapchain;
    }

    void PresentPass::beginPass(rhi::CommandList& cmdList) noexcept
    {
        rhi::TextureView* swapchainTextureView = mSwapchain->getCurrentTextureView();
        rhi::TextureBarrier presentBarrier;
        presentBarrier.texture = &swapchainTextureView->getTexture();
        presentBarrier.range = swapchainTextureView->getDesc().range;
        presentBarrier.oldState = rhi::ResourceState::Present;
        presentBarrier.newState = rhi::ResourceState::Color_Attachment;

        cmdList.insertBarriers({}, { &presentBarrier, 1 });

        uint2 swapchainExtent = mSwapchain->getExtent();
        mColorAttachmentInfo.textureView = mSwapchain->getCurrentTextureView();
        mColorAttachmentInfo.loadOp = rhi::AttachmentLoadOp::Clear;
        mColorAttachmentInfo.storeOp = rhi::AttachmentStoreOp::Store;
        mColorAttachmentInfo.clearColor = rhi::ClearColorValue{ float4(0.0, 0.0, 0.0, 0.0) };
        rhi::RenderArea renderArea = { 0, 0, swapchainExtent.x, swapchainExtent.y };
        rhi::DepthStencilAttachmentInfo depthStencilAttachmentInfo{};
        cmdList.beginRenderPass(
            renderArea,
            depthStencilAttachmentInfo,
            { &mColorAttachmentInfo, 1 }
        );
    }

    void PresentPass::endPass(rhi::CommandList& cmdList) noexcept
    {
        cmdList.endRenderPass();

        rhi::TextureView* swapchainTextureView = mSwapchain->getCurrentTextureView();
        rhi::TextureBarrier presentBarrier;
        presentBarrier.texture = &swapchainTextureView->getTexture();
        presentBarrier.range = swapchainTextureView->getDesc().range;
        presentBarrier.oldState = rhi::ResourceState::Color_Attachment;
        presentBarrier.newState = rhi::ResourceState::Present;
        cmdList.insertBarriers({}, { &presentBarrier, 1 });
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
