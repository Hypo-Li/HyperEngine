#pragma once
#include <utility>
#include <optional>

#include "Pass.h"
#include "Blackboard.h"

#include <Core/Memory/Allocator.h>

namespace hyp::rg
{
    class RenderGraph
    {
    public:
        class Builder
        {
            friend class RenderGraph;
        public:
            BufferHandle createBuffer(HString name, const BufferDescription& desc);

            BufferHandle importBuffer(HString name, BufferResource::PhysicalResource* physicalResource);

            BufferHandle accessBuffer(BufferHandle handle, BufferUsage usage, AccessType accessType);

            TextureHandle createTexture(HString name, const TextureDescription& desc);

            TextureHandle importTexture(HString name, TextureResource::PhysicalResource* physicalResource, VkImageLayout defaultLayout);

            TextureHandle createSubTexture(HString name, TextureHandle handle, const SubTextureDescription& desc);

            // 同步 texture 与其 subtexture 之间的版本
            TextureHandle syncTextures(TextureHandle source, TextureHandle target);

            TextureHandle addColorAttachment(TextureHandle handle, VkAttachmentLoadOp loadOp, ClearColorValue clearColor = ClearColorValue::BlackOpaque);

            TextureHandle addDepthAttachment(TextureHandle handle, VkAttachmentLoadOp loadOp, bool write, ClearDepthValue clearDepth = ClearDepthValue::Zero);

            TextureHandle addStencilAttachment(TextureHandle handle, VkAttachmentLoadOp loadOp, bool write, uint8_t clearStencil = 0u);

            TextureHandle addDepthStencilAttachment(
                TextureHandle handle,
                VkAttachmentLoadOp depthLoadOp,
                bool writeDepth,
                VkAttachmentLoadOp stencilLoadOp,
                bool writeStencil,
                ClearDepthStencilValue clearDepthStencil = { ClearDepthValue::Zero, 0 }
            );

            TextureHandle sample(TextureHandle handle);

            TextureHandle storage(TextureHandle handle, AccessType accessType);

            TextureHandle transfer(TextureHandle handle, AccessType accessType);

            void setRenderArea(VkRect2D renderArea);

            void markSideEffect();

            Blackboard& getBlackboard() noexcept
            {
                return mRenderGraph.getBlackboard();
            }

        private:
            Builder(RenderGraph& renderGraph, PassNode* passNode) noexcept;

        private:
            RenderGraph& mRenderGraph;
            PassNode* mPassNode;
        };

        RenderGraph(TransientResources& transientResources, vk::Swapchain& swapchain) noexcept;

        ~RenderGraph() noexcept;

        void compile();

        void execute(ExecuteContext& context);

        template <typename Data, typename Setup, typename Execute>
        TComputePass<Data, Execute>& addComputePass(HString name, Setup&& setup, Execute&& execute, PassFlags flags = PassFlags::None) noexcept
        {
            return addPass<TComputePass, Data, Setup, Execute>(name, flags, std::forward<Setup>(setup), std::forward<Execute>(execute));
        }

        template <typename Data, typename Setup, typename Execute>
        TGraphicsPass<Data, Execute>& addGraphicsPass(HString name, Setup&& setup, Execute&& execute, PassFlags flags = PassFlags::None) noexcept
        {
            return addPass<TGraphicsPass, Data, Setup, Execute>(name, flags, std::forward<Setup>(setup), std::forward<Execute>(execute));
        }

        template <typename Data, typename Setup, typename Execute>
        TPresentPass<Data, Execute>& addPresentPass(HString name, Setup&& setup, Execute&& execute, PassFlags flags = PassFlags::None) noexcept
        {
            auto& pass = addPass<TPresentPass, Data, Setup, Execute>(name, flags, std::forward<Setup>(setup), std::forward<Execute>(execute));
            pass.setSwapchain(&mSwapchain);
            return pass;
        }

        BufferHandle importBuffer(HString name, BufferResource::PhysicalResource* physicalResource);

        TextureHandle importTexture(HString name, TextureResource::PhysicalResource* physicalResource, VkImageLayout defaultLayout);

        VirtualResource* getResource(ResourceHandle handle) const noexcept
        {
            assert(handle.getResourceId() < mResources.size());
            return mResources[handle.getResourceId()];
        }

        TransientResources& getTransientResources() const noexcept
        {
            return mTransientResources;
        }

        Arena& getArena() noexcept
        {
            return mArena;
        }

        Blackboard& getBlackboard() noexcept
        {
            return mBlackboard;
        }

        vk::Buffer* getBuffer(BufferHandle handle) const noexcept;

        vk::ImageView* getTexture(TextureHandle handle) const noexcept;

        std::string toGraphviz() const;

    private:
        template <typename RESOURCE, typename... Args>
        TResourceHandle<RESOURCE> createResource(Args&&... args)
        {
            TResourceHandle<RESOURCE> handle(static_cast<uint16_t>(mResources.size()), kInvalidIndex, 0);
            mResources.emplace_back(mArena.create<RESOURCE>(std::forward<Args>(args)...));
            return handle;
        }

        TextureHandle createSubTexture(HString name, TextureHandle texture, const SubTextureDescription& desc);

        template <typename RESOURCE, typename... Args>
        TResourceHandle<RESOURCE> importResource(Args&&... args)
        {
            TResourceHandle<RESOURCE> handle(static_cast<uint16_t>(mResources.size()), kInvalidIndex, 0);
            mResources.emplace_back(mArena.create<RESOURCE>(std::forward<Args>(args)...));
            return handle;
        }

        template <typename RESOURCE>
        TResourceHandle<RESOURCE> access(PassNode* passNode, TResourceHandle<RESOURCE> handle, RESOURCE::Usage usage, AccessType accessType)
        {
            uint16_t newNodeId = handle.getNodeId();
            uint16_t newVersion = handle.getVersion();

            const bool readResource = enumHasAnyFlags(accessType, AccessType::Read_Only | AccessType::Stencil_Read_Only);
            const bool writeResource = enumHasAnyFlags(accessType, AccessType::Write_Only | AccessType::Stencil_Write_Only);
            
            VirtualResource* resource = mResources[handle.getResourceId()];

            if (readResource)
            {
                ResourceNode* resourceNode;
                if (handle.getNodeId() != kInvalidIndex)
                {
                    // 之前有写入过该资源, 所以有节点, 可以读
                    resourceNode = mResourceNodes[handle.getNodeId()];
                }
                else
                {
                    // 只有导入的资源才能未写先读
                    assert(resource->isImported());
                    newNodeId = static_cast<uint16_t>(mResourceNodes.size());
                    resourceNode = mArena.create<ResourceNode>(mDAG, handle.getResourceId());
                    mResourceNodes.emplace_back(resourceNode);
                }

                mDAG.link(resourceNode, passNode);
            }

            if (writeResource)
            {
                newNodeId = static_cast<uint16_t>(mResourceNodes.size());
                ++newVersion;
                uint16_t effectiveResourceId = handle.getResourceId();
                if (resource->getType() == ResourceType::SubTexture)
                {
                    effectiveResourceId = static_cast<SubTextureResource*>(resource)->getTextureResourceId();
                }
                ResourceNode* resourceNode = mArena.create<ResourceNode>(mDAG, effectiveResourceId);
                mResourceNodes.emplace_back(resourceNode);

                mDAG.link(passNode, resourceNode);
            }

            TResourceHandle<RESOURCE> newHandle(handle.getResourceId(), newNodeId, newVersion);

            passNode->getPass()->access<RESOURCE>(handle, usage, accessType);

            return newHandle;
        }

        template <template<typename, typename> typename PASS, typename Data, typename Setup, typename Execute>
        PASS<Data, Execute>& addPass(HString name, PassFlags flags, Setup&& setup, Execute&& execute) noexcept
        {
            PASS<Data, Execute>* pass = mArena.create<PASS<Data, Execute>>(name, flags, mArena, std::forward<Execute>(execute));
            PassNode* passNode = mArena.create<PassNode>(mDAG, pass);
            mPassNodes.emplace_back(passNode);
            Builder builder(*this, passNode);
            setup(builder, pass->getData());
            return *pass;
        }

        void clear();

        void inferRenderPassKey(PassNode* passNode);

        void inferBarriers(PassNode* passNode);

        static bool subresourceRangeEqual(const VkImageSubresourceRange& a, const VkImageSubresourceRange& b);

        static bool checkSubresourceRangeHasOverlap(const VkImageSubresourceRange& a, const VkImageSubresourceRange& b);

        static std::optional<VkImageSubresourceRange> intersectSubresourceRange(const VkImageSubresourceRange& a, const VkImageSubresourceRange& b);

        class SubresourceRangeSet
        {
        public:
            SubresourceRangeSet(Arena& arena) : mRanges(StlAllocator<VkImageSubresourceRange>(arena))
            {
                mRanges.reserve(4);
            }

            void insert(const VkImageSubresourceRange& range);

            void clear() noexcept
            {
                mRanges.clear();
            }

            std::span<const VkImageSubresourceRange> getRanges() const noexcept
            {
                return mRanges;
            }

        private:
            std::optional<VkImageSubresourceRange> tryMergeSubresourceRanges(const VkImageSubresourceRange& a, const VkImageSubresourceRange& b);

        private:
            ArenaVector<VkImageSubresourceRange> mRanges;
        };

        static uint8_t subtractSubresourceRange(const VkImageSubresourceRange& range, const VkImageSubresourceRange& cover, std::array<VkImageSubresourceRange, 5>& result);

        ArenaVector<VkImageSubresourceRange> subtractSubresourceRangeSet(const VkImageSubresourceRange& range, const SubresourceRangeSet& set);

        static VkImageLayout inferImageLayout(TextureUsage usage, AccessType accessType);

        static void inferBufferStageAccessMask(PassType passType, BufferUsage usage, AccessType accessType, bool isDst, VkPipelineStageFlags2& stageMask, VkAccessFlags2& accessMask);

        static void inferImageStageAccessMask(PassType passType, TextureUsage usage, AccessType accessType, bool isDst, VkPipelineStageFlags2& stageMask, VkAccessFlags2& accessMask);

    private:
        TransientResources& mTransientResources;
        vk::Swapchain& mSwapchain;
        Arena mArena;
        DAG mDAG;
        Blackboard mBlackboard;
        ArenaVector<VirtualResource*> mResources;
        ArenaVector<ResourceNode*> mResourceNodes;
        ArenaVector<PassNode*> mPassNodes;
    };
}
