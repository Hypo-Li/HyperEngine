#pragma once
#include "Resource.h"
#include "../Vulkan/Swapchain.h"
#include "../Vulkan/CommandBuffer.h"

#include <Core/Memory/Allocator.h>

namespace hyp
{
    class RenderBackend;
}

namespace hyp::rg
{
    class PassBase;
    class RenderGraph;
    class ExecuteContext
    {
        friend class PassBase;
    public:
        ExecuteContext(RenderGraph& renderGraph, RenderBackend& renderBackend, vk::CommandBuffer& commandBuffer, vk::Swapchain& swapchain, uint32_t frameIndex);

        RenderBackend& getRenderBackend() const noexcept
        {
            return mRenderBackend;
        }

        vk::CommandBuffer& getCommandBuffer() const noexcept
        {
            return mCommandBuffer;
        }

        vk::Swapchain& getSwapchain() const noexcept
        {
            return mSwapchain;
        }

        uint32_t getFrameIndex() const noexcept
        {
            return mFrameIndex;
        }

        vk::Buffer* getBuffer(BufferHandle handle) const noexcept;

        vk::ImageView* getTexture(TextureHandle handle) const noexcept;

        void beginRenderPass() const noexcept;

        void endRenderPass() const noexcept;

    private:
        void setPass(PassBase* pass) noexcept
        {
            mPass = pass;
        }

        std::span<const VkBufferMemoryBarrier2> getPassPrevBufferBarriers(uint16_t begin, uint16_t count) const noexcept;

        std::span<const VkImageMemoryBarrier2> getPassPrevImageBarriers(uint16_t begin, uint16_t count) const noexcept;

        std::span<const VkBufferMemoryBarrier2> getPassPostBufferBarriers(uint16_t begin, uint16_t count) const noexcept;

        std::span<const VkImageMemoryBarrier2> getPassPostImageBarriers(uint16_t begin, uint16_t count) const noexcept;

    private:
        RenderGraph& mRenderGraph;
        RenderBackend& mRenderBackend;
        vk::CommandBuffer& mCommandBuffer;
        vk::Swapchain& mSwapchain;
        uint32_t mFrameIndex;
        PassBase* mPass;
    };

    enum class PassFlags
    {
        None = 0,
        Skip_Begin_Render_Pass = 1 << 1,
        Skip_End_Render_Pass = 1 << 2,
    };
    ENABLE_ENUM_CLASS_BITWISE(PassFlags)

        class RenderGraph;
    class PassNode;
    class PassBase
    {
        friend class RenderGraph;
        friend class PassNode;
    public:
        PassBase(HString name, PassType type, PassFlags flags, Arena& arena);

        virtual ~PassBase() noexcept = default;

        virtual size_t getTypeSize() const noexcept = 0;

        HString getName() const noexcept
        {
            return mName;
        }

        PassType getType() const noexcept
        {
            return mType;
        }

        PassFlags getFlags() const noexcept
        {
            return mFlags;
        }

        virtual void beginRenderPass(vk::CommandBuffer& cmd) noexcept = 0;

        virtual void endRenderPass(vk::CommandBuffer& cmd) noexcept = 0;

    protected:
        virtual void devirtualize(TransientResources& transientResources) = 0;

        virtual void execute(ExecuteContext&) = 0;

        void prevExecute(ExecuteContext& context);

        void postExecute(ExecuteContext& context);

        template <typename RESOURCE>
        void access(TResourceHandle<RESOURCE> handle, RESOURCE::Usage usage, AccessType accessType)
        {
            mDependencyRecords.emplace_back(handle, static_cast<uint16_t>(usage), accessType);
        }

        std::span<const PassDependencyRecord> getDependencyRecords() const
        {
            return mDependencyRecords;
        }

        void setPrevBufferBarrierRange(uint16_t begin, uint16_t count)
        {
            mPrevBufferBarrierBegin = begin;
            mPrevBufferBarrierCount = count;
        }

        void setPrevImageBarrierRange(uint16_t begin, uint16_t count)
        {
            mPrevImageBarrierBegin = begin;
            mPrevImageBarrierCount = count;
        }

        void setPostBufferBarrierRange(uint16_t begin, uint16_t count)
        {
            mPostBufferBarrierBegin = begin;
            mPostBufferBarrierCount = count;
        }

        void setPostImageBarrierRange(uint16_t begin, uint16_t count)
        {
            mPostImageBarrierBegin = begin;
            mPostImageBarrierCount = count;
        }

    protected:
        uint16_t mPrevBufferBarrierBegin = 0;
        uint16_t mPrevBufferBarrierCount = 0;
        uint16_t mPrevImageBarrierBegin = 0;
        uint16_t mPrevImageBarrierCount = 0;

        uint16_t mPostBufferBarrierBegin = 0;
        uint16_t mPostBufferBarrierCount = 0;
        uint16_t mPostImageBarrierBegin = 0;
        uint16_t mPostImageBarrierCount = 0;

    private:
        HString mName;
        PassType mType;
        PassFlags mFlags;

        ArenaVector<PassDependencyRecord> mDependencyRecords;
    };

    class ComputePass : public PassBase
    {
    public:
        ComputePass(HString name, PassFlags flags, Arena& arena) : PassBase(name, PassType::Compute, flags, arena) {}

        void beginRenderPass(vk::CommandBuffer& cmd) noexcept override {}

        void endRenderPass(vk::CommandBuffer& cmd) noexcept override {}

    protected:
        virtual void devirtualize(TransientResources& transientResources) override {}
    };

    class GraphicsPass : public PassBase
    {
        friend class RenderGraph;
    public:
        GraphicsPass(HString name, PassFlags flags, Arena& arena) : PassBase(name, PassType::Graphics, flags, arena) {}

        void beginRenderPass(vk::CommandBuffer& cmd) noexcept override;

        void endRenderPass(vk::CommandBuffer& cmd) noexcept override;

    protected:
        virtual void devirtualize(TransientResources& transientResources) override;

        void resolveFramebufferKey(RenderGraph& renderGraph);

        void addColorAttachment(TextureHandle color, VkAttachmentLoadOp loadOp, ClearColorValue clearColorValue);

        void addDepthStencilAttachment(TextureHandle depthStencil, VkAttachmentLoadOp depthLoadOp, VkAttachmentLoadOp stencilLoadOp, ClearDepthStencilValue clearDepthStencilValue);

        void setRenderArea(VkRect2D renderArea);

    protected:
        uint8_t mColorAttachmentCount = 0;
        ClearColorValue mClearColorValues[kMaxColorAttachmentCount];
        ClearDepthStencilValue mClearDepthStencilValue;
        std::array<TextureHandle, kMaxColorAttachmentCount> mColorAttachments;
        TextureHandle mDepthStencilAttachment;
        VkRect2D mRenderArea = { {0, 0}, {0, 0} };

        TRef<vk::RenderPass> mRenderPass;
        TRef<vk::Framebuffer> mFramebuffer;

    private:
        RenderPassKey mRenderPassKey;
        FramebufferKey mFramebufferKey;
        std::array<AttachmentLoadOp, kMaxColorAttachmentCount> mColorAttachmentLoadOps = { AttachmentLoadOp::DontCare };
        AttachmentLoadOp mDepthAttachmentLoadOp = AttachmentLoadOp::DontCare;
        AttachmentLoadOp mStencilAttachmentLoadOp = AttachmentLoadOp::DontCare;
    };

    class PresentPass : public PassBase
    {
    public:
        PresentPass(HString name, PassFlags flags, Arena& arena) : PassBase(name, PassType::Present, flags, arena) {}

        void setSwapchain(vk::Swapchain* swapchain) noexcept;

        void beginRenderPass(vk::CommandBuffer& cmd) noexcept override;

        void endRenderPass(vk::CommandBuffer& cmd) noexcept override;

    protected:
        virtual void devirtualize(TransientResources& transientResources) override;

    protected:
        TRef<vk::Swapchain> mSwapchain;
        TRef<vk::RenderPass> mRenderPass;
        std::array<TRef<vk::Framebuffer>, 3> mFramebuffers;

    private:
        RenderPassKey mRenderPassKey;
    };

    // Execute should be void(ExecuteContext&, Data&) lambda
    template <typename Data, typename Execute>
    class TComputePass : public ComputePass
    {
    public:
        TComputePass(HString name, PassFlags flags, Arena& arena, Execute&& execute) noexcept
            : ComputePass(name, flags, arena), mExecute(std::move(execute))
        {
        }

        virtual size_t getTypeSize() const noexcept override
        {
            return sizeof(TComputePass<Data, Execute>);
        }

        virtual void execute(ExecuteContext& context) override
        {
            prevExecute(context);
            mExecute(context, mData);
            postExecute(context);
        }

        Data& getData() noexcept
        {
            return mData;
        }

    private:
        Data mData;
        Execute mExecute;
    };

    template <typename Data, typename Execute>
    class TGraphicsPass : public GraphicsPass
    {
    public:
        TGraphicsPass(HString name, PassFlags flags, Arena& arena, Execute&& execute) noexcept
            : GraphicsPass(name, flags, arena), mExecute(std::move(execute))
        {
        }

        virtual size_t getTypeSize() const noexcept override
        {
            return sizeof(TGraphicsPass<Data, Execute>);
        }

        virtual void execute(ExecuteContext& context) override
        {
            prevExecute(context);
            auto& cmd = context.getCommandBuffer();
            if (!enumHasAllFlags(getFlags(), PassFlags::Skip_Begin_Render_Pass))
                beginRenderPass(cmd);
            mExecute(context, mData);
            if (!enumHasAllFlags(getFlags(), PassFlags::Skip_End_Render_Pass))
                endRenderPass(cmd);
            postExecute(context);
        }

        Data& getData() noexcept
        {
            return mData;
        }

    private:
        Data mData;
        Execute mExecute;
    };

    template <typename Data, typename Execute>
    class TPresentPass : public PresentPass
    {
    public:
        TPresentPass(HString name, PassFlags flags, Arena& arena, Execute&& execute) noexcept
            : PresentPass(name, flags, arena), mExecute(std::move(execute))
        {
        }

        virtual size_t getTypeSize() const noexcept override
        {
            return sizeof(TPresentPass<Data, Execute>);
        }

        virtual void execute(ExecuteContext& context) override
        {
            prevExecute(context);
            auto& cmd = context.getCommandBuffer();
            if (!enumHasAllFlags(getFlags(), PassFlags::Skip_Begin_Render_Pass))
                beginRenderPass(cmd);
            mExecute(context, mData);
            if (!enumHasAllFlags(getFlags(), PassFlags::Skip_End_Render_Pass))
                endRenderPass(cmd);
            postExecute(context);
        }

        Data& getData() noexcept
        {
            return mData;
        }

    private:
        Data mData;
        Execute mExecute;
    };

    class PassNode final : public DAG::Node
    {
    public:
        PassNode(DAG& dag, PassBase* pass) noexcept;

        ~PassNode();

        PassBase* getPass() const noexcept
        {
            return mPass;
        }

        void execute(ExecuteContext& context) const;

    private:
        PassBase* mPass;
    };
}
