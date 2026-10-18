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
            // maxUnusedFrames: 这个资源连续多少帧没被任何 pass 用到才允许 TransientResources 池
            // 回收它, 默认 TransientResources::kDefaultMaxUnusedFrames。给那些"是否需要"会随场景
            // 内容(而非分辨率/画质设置)逐帧变化、且变化频率可能比默认冷却期长的资源调大这个值,
            // 避免反复创建销毁造成的显存分配抖动——例子见 DirectLightingPass 里 LightVolumeDepth
            // 的用法
            BufferHandle createBuffer(HString name, const rhi::BufferDesc& desc, uint32_t maxUnusedFrames = TransientResources::kDefaultMaxUnusedFrames);

            BufferHandle importBuffer(HString name, BufferResource::PhysicalResource* physicalResource);

            BufferHandle accessBuffer(BufferHandle handle, rhi::BufferUsage usage, AccessType accessType);

            TextureHandle createTexture(HString name, const rhi::TextureDesc& desc, uint32_t maxUnusedFrames = TransientResources::kDefaultMaxUnusedFrames);

            // 把一张不由 render graph 创建、生命周期由外部持有的物理纹理接入图里。defaultState
            // 是调用方对 render graph 许下的一份双向承诺，不是"render graph 会自动帮你查/转换到
            // 这个状态"：
            //   - 进入本帧第一个使用该纹理的 pass 之前，调用方必须保证它已经真实处于 defaultState
            //     （首次导入的纹理如果物理初始状态不是 defaultState——比如刚创建、真实布局是
            //     UNDEFINED——调用方要在 render graph 之外先用一次性 barrier 把它转过去，参考
            //     DefaultResources::ensureCreated 的写法）；
            //   - 作为回报，render graph 保证在本帧最后一个使用它的 pass 结束后，把它转回
            //     defaultState，这样下一帧的这份承诺才继续成立。
            // 只要某一帧这份承诺没有兑现（真实状态跟声明的 defaultState 对不上），效果是那一帧里
            // 第一次访问该纹理时插入的 barrier 会基于错误的 oldState 去转换，物理布局和 Vulkan
            // 校验层跟踪到的布局就会分道扬镳，通常在后续某次描述符访问时才会暴露出来
            // （VUID-vkCmdDraw-None-09600 一类的报错）。
            TextureHandle importTexture(HString name, TextureResource::PhysicalResource* physicalResource, rhi::ResourceState defaultState);

            TextureHandle createSubTexture(HString name, TextureHandle handle, const rhi::TextureViewDesc& desc);

            // 同步 texture 与其 subtexture 之间的版本
            TextureHandle syncTextures(TextureHandle source, TextureHandle target);

            TextureHandle addColorAttachment(TextureHandle handle, rhi::AttachmentLoadOp loadOp, rhi::ClearColorValue clearColor = rhi::ClearColorValue(float4(0.0f, 0.0f, 0.0f, 1.0)));

            TextureHandle addDepthAttachment(TextureHandle handle, rhi::AttachmentLoadOp loadOp, bool write, float clearDepth = 0.0f);

            TextureHandle addStencilAttachment(TextureHandle handle, rhi::AttachmentLoadOp loadOp, bool write, uint8_t clearStencil = 0u);

            TextureHandle addDepthStencilAttachment(
                TextureHandle handle,
                rhi::AttachmentLoadOp depthLoadOp,
                bool writeDepth,
                rhi::AttachmentLoadOp stencilLoadOp,
                bool writeStencil,
                rhi::ClearDepthStencilValue clearDepthStencil = { 0.0f, 0 }
            );

            TextureHandle sample(TextureHandle handle);

            TextureHandle storage(TextureHandle handle, AccessType accessType);

            TextureHandle transfer(TextureHandle handle, AccessType accessType);

            void setRenderArea(rhi::RenderArea renderArea);

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

        RenderGraph(TransientResources& transientResources, rhi::Swapchain& swapchain) noexcept;

        ~RenderGraph() noexcept;

        void compile();

        void execute(ExecuteContext& context);

        template <typename Data, typename Setup, typename Execute>
        TComputePass<Data, Execute>& addComputePass(HString name, Setup&& setup, Execute&& execute, PassFlags flags = PassFlags::None) noexcept
        {
            return addPass<TComputePass, Data, Setup, Execute>(name, flags, std::forward<Setup>(setup), std::forward<Execute>(execute));
        }

        // 不像 addComputePass/addGraphicsPass/addPresentPass 那样带 flags 参数 —— TTransferPass
        // 无条件跳过 beginPass/endPass(见 Pass.h 的注释), Skip_Begin_Pass/Skip_End_Pass 对它没有
        // 任何意义, 暴露一个"传了也不起作用"的参数只会带来困惑。execute 应为
        // void(ExecuteContext&, Data&) lambda, 只做 copyBuffer/copyBufferToTexture/clearBuffer/
        // insertBarriers, 不应 bindPipeline/dispatch/draw。
        template <typename Data, typename Setup, typename Execute>
        TTransferPass<Data, Execute>& addTransferPass(HString name, Setup&& setup, Execute&& execute) noexcept
        {
            return addPass<TTransferPass, Data, Setup, Execute>(name, PassFlags::None, std::forward<Setup>(setup), std::forward<Execute>(execute));
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
            mHasPresentPass = true;
            return pass;
        }

        BufferHandle importBuffer(HString name, BufferResource::PhysicalResource* physicalResource);

        // defaultState 的语义见上面 Builder::importTexture 的注释——这里是给不经过 Setup
        // lambda、直接持有 RenderGraph 引用的调用方（而不是 Builder）用的同一个入口，语义完全一致
        TextureHandle importTexture(HString name, TextureResource::PhysicalResource* physicalResource, rhi::ResourceState defaultState);

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

        rhi::Buffer* getBuffer(BufferHandle handle) const noexcept;

        rhi::TextureView* getTexture(TextureHandle handle) const noexcept;

        std::string toGraphviz() const;

        bool hasPresentPass() const noexcept
        {
            return mHasPresentPass;
        }

    private:
        template <typename RESOURCE, typename... Args>
        TResourceHandle<RESOURCE> createResource(Args&&... args)
        {
            TResourceHandle<RESOURCE> handle(static_cast<uint16_t>(mResources.size()), kInvalidIndex, 0);
            mResources.emplace_back(mArena.create<RESOURCE>(std::forward<Args>(args)...));
            return handle;
        }

        TextureHandle createSubTexture(HString name, TextureHandle texture, const rhi::TextureViewDesc& desc);

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
                    assert(resource->isImported() ||
                        (resource->getType() == ResourceType::SubTexture && static_cast<SubTextureResource*>(resource)->getTextureResource()->isImported()));
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

            passNode->getPass()->recordAccess<RESOURCE>(handle, usage, accessType);

            return newHandle;
        }

        // By the time a pass's `execute` runs — inside RenderGraph::execute() — every pass
        // added this frame has already run its `setup`.
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

        void setupGraphicsPassAttachments(PassNode* passNode);

        void inferBarriers(PassNode* passNode);

        static bool subresourceRangeEqual(const rhi::TextureRange& a, const rhi::TextureRange& b);

        // 判断 a 是否包含 b
        static bool subresourceRangeCovers(const rhi::TextureRange& a, const rhi::TextureRange& b);

        static bool checkSubresourceRangeHasOverlap(const rhi::TextureRange& a, const rhi::TextureRange& b);

        static std::optional<rhi::TextureRange> intersectSubresourceRange(const rhi::TextureRange& a, const rhi::TextureRange& b);

        class SubresourceRangeSet
        {
        public:
            SubresourceRangeSet(Arena& arena) : mRanges(StlAllocator<rhi::TextureRange>(arena))
            {
                mRanges.reserve(4);
            }

            void insert(const rhi::TextureRange& range);

            void clear() noexcept
            {
                mRanges.clear();
            }

            std::span<const rhi::TextureRange> getRanges() const noexcept
            {
                return mRanges;
            }

        private:
            std::optional<rhi::TextureRange> tryMergeSubresourceRanges(const rhi::TextureRange& a, const rhi::TextureRange& b);

        private:
            ArenaVector<rhi::TextureRange> mRanges;
        };

        static uint8_t subtractSubresourceRange(const rhi::TextureRange& range, const rhi::TextureRange& cover, std::array<rhi::TextureRange, 5>& result);

        ArenaVector<rhi::TextureRange> subtractSubresourceRangeSet(const rhi::TextureRange& range, const SubresourceRangeSet& set);

        static rhi::ResourceState inferBufferState(rhi::BufferUsage usage, AccessType accessType, PassType passType);

        static rhi::ResourceState inferTextureState(rhi::TextureUsage usage, AccessType accessType, rhi::TextureAspect aspect, PassType passType);

        // 针对单个BufferUsage bit, 直接推断其pipeline stage与access mask, 避免在RAR覆盖判断中反复调用通用函数
        static rhi::ResourceState inferSingleBufferUsageState(rhi::BufferUsage usageBit, AccessType accessType, PassType passType);

    private:
        TransientResources& mTransientResources;
        rhi::Swapchain& mSwapchain;
        Arena mArena;
        DAG mDAG;
        Blackboard mBlackboard;
        ArenaVector<VirtualResource*> mResources;
        ArenaVector<ResourceNode*> mResourceNodes;
        ArenaVector<PassNode*> mPassNodes;
        bool mHasPresentPass = false;
    };
}
