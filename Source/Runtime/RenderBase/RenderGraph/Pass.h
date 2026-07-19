#pragma once
#include "Resource.h"
#include <RHI/Swapchain.h>
#include <RHI/CommandList.h>

#include <Core/Memory/Allocator.h>

#include <functional>

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
        ExecuteContext(
            RenderGraph& renderGraph,
            RenderBackend& renderBackend,
            rhi::CommandList& commandList,
            rhi::Swapchain& swapchain,
            uint32_t frameIndex
        );

        RenderBackend& getRenderBackend() const noexcept
        {
            return mRenderBackend;
        }

        rhi::CommandList& getCommandList() const noexcept
        {
            return mCommandList;
        }

        rhi::Swapchain& getSwapchain() const noexcept
        {
            return mSwapchain;
        }

        uint32_t getFrameIndex() const noexcept
        {
            return mFrameIndex;
        }

        rhi::Buffer* getBuffer(BufferHandle handle) const noexcept;

        rhi::TextureView* getTexture(TextureHandle handle) const noexcept;

        void beginPass() const noexcept;

        void endPass() const noexcept;

    private:
        void setPass(PassBase* pass) noexcept
        {
            mPass = pass;
        }

        std::span<const rhi::BufferBarrier> getPrePassBufferBarriers(uint16_t begin, uint16_t count) const noexcept;

        std::span<const rhi::TextureBarrier> getPrePassTextureBarriers(uint16_t begin, uint16_t count) const noexcept;

        std::span<const rhi::BufferBarrier> getPostPassBufferBarriers(uint16_t begin, uint16_t count) const noexcept;

        std::span<const rhi::TextureBarrier> getPostPassTextureBarriers(uint16_t begin, uint16_t count) const noexcept;

    private:
        RenderGraph& mRenderGraph;
        RenderBackend& mRenderBackend;
        rhi::CommandList& mCommandList;
        rhi::Swapchain& mSwapchain;
        uint32_t mFrameIndex;
        PassBase* mPass;
    };

    enum class PassFlags
    {
        None = 0,
        Skip_Begin_Pass = 1 << 1,
        Skip_End_Pass = 1 << 2,
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

        virtual void beginPass(rhi::CommandList& cmdList) noexcept = 0;

        virtual void endPass(rhi::CommandList& cmdList) noexcept = 0;

    protected:
        virtual void execute(ExecuteContext&) = 0;

        void preExecute(ExecuteContext& context);

        void postExecute(ExecuteContext& context);

        template <typename RESOURCE>
        void recordAccess(TResourceHandle<RESOURCE> handle, RESOURCE::Usage usage, AccessType accessType)
        {
            mDependencyRecords.emplace_back(handle, static_cast<uint16_t>(usage), accessType);
        }

        std::span<const PassDependencyRecord> getDependencyRecords() const noexcept
        {
            return mDependencyRecords;
        }

        std::span<PassDependencyRecord> getDependencyRecords() noexcept
        {
            return mDependencyRecords;
        }

        void setPreBufferBarrierRange(uint16_t begin, uint16_t count)
        {
            mPreBufferBarrierBegin = begin;
            mPreBufferBarrierCount = count;
        }

        void setPreTextureBarrierRange(uint16_t begin, uint16_t count)
        {
            mPreTextureBarrierBegin = begin;
            mPreTextureBarrierCount = count;
        }

        void setPostBufferBarrierRange(uint16_t begin, uint16_t count)
        {
            mPostBufferBarrierBegin = begin;
            mPostBufferBarrierCount = count;
        }

        void setPostTextureBarrierRange(uint16_t begin, uint16_t count)
        {
            mPostTextureBarrierBegin = begin;
            mPostTextureBarrierCount = count;
        }

    protected:
        uint16_t mPreBufferBarrierBegin = 0;
        uint16_t mPreBufferBarrierCount = 0;
        uint16_t mPreTextureBarrierBegin = 0;
        uint16_t mPreTextureBarrierCount = 0;

        uint16_t mPostBufferBarrierBegin = 0;
        uint16_t mPostBufferBarrierCount = 0;
        uint16_t mPostTextureBarrierBegin = 0;
        uint16_t mPostTextureBarrierCount = 0;

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

        void beginPass(rhi::CommandList& cmdList) noexcept override;

        void endPass(rhi::CommandList& cmdList) noexcept override;
    };

    // 专用于"只做 copyBuffer/copyBufferToTexture/clearBuffer/insertBarriers、不 dispatch 任何
    // 着色器"的 pass —— 这类操作在所有后端都是顶层 command encoder 操作, 从不需要、也不能处于
    // 任何 pass 的 begin/end 范围内(WebGPU 尤其严格: copyBufferToBuffer 只能在 GPUCommandEncoder
    // 上调用, 若已经 begin 了 GPUComputePassEncoder/GPURenderPassEncoder 则会直接断言失败)。
    //
    // 在 TransferPass 出现之前, 这类"纯上传"的工作只能借用 ComputePass、并手动传
    // Skip_Begin_Pass(/Skip_End_Pass)来避免被自动套进 compute pass 的 begin/end —— 这是一个容易
    // 忘记传的 flag(Vulkan 没有这个限制, 忘记传在纯 Vulkan 环境下不会暴露问题, 只有在 WebGPU
    // 后端才会触发断言), 已经在 Test/GSplat2/GSplatPass2.cpp 里踩过一次。TransferPass 把这件事
    // 从"记得传对 flag"变成"这类 pass 的 beginPass/endPass 天生就是空实现, 根本没有忘记的余地"——
    // 见 TTransferPass::execute() 的注释, 它连 Skip_Begin_Pass/Skip_End_Pass 判断都不做, 无条件
    // 跳过 begin/end。
    class TransferPass : public PassBase
    {
    public:
        TransferPass(HString name, PassFlags flags, Arena& arena) : PassBase(name, PassType::Transfer, flags, arena) {}

        // 空实现, 且正常情况下不会被调用到(TTransferPass::execute() 从不调用它们)—— 只有当
        // execute 回调里误调用了 context.beginPass()/endPass() 才会走到这里, 因此故意断言失败,
        // 而不是悄悄地什么都不做: 需要 dispatch/draw 的工作应该用 ComputePass/GraphicsPass,
        // 而不是把 TransferPass 当成它们的替代品。
        void beginPass(rhi::CommandList& cmdList) noexcept override;

        void endPass(rhi::CommandList& cmdList) noexcept override;
    };

    static constexpr uint32_t kMaxColorAttachmentCount = 8;
    class GraphicsPass : public PassBase
    {
        friend class RenderGraph;
    public:
        GraphicsPass(HString name, PassFlags flags, Arena& arena) : PassBase(name, PassType::Graphics, flags, arena) {}

        void beginPass(rhi::CommandList& cmdList) noexcept override;

        void endPass(rhi::CommandList& cmdList) noexcept override;

    protected:
        void addColorAttachment(TextureHandle color, rhi::AttachmentLoadOp loadOp, rhi::ClearColorValue clearColorValue);

        void addDepthStencilAttachment(
            TextureHandle depthStencil,
            rhi::AttachmentLoadOp depthLoadOp,
            rhi::AttachmentLoadOp stencilLoadOp,
            rhi::ClearDepthStencilValue clearDepthStencilValue
        );

        void setRenderArea(rhi::RenderArea renderArea);

    protected:
        rhi::RenderArea mRenderArea = rhi::RenderArea{ 0, 0, 0u, 0u };
        uint8_t mColorAttachmentCount = 0;
        std::array<rhi::ColorAttachmentInfo, kMaxColorAttachmentCount> mColorAttachmentInfos;
        rhi::DepthStencilAttachmentInfo mDepthStencilAttachmentInfo{};

        std::array<TextureHandle, kMaxColorAttachmentCount> mColorAttachments;
        TextureHandle mDepthStencilAttachment;

        std::array<uint32_t, kMaxColorAttachmentCount> mColorAccessRecordIndices{ UINT32_MAX };
        uint32_t mDepthStencilAccessRecordIndex = UINT32_MAX;
    };

    class PresentPass : public PassBase
    {
    public:
        PresentPass(HString name, PassFlags flags, Arena& arena) : PassBase(name, PassType::Present, flags, arena) {}

        void setSwapchain(rhi::Swapchain* swapchain) noexcept;

        void beginPass(rhi::CommandList& cmdList) noexcept override;

        void endPass(rhi::CommandList& cmdList) noexcept override;

    protected:
        rhi::Swapchain* mSwapchain;
        rhi::ColorAttachmentInfo mColorAttachmentInfo;
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
            preExecute(context);
            auto& cmdList = context.getCommandList();
            if (!enumHasAllFlags(getFlags(), PassFlags::Skip_Begin_Pass))
                beginPass(cmdList);
            mExecute(context, mData);
            if (!enumHasAllFlags(getFlags(), PassFlags::Skip_End_Pass))
                endPass(cmdList);
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

    // Execute should be void(ExecuteContext&, Data&) lambda —— 只应调用 copyBuffer/
    // copyBufferToTexture/clearBuffer/insertBarriers 这类顶层 command encoder 操作, 不应
    // bindPipeline/dispatch/draw(那是 ComputePass/GraphicsPass 的职责)。
    template <typename Data, typename Execute>
    class TTransferPass : public TransferPass
    {
    public:
        TTransferPass(HString name, PassFlags flags, Arena& arena, Execute&& execute) noexcept
            : TransferPass(name, flags, arena), mExecute(std::move(execute))
        {
        }

        virtual size_t getTypeSize() const noexcept override
        {
            return sizeof(TTransferPass<Data, Execute>);
        }

        virtual void execute(ExecuteContext& context) override
        {
            preExecute(context);
            // 无条件跳过 beginPass()/endPass()
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
            preExecute(context);
            auto& cmdList = context.getCommandList();
            if (!enumHasAllFlags(getFlags(), PassFlags::Skip_Begin_Pass))
                beginPass(cmdList);
            mExecute(context, mData);
            if (!enumHasAllFlags(getFlags(), PassFlags::Skip_End_Pass))
                endPass(cmdList);
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
            preExecute(context);
            auto& cmdList = context.getCommandList();
            if (!enumHasAllFlags(getFlags(), PassFlags::Skip_Begin_Pass))
                beginPass(cmdList);
            mExecute(context, mData);
            if (!enumHasAllFlags(getFlags(), PassFlags::Skip_End_Pass))
                endPass(cmdList);
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
