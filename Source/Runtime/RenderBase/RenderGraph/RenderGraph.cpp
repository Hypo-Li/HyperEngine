#include "RenderGraph.h"

#include <algorithm>
#include <sstream>
#include <memory>

namespace hyp::rg
{
    BufferHandle RenderGraph::Builder::createBuffer(HString name, const rhi::BufferDesc& desc, uint32_t maxUnusedFrames)
    {
        return mRenderGraph.createResource<BufferResource>(name, desc, mRenderGraph.getArena(), maxUnusedFrames);
    }

    BufferHandle RenderGraph::Builder::importBuffer(HString name, BufferResource::PhysicalResource* physicalResource)
    {
        return mRenderGraph.importBuffer(name, physicalResource);
    }

    BufferHandle RenderGraph::Builder::accessBuffer(BufferHandle handle, rhi::BufferUsage usage, AccessType accessType)
    {
        return mRenderGraph.access<BufferResource>(mPassNode, handle, usage, accessType);
    }

    TextureHandle RenderGraph::Builder::createTexture(HString name, const rhi::TextureDesc& desc, uint32_t maxUnusedFrames)
    {
        return mRenderGraph.createResource<TextureResource>(name, desc, mRenderGraph.getArena(), maxUnusedFrames);
    }

    TextureHandle RenderGraph::Builder::importTexture(HString name, TextureResource::PhysicalResource* physicalResource, rhi::ResourceState defaultState)
    {
        return mRenderGraph.importTexture(name, physicalResource, defaultState);
    }

    TextureHandle RenderGraph::Builder::createSubTexture(HString name, TextureHandle handle, const rhi::TextureViewDesc& desc)
    {
        return mRenderGraph.createSubTexture(name, handle, desc);
    }

    TextureHandle RenderGraph::Builder::syncTextures(TextureHandle source, TextureHandle target)
    {
        auto checkSyncResources = [this, source, target]() -> bool
            {
                bool isValid = false;
                VirtualResource* srcResource = mRenderGraph.getResource(source);
                VirtualResource* tgtResource = mRenderGraph.getResource(target);
                if (srcResource->getType() == ResourceType::Texture && tgtResource->getType() == ResourceType::SubTexture)
                {
                    TextureResource* src = static_cast<TextureResource*>(srcResource);
                    SubTextureResource* tgt = static_cast<SubTextureResource*>(tgtResource);
                    isValid = src == tgt->getTextureResource();
                }
                else if (srcResource->getType() == ResourceType::SubTexture && tgtResource->getType() == ResourceType::Texture)
                {
                    SubTextureResource* src = static_cast<SubTextureResource*>(srcResource);
                    TextureResource* tgt = static_cast<TextureResource*>(tgtResource);
                    isValid = src->getTextureResource() == tgt;
                }
                return isValid;
            };

        assert(checkSyncResources());
        assert(source.getVersion() > target.getVersion());
        TextureHandle newHandle = target;
        newHandle.mVersion = source.getVersion();
        newHandle.mNodeId = source.getNodeId();
        return newHandle;
    }

    TextureHandle RenderGraph::Builder::addColorAttachment(TextureHandle handle, rhi::AttachmentLoadOp loadOp, rhi::ClearColorValue clearColorValue)
    {
        AccessType accessType = AccessType::Write_Only;
        if (loadOp == rhi::AttachmentLoadOp::Load)
            accessType |= AccessType::Read_Only;

        PassBase* pass = mPassNode->getPass();
        assert(pass->getType() == PassType::Graphics);
        static_cast<GraphicsPass*>(pass)->addColorAttachment(handle, loadOp, clearColorValue);

        return mRenderGraph.access<TextureResource>(mPassNode, handle, rhi::TextureUsage::Color_Attachment, accessType);
    }

    TextureHandle RenderGraph::Builder::addDepthAttachment(TextureHandle handle, rhi::AttachmentLoadOp loadOp, bool write, float clearDepthValue)
    {
        AccessType accessType = AccessType::None;
        if (loadOp == rhi::AttachmentLoadOp::Load)
            accessType |= AccessType::Read_Only;
        if (loadOp == rhi::AttachmentLoadOp::Clear || write)
            accessType |= AccessType::Write_Only;

        PassBase* pass = mPassNode->getPass();
        assert(pass->getType() == PassType::Graphics);
        static_cast<GraphicsPass*>(pass)->addDepthStencilAttachment(handle, loadOp, rhi::AttachmentLoadOp::Dont_Care, { clearDepthValue, 0u });

        return mRenderGraph.access<TextureResource>(mPassNode, handle, rhi::TextureUsage::Depth_Stencil_Attachment, accessType);
    }

    TextureHandle RenderGraph::Builder::addStencilAttachment(TextureHandle handle, rhi::AttachmentLoadOp loadOp, bool write, uint8_t clearStencil)
    {
        AccessType accessType = AccessType::None;
        if (loadOp == rhi::AttachmentLoadOp::Load)
            accessType |= AccessType::Stencil_Read_Only;
        if (loadOp == rhi::AttachmentLoadOp::Clear || write)
            accessType |= AccessType::Stencil_Write_Only;

        PassBase* pass = mPassNode->getPass();
        assert(pass->getType() == PassType::Graphics);
        static_cast<GraphicsPass*>(pass)->addDepthStencilAttachment(handle, rhi::AttachmentLoadOp::Dont_Care, loadOp, { 0.0f, clearStencil });

        return mRenderGraph.access<TextureResource>(mPassNode, handle, rhi::TextureUsage::Depth_Stencil_Attachment, accessType);
    }

    TextureHandle RenderGraph::Builder::addDepthStencilAttachment(
        TextureHandle handle,
        rhi::AttachmentLoadOp depthLoadOp,
        bool writeDepth,
        rhi::AttachmentLoadOp stencilLoadOp,
        bool writeStencil,
        rhi::ClearDepthStencilValue clearDepthStencilValue
    )
    {
        AccessType accessType = AccessType::None;
        if (depthLoadOp == rhi::AttachmentLoadOp::Load)
            accessType |= AccessType::Read_Only;
        if (depthLoadOp == rhi::AttachmentLoadOp::Clear || writeDepth)
            accessType |= AccessType::Write_Only;
        if (stencilLoadOp == rhi::AttachmentLoadOp::Load)
            accessType |= AccessType::Stencil_Read_Only;
        if (stencilLoadOp == rhi::AttachmentLoadOp::Clear || writeStencil)
            accessType |= AccessType::Stencil_Write_Only;

        PassBase* pass = mPassNode->getPass();
        assert(pass->getType() == PassType::Graphics);
        static_cast<GraphicsPass*>(pass)->addDepthStencilAttachment(handle, depthLoadOp, stencilLoadOp, clearDepthStencilValue);

        return mRenderGraph.access<TextureResource>(mPassNode, handle, rhi::TextureUsage::Depth_Stencil_Attachment, accessType);
    }

    TextureHandle RenderGraph::Builder::sample(TextureHandle handle)
    {
        return mRenderGraph.access<TextureResource>(mPassNode, handle, rhi::TextureUsage::Sampled, AccessType::Read_Only);
    }

    TextureHandle RenderGraph::Builder::storage(TextureHandle handle, AccessType accessType)
    {
        return mRenderGraph.access<TextureResource>(mPassNode, handle, rhi::TextureUsage::Storage, accessType);
    }

    TextureHandle RenderGraph::Builder::transfer(TextureHandle handle, AccessType accessType)
    {
        rhi::TextureUsage usage = rhi::TextureUsage::None;
        if (enumHasAnyFlags(accessType, AccessType::Read_Only))
        {
            usage |= rhi::TextureUsage::Transfer_Src;
        }
        if (enumHasAnyFlags(accessType, AccessType::Write_Only))
        {
            usage |= rhi::TextureUsage::Transfer_Dst;
        }
        return mRenderGraph.access<TextureResource>(mPassNode, handle, usage, accessType);
    }

    void RenderGraph::Builder::setRenderArea(rhi::RenderArea renderArea)
    {
        PassBase* pass = mPassNode->getPass();
        assert(pass->getType() == PassType::Graphics);
        static_cast<GraphicsPass*>(pass)->setRenderArea(renderArea);
    }

    void RenderGraph::Builder::markSideEffect()
    {
        mPassNode->sideEffect();
    }

    RenderGraph::Builder::Builder(RenderGraph& renderGraph, PassNode* passNode) noexcept
        : mRenderGraph(renderGraph), mPassNode(passNode)
    {

    }

    RenderGraph::RenderGraph(TransientResources& transientResources, rhi::Swapchain& swapchain) noexcept :
        mTransientResources(transientResources),
        mSwapchain(swapchain),
        mArena(1024 * 1024),
        mDAG(mArena),
        mResources(StlAllocator<VirtualResource*>(mArena)),
        mResourceNodes(StlAllocator<ResourceNode*>(mArena)),
        mPassNodes(StlAllocator<PassNode*>(mArena))
    {
        mResources.reserve(256);
        mResourceNodes.reserve(256);
        mPassNodes.reserve(64);
    }

    RenderGraph::~RenderGraph() noexcept
    {
        clear();
        mTransientResources.clearBarriers();
    }

    void RenderGraph::compile()
    {
        mDAG.cull();

        auto activePassNodesEnd = std::stable_partition(
            mPassNodes.begin(),
            mPassNodes.end(),
            [](PassNode* passNode)
            {
                return !passNode->isCulled();
            }
        );

        uint32_t activePassNodeCount = static_cast<uint16_t>(activePassNodesEnd - mPassNodes.begin());

        // 为所有resource记录访问记录
        for (uint32_t i = 0; i < activePassNodeCount; ++i)
        {
            PassNode* passNode = mPassNodes[i];
            assert(!passNode->isCulled());
            PassBase* pass = passNode->getPass();
            uint32_t colorAttachmentIndex = 0;

            for (auto& dependencyRecord : pass->getDependencyRecords())
            {
                VirtualResource* resource = getResource(dependencyRecord.resource);
                resource->registerPass(passNode);

                if (resource->getType() == ResourceType::Buffer)
                {
                    BufferResource* bufferResource = static_cast<BufferResource*>(resource);
                    dependencyRecord.accessRecordIndex = bufferResource->addAccessRecord(
                        passNode,
                        static_cast<rhi::BufferUsage>(dependencyRecord.usage),
                        dependencyRecord.accessType
                    );
                }
                else if (resource->getType() == ResourceType::Texture)
                {
                    TextureResource* textureResource = static_cast<TextureResource*>(resource);
                    const rhi::TextureDesc& textureDesc = textureResource->getDescription();

                    dependencyRecord.accessRecordIndex = textureResource->addAccessRecord(
                        passNode,
                        static_cast<rhi::TextureUsage>(dependencyRecord.usage),
                        dependencyRecord.accessType,
                        rhi::TextureRange{
                            rhi::getFormatAspect(textureDesc.format),
                            0, textureDesc.mipCount,
                            0, textureDesc.layerCount
                        }
                    );
                    rhi::TextureUsage usage = static_cast<rhi::TextureUsage>(dependencyRecord.usage);
                    if (usage == rhi::TextureUsage::Color_Attachment)
                    {
                        static_cast<GraphicsPass*>(pass)->mColorAccessRecordIndices[colorAttachmentIndex++] = dependencyRecord.accessRecordIndex;
                    }
                    else if (usage == rhi::TextureUsage::Depth_Stencil_Attachment)
                    {
                        static_cast<GraphicsPass*>(pass)->mDepthStencilAccessRecordIndex = dependencyRecord.accessRecordIndex;
                    }
                }
                else if (resource->getType() == ResourceType::SubTexture)
                {
                    SubTextureResource* subTextureResource = static_cast<SubTextureResource*>(resource);
                    const rhi::TextureViewDesc& subTextureDesc = subTextureResource->getDescription();

                    dependencyRecord.accessRecordIndex = subTextureResource->getTextureResource()->addAccessRecord(
                        passNode,
                        static_cast<rhi::TextureUsage>(dependencyRecord.usage),
                        dependencyRecord.accessType,
                        subTextureDesc.range
                    );
                    rhi::TextureUsage usage = static_cast<rhi::TextureUsage>(dependencyRecord.usage);
                    if (usage == rhi::TextureUsage::Color_Attachment)
                    {
                        static_cast<GraphicsPass*>(pass)->mColorAccessRecordIndices[colorAttachmentIndex++] = dependencyRecord.accessRecordIndex;
                    }
                    else if (usage == rhi::TextureUsage::Depth_Stencil_Attachment)
                    {
                        static_cast<GraphicsPass*>(pass)->mDepthStencilAccessRecordIndex = dependencyRecord.accessRecordIndex;
                    }

                    subTextureResource->getTextureResource()->registerPass(passNode);
                }
            }
        }

        // Devirtualize Resources
        for (VirtualResource* resource : mResources)
        {
            bool registered = resource->getType() == ResourceType::SubTexture ?
                static_cast<SubTextureResource*>(resource)->getTextureResource()->isRegistered() : resource->isRegistered();
            if (!resource->isDevirtualized() && registered)
            {
                resource->devirtualize(mTransientResources);
            }
        }

        // Setup Graphics Pass Attachments
        for (uint32_t i = 0; i < activePassNodeCount; ++i)
        {
            if (mPassNodes[i]->getPass()->getType() == PassType::Graphics)
            {
                setupGraphicsPassAttachments(mPassNodes[i]);
            }
        }

        // infer Barriers
        for (uint32_t i = 0; i < activePassNodeCount; ++i)
        {
            inferBarriers(mPassNodes[i]);
        }

        mTransientResources.destroyUselessResources();
    }

    void RenderGraph::execute(ExecuteContext& context)
    {
        for (PassNode* passNode : mPassNodes)
        {
            if (passNode->isCulled())
                break;
            passNode->execute(context);
        }
    }

    BufferHandle RenderGraph::importBuffer(HString name, BufferResource::PhysicalResource* physicalResource)
    {
        return importResource<BufferResource>(name, physicalResource, getArena());
    }

    TextureHandle RenderGraph::importTexture(HString name, TextureResource::PhysicalResource* physicalResource, rhi::ResourceState defaultState)
    {
        return importResource<TextureResource>(name, physicalResource, defaultState, getArena());
    }

    rhi::Buffer* RenderGraph::getBuffer(BufferHandle handle) const noexcept
    {
        return static_cast<BufferResource*>(getResource(handle))->getPhysicalResource();
    }

    rhi::TextureView* RenderGraph::getTexture(TextureHandle handle) const noexcept
    {
        VirtualResource* resource = getResource(ResourceHandle(handle));
        if (resource->getType() == ResourceType::Texture)
        {
            return static_cast<TextureResource*>(resource)->getPhysicalResource();
        }
        else if (resource->getType() == ResourceType::SubTexture)
        {
            return static_cast<SubTextureResource*>(resource)->getPhysicalResource();
        }
        return nullptr;
    }

    std::string RenderGraph::toGraphviz() const
    {
        std::ostringstream oss;
        oss << "digraph G {\n";
        oss << "  rankdir=LR;\n";  // 从左到右的方向布局（可选）

        std::unordered_map<uintptr_t, uint16_t> pointerIndexMap;
        uint16_t nodeIndex = 0;
        for (DAG::Node* node : mDAG.getNodes())
        {
            pointerIndexMap.emplace(reinterpret_cast<uintptr_t>(node), nodeIndex);
            
            std::string escapedName;
            std::string nodeStyle;
            if (node->getType() == DAG::Node::Type::Pass)
            {
                PassNode* passNode = static_cast<PassNode*>(node);
                PassBase* pass = passNode->getPass();
                std::string passColor[] = {
                    R"("#2D9CDB")",
                    R"("#EB5757")",
                    R"("#27AE60")"
                };
                escapedName = pass->getName().toStringView();
                nodeStyle = R"(", shape=box, style="filled", fillcolor=)" + passColor[int(pass->getType())];
            }
            else
            {
                ResourceNode* resourceNode = static_cast<ResourceNode*>(node);
                VirtualResource* resource = mResources[resourceNode->getResourceId()];
                escapedName = resource->getName().toStringView();
                nodeStyle = R"(", shape=ellipse, style="filled", fillcolor="#f79646")";
            }
            oss << "  n" << nodeIndex
                << " [label=\"" << escapedName << nodeStyle << "];\n";

            ++nodeIndex;
        }

        // 输出边关系
        for (const auto& edge : mDAG.getEdges())
        {
            std::string edgeStyle;
            if (edge.from->getType() == DAG::Node::Type::Pass)
            {
                // pass to resource, write
                edgeStyle = R"( [color="#be4f4c"])";
            }
            else
            {
                // resource to pass, read
                edgeStyle = R"( [color="#9bbb59"])";
            }
            oss << "  n" << pointerIndexMap.at(reinterpret_cast<uintptr_t>(edge.from))
                << " -> n" << pointerIndexMap.at(reinterpret_cast<uintptr_t>(edge.to)) << edgeStyle << ";\n";
        }

        oss << "}\n";
        return oss.str();
    }

    TextureHandle RenderGraph::createSubTexture(HString name, TextureHandle texture, const rhi::TextureViewDesc& desc)
    {
        TextureHandle handle(static_cast<uint16_t>(mResources.size()), texture.getNodeId(), texture.getVersion());
        assert(mResources.size() > texture.getResourceId());
        TextureResource* textureResource = static_cast<TextureResource*>(mResources[texture.getResourceId()]);
        mResources.emplace_back(mArena.create<SubTextureResource>(name, texture.getResourceId(), textureResource, desc));
        return handle;
    }

    void RenderGraph::clear()
    {
        mDAG.clear();

        for (auto resource : mResources)
        {
            if (resource->getType() == ResourceType::Buffer)
            {
                mArena.destroy(static_cast<BufferResource*>(resource));
            }
            else if (resource->getType() == ResourceType::Texture)
            {
                mArena.destroy(static_cast<TextureResource*>(resource));
            }
            else if (resource->getType() == ResourceType::SubTexture)
            {
                mArena.destroy(static_cast<SubTextureResource*>(resource));
            }
        }
        mResources.clear();

        for (auto resourceNode : mResourceNodes)
        {
            mArena.destroy(resourceNode);
        }
        mResourceNodes.clear();

        for (auto passNode : mPassNodes)
        {
            // getTypeSize() 是纯虚函数, 必须在 std::destroy_at 之前取值——析构链跑完后
            // 对象的 vtable 指针会停在 PassBase 这一层, 再调用 getTypeSize() 会落到纯虚桩上。
            PassBase* pass = passNode->getPass();
            size_t typeSize = pass->getTypeSize();
            std::destroy_at(pass);
            mArena.free(pass, typeSize);
            mArena.destroy(passNode);
        }
        mPassNodes.clear();
    }

    void RenderGraph::setupGraphicsPassAttachments(PassNode* passNode)
    {
        GraphicsPass* const pass = static_cast<GraphicsPass*>(passNode->getPass());
        uint32_t attachmentCount = pass->mDepthStencilAttachment.isValid() ?
            pass->mColorAttachmentCount + 1 : pass->mColorAttachmentCount;
        uint8_t samples = 0;
        pass->mDepthStencilAttachmentInfo.stencilStoreOp = rhi::AttachmentStoreOp::Dont_Care;

        for (uint32_t i = 0; i < attachmentCount; ++i)
        {
            bool isColorAttachment = i < pass->mColorAttachmentCount;
            TextureHandle textureHandle = isColorAttachment ? pass->mColorAttachments[i] : pass->mDepthStencilAttachment;

            rhi::TextureView* textureView = nullptr;
            VirtualResource* resource = getResource(textureHandle);
            TextureResource* textureResource = nullptr;
            SubTextureResource* subTextureResource = nullptr;
            if (resource->getType() == ResourceType::Texture)
            {
                textureResource = static_cast<TextureResource*>(resource);
                textureView = textureResource->getPhysicalResource();
            }
            else if (resource->getType() == ResourceType::SubTexture)
            {
                subTextureResource = static_cast<SubTextureResource*>(resource);
                textureView = subTextureResource->getPhysicalResource();
                textureResource = subTextureResource->getTextureResource();
            }
            assert(textureResource);
            const auto& textureDesc = textureResource->getDescription();
            auto accessRecords = textureResource->getAccessRecords();
            if (!samples)
                samples = textureDesc.samples;

            // 找到attachment在其资源访问记录中的迭代器
            uint32_t accessRecordIndex = isColorAttachment ? pass->mColorAccessRecordIndices[i] : pass->mDepthStencilAccessRecordIndex;
            assert(accessRecordIndex != UINT32_MAX);
            assert(accessRecordIndex < accessRecords.size());
            const auto recordIt = accessRecords.begin() + accessRecordIndex;
            assert(recordIt != accessRecords.end());
            auto& record = *recordIt;

            // 推导storeOp。只读的depth/stencil aspect使用None，在不产生store访问的同时保留原内容。
            // 对于发生写入的aspect：导入纹理始终Store；普通纹理如果后续没有其它pass访问其
            // SubresourceRange则为Dont Care，否则为Store。
            rhi::AttachmentStoreOp storeOp = rhi::AttachmentStoreOp::Dont_Care;
            if (textureResource->isImported())
            {
                storeOp = rhi::AttachmentStoreOp::Store;
                if (!isColorAttachment)
                    pass->mDepthStencilAttachmentInfo.stencilStoreOp = rhi::AttachmentStoreOp::Store;
            }
            else
            {
                for (auto it = std::next(recordIt); it != accessRecords.end(); ++it)
                {
                    auto& postRecord = *it;
                    if (postRecord.passNode == passNode)
                        continue;

                    if (checkSubresourceRangeHasOverlap(postRecord.range, record.range))
                    {
                        storeOp = rhi::AttachmentStoreOp::Store;

                        // color attachment可以提前跳出循环
                        if (isColorAttachment)
                            break;

                        // 非color attachment需要检测到有stencil访问才能提前跳出循环
                        if (enumHasAnyFlags(postRecord.range.aspect, rhi::TextureAspect::Stencil) &&
                            enumHasAnyFlags(record.range.aspect, rhi::TextureAspect::Stencil))
                        {
                            pass->mDepthStencilAttachmentInfo.stencilStoreOp = rhi::AttachmentStoreOp::Store;
                            break;
                        }
                    }
                }
            }

            if (isColorAttachment)
            {
                pass->mColorAttachmentInfos[i].textureView = textureView;
                pass->mColorAttachmentInfos[i].storeOp = storeOp;
            }
            else
            {
                pass->mDepthStencilAttachmentInfo.textureView = textureView;
                pass->mDepthStencilAttachmentInfo.state = inferTextureState(record.usage, record.accessType, record.range.aspect, PassType::Graphics);

                const bool writesDepth = enumHasAnyFlags(record.accessType, AccessType::Write_Only);
                const bool writesStencil = enumHasAnyFlags(record.accessType, AccessType::Stencil_Write_Only);
                pass->mDepthStencilAttachmentInfo.depthStoreOp = writesDepth ? storeOp : rhi::AttachmentStoreOp::None;
                if (!writesStencil)
                    pass->mDepthStencilAttachmentInfo.stencilStoreOp = rhi::AttachmentStoreOp::None;
            }

            // 如果 renderArea 无效, 则主动设置 renderArea
            if (i == 0 &&
                (pass->mRenderArea.width == 0 || pass->mRenderArea.height == 0))
            {
                pass->mRenderArea.width = textureDesc.extent.x;
                pass->mRenderArea.height = textureDesc.extent.y;
            }
        }
    }

    void RenderGraph::inferBarriers(PassNode* passNode)
    {
        uint16_t preBufferBarrierBegin = mTransientResources.getPrePassBufferBarrierCount();
        uint16_t preBufferBarrierCount = 0;
        uint16_t preTextureBarrierBegin = mTransientResources.getPrePassTextureBarrierCount();
        uint16_t preTextureBarrierCount = 0;
        uint16_t postBufferBarrierBegin = mTransientResources.getPostPassBufferBarrierCount();
        uint16_t postBufferBarrierCount = 0;
        uint16_t postTextureBarrierBegin = mTransientResources.getPostPassTextureBarrierCount();
        uint16_t postTextureBarrierCount = 0;

        PassBase* pass = passNode->getPass();
        // 遍历资源, 推导对应的 barriers
        for (const auto& dependencyRecord : pass->getDependencyRecords())
        {
            VirtualResource* resource = getResource(dependencyRecord.resource);
            if (resource->getType() == ResourceType::Buffer)
            {
                // Buffer Resource
                BufferResource* bufferResource = static_cast<BufferResource*>(resource);
                auto accessRecords = bufferResource->getAccessRecords();

                auto recordIt = accessRecords.rend() - dependencyRecord.accessRecordIndex - 1;
                assert(recordIt != accessRecords.rend());
                auto& record = *recordIt;

                auto prevRecordIt = accessRecords.rend();
                for (auto it = std::next(recordIt); it != accessRecords.rend(); ++it)
                {
                    if (it->passNode != passNode)
                    {
                        prevRecordIt = it;
                        break;
                    }
                }
                if (prevRecordIt != accessRecords.rend())
                {
                    auto& prevRecord = *prevRecordIt;
                    // RAW/WAR/WAW: 只要本次访问包含写, 就必须插入 barrier 与上一次访问同步
                    if (enumHasAnyFlags(record.accessType, AccessType::Write_Only))
                    {
                        // 如果本次是写访问, 则必须添加barrier
                        rhi::BufferBarrier barrier;
                        barrier.buffer = bufferResource->getPhysicalResource();
                        barrier.oldState = inferBufferState(
                            prevRecord.usage,
                            prevRecord.accessType,
                            prevRecord.passNode->getPass()->getType()
                        );
                        barrier.newState = inferBufferState(
                            record.usage,
                            record.accessType,
                            pass->getType()
                        );
                        barrier.offset = 0;
                        barrier.size = rhi::kWholeBuffer;
                        mTransientResources.addPrePassBufferBarrier(barrier);
                        ++preBufferBarrierCount;
                    }
                    else
                    {
                        // RAR: 需要向前追溯, 判断当前 read 的每个 usage bit 是否已经被之前的 read 覆盖,
                        // 只有当某个 bit 从最近一次 write 之后没有被同 stage/access 的 read 覆盖过时, 才需要 barrier.
                        // 例如: Storage Read (compute) -> Indirect Read (graphics) -> Storage Read (compute)
                        // 第一个 Storage Read 不能覆盖 Indirect Read, 所以 Render 前需要 barrier;
                        // 但第二个 Storage Read 可以被第一个覆盖, 因此不需要额外 barrier.
                        auto writeIt = accessRecords.rend();
                        for (auto it = std::next(recordIt); it != accessRecords.rend(); ++it)
                        {
                            if (it->passNode == passNode)
                                continue;
                            if (enumHasAnyFlags(it->accessType, AccessType::Write_Only))
                            {
                                writeIt = it;
                                break;
                            }
                        }

                        static constexpr std::array<rhi::BufferUsage, 6> kRARUsageBits = {
                            rhi::BufferUsage::Transfer_Src,
                            rhi::BufferUsage::Uniform,
                            rhi::BufferUsage::Storage,
                            rhi::BufferUsage::Index,
                            rhi::BufferUsage::Vertex,
                            rhi::BufferUsage::Indirect
                        };

                        // 找到最近一次写后, 检查当前 read 的每个 usage bit 是否已被覆盖
                        if (writeIt != accessRecords.rend())
                        {
                            // BufferUsage 当前有 6 个 bit 需要判断是否有; 这里遍历每一个 bit, 判断是否需要 barrier
                            // NOTE: 如果 BufferUsage 增加新 bit, 这里需要同步更新
                            std::array<rhi::ResourceState, 6> coveredStatePerBit{};
                            std::array<bool, 6> coveredUsageBits{};

                            for (auto it = std::next(recordIt); it != writeIt; ++it)
                            {
                                if (it->passNode == passNode)
                                    continue;

                                PassType passType = it->passNode->getPass()->getType();
                                for (uint32_t bit = 0; bit < 6; ++bit)
                                {
                                    rhi::BufferUsage usageBit = kRARUsageBits[bit];
                                    if (!enumHasAnyFlags(it->usage, usageBit)) continue;

                                    coveredUsageBits[bit] = true;
                                    coveredStatePerBit[bit] |= inferSingleBufferUsageState(usageBit, it->accessType, passType);
                                }
                            }

                            rhi::BufferUsage uncoveredUsage = rhi::BufferUsage::None;
                            rhi::ResourceState newState = rhi::ResourceState::None;
                            for (uint32_t bit = 0; bit < 6; ++bit)
                            {
                                rhi::BufferUsage usageBit = kRARUsageBits[bit];
                                if (!enumHasAnyFlags(record.usage, usageBit))
                                    continue;

                                rhi::ResourceState currState = inferSingleBufferUsageState(usageBit, record.accessType, pass->getType());

                                bool covered = coveredUsageBits[bit] && (enumHasAllFlags(coveredStatePerBit[bit], currState));
                                if (!covered)
                                {
                                    uncoveredUsage |= usageBit;
                                    newState |= currState;
                                }
                            }

                            if (uncoveredUsage != rhi::BufferUsage::None)
                            {
                                rhi::BufferBarrier barrier;
                                barrier.buffer = bufferResource->getPhysicalResource();
                                barrier.oldState = inferBufferState(
                                    writeIt->usage,
                                    writeIt->accessType,
                                    writeIt->passNode->getPass()->getType()
                                );
                                barrier.newState = newState;
                                barrier.offset = 0;
                                barrier.size = rhi::kWholeBuffer;
                                mTransientResources.addPrePassBufferBarrier(barrier);
                                ++preBufferBarrierCount;
                            }
                        }
                        // 如果资源历史上没有写访问, 则不需要 barrier
                    }
                }
                else
                {
                    // 本次访问是第一次访问
                    assert(passNode == bufferResource->getFirstPass());
                }
            }
            else
            {
                // Texture Resource
                TextureResource* textureResource = nullptr;
                rhi::TextureView* textureView = nullptr;
                if (resource->getType() == ResourceType::Texture)
                {
                    textureResource = static_cast<TextureResource*>(resource);
                    textureView = textureResource->getPhysicalResource();
                }
                else if (resource->getType() == ResourceType::SubTexture)
                {
                    SubTextureResource* subTextureResource = static_cast<SubTextureResource*>(resource);
                    textureResource = subTextureResource->getTextureResource();
                    textureView = subTextureResource->getPhysicalResource();
                }
                assert(textureResource && textureView);
                auto accessRecords = textureResource->getAccessRecords();

                // 找到当前pass的访问记录
                auto recordIt = accessRecords.rend() - dependencyRecord.accessRecordIndex - 1;
                assert(recordIt != accessRecords.rend());
                auto& record = *recordIt;

                rhi::TextureBarrier barrier;
                const auto& range = textureView->getDesc().range;
                barrier.texture = &textureView->getTexture();
                barrier.newState = inferTextureState(record.usage, record.accessType, range.aspect, pass->getType());

                const bool readOnlyAccess = !enumHasAnyFlags(record.accessType, AccessType::Write_Only | AccessType::Stencil_Write_Only);

                // Image 的 barrier 推导比 Buffer 复杂，因为一个 Texture 资源可能按 subresource range 被多次访问。
                // 例如：同一个 2D 纹理的 mip0 被 Pass A 写过，mip1 被 Pass B 读过，
                // 当前 Pass C 同时访问 mip0 和 mip1 时，需要为 mip0 插一个 barrier，为 mip1 插另一个 barrier（或合并）。
                //
                // 算法思路（逐步覆盖法）：
                //   1. 设当前 Pass 要访问的范围为 PassRange。
                //   2. 从当前 record 向前遍历历史访问记录。
                //   3. 对每个历史记录，先计算它与 PassRange 的交集 intersection。
                //   4. 再计算 intersection - 已覆盖范围(findRangeSet) = 还需要同步的差集 differences。
                //   5. 对 differences 中的每一块，生成一个 image barrier，并把它加入已覆盖范围。
                //   6. 当已覆盖范围等于 PassRange 时，提前结束。
                //
                // 这样做的好处：
                //   - 同一个历史记录只需要为它“新覆盖到”的那部分 subresource 生成 barrier，
                //     已经被其他历史记录覆盖过的部分不会重复生成。
                //   - 如果某个历史记录与 PassRange 完全不重叠，直接跳过。
                SubresourceRangeSet findedRangeSet(mArena);
                bool findAllRange = false;
                auto dontNeedBarrierForRecord = [&](const TextureAccessRecord& prev, const TextureAccessRecord& cur) -> bool
                    {
                        // 当前策略：只检查相邻的prev record, 因此比较严格但实现简单.
                        // 同布局 + 同 usage + 只读 + state 覆盖 → barrier 可省略,
                        // 否则插入 barrier.
                        // 注：这里不实现跨多个 record 的向前追溯, 因为 image 还要处理 subresource range 切片,
                        //     完整追溯会让“已覆盖”状态与 range 二维交叉，复杂度较高。
                        const bool readAfterRead = readOnlyAccess && !enumHasAnyFlags(prev.accessType, AccessType::Write_Only | AccessType::Stencil_Write_Only);
                        const bool stateMatch = barrier.oldState == barrier.newState;
                        const bool usageMatch = prev.usage == cur.usage;

                        rhi::ResourceState prevState = inferTextureState(prev.usage, prev.accessType, prev.range.aspect, prev.passNode->getPass()->getType());
                        rhi::ResourceState currState = inferTextureState(cur.usage, cur.accessType, cur.range.aspect, pass->getType());
                        const bool stateCovered = enumHasAllFlags(prevState, currState);

                        return readAfterRead && stateMatch && usageMatch && stateCovered;
                    };

                for (auto it = std::next(recordIt); it != accessRecords.rend(); ++it)
                {
                    auto& prevRecord = *it;

                    if (prevRecord.passNode == passNode)
                        continue;

                    // 如果相邻且前面的记录完全覆盖后面的记录, 则不需要进行复杂的subresource range切分
                    if (it == std::next(recordIt) && subresourceRangeCovers(prevRecord.range, record.range))
                    {
                        barrier.oldState = inferTextureState(
                            prevRecord.usage,
                            prevRecord.accessType,
                            prevRecord.range.aspect,
                            prevRecord.passNode->getPass()->getType()
                        );

                        if (!dontNeedBarrierForRecord(prevRecord, record))
                        {
                            barrier.range = record.range;
                            mTransientResources.addPrePassTextureBarrier(barrier);
                            ++preTextureBarrierCount;
                        }

                        findAllRange = true;
                        break;
                    }

                    auto intersection = intersectSubresourceRange(prevRecord.range, record.range);
                    if (!intersection)
                        continue;

                    barrier.oldState = inferTextureState(
                        prevRecord.usage,
                        prevRecord.accessType,
                        prevRecord.range.aspect,
                        prevRecord.passNode->getPass()->getType()
                    );

                    const bool dontNeedBarrier = dontNeedBarrierForRecord(prevRecord, record);

                    auto differences = subtractSubresourceRangeSet(*intersection, findedRangeSet);
                    for (auto& diff : differences)
                    {
                        findedRangeSet.insert(diff);

                        if (!dontNeedBarrier)
                        {
                            barrier.range = diff;
                            mTransientResources.addPrePassTextureBarrier(barrier);
                            ++preTextureBarrierCount;
                        }
                    }

                    auto ranges = findedRangeSet.getRanges();
                    if (ranges.size() == 1 && subresourceRangeEqual(ranges[0], record.range))
                    {
                        findAllRange = true;
                        break;
                    }
                }

                // 如果向前遍历完历史记录后，PassRange 仍有部分 subresource 从未被访问过，
                // 那么这部分不存在“上一次访问”，需要从初始布局转换过来：
                //   - 导入纹理：从 default layout 转换到目标 layout。
                //   - 非导入纹理（rendergraph 内部创建）：从 UNDEFINED 转换到目标 layout。
                // 这里无条件插入 barrier，是因为内部创建的纹理首次访问必须做 layout transition。
                if (!findAllRange)
                {

                    // 保守起见, 我们规定, 导入纹理必须在第一次访问前插入barrier,
                    // 即使它的访问布局和默认布局可能一致, 而最后一次访问后可以因为布局一致而不插入barrier
                    // const bool dontNeedBarrier = readOnlyAccess && textureResource->isImported() && textureResource->getDefaultState() == barrier.newState;
                    // if (!dontNeedBarrier)
                    {
                        auto unaccessedRanges = subtractSubresourceRangeSet(record.range, findedRangeSet);
                        for (auto& r : unaccessedRanges)
                        {
                            // 如果是导入纹理, 则从默认状态转换到目标状态
                            // 否则从 None 转换到目标状态
                            barrier.oldState = textureResource->isImported() ? textureResource->getDefaultState() : rhi::ResourceState::None;
                            barrier.range = r;

                            mTransientResources.addPrePassTextureBarrier(barrier);
                            ++preTextureBarrierCount;
                        }
                    }
                }

                // 对于导入纹理，如果本次访问是资源的最后一次访问，需要生成 post barrier
                // 把纹理从当前 layout 转回 default layout，以便下一帧/外部代码继续使用。
                // 同样使用逐步覆盖法，从后向前遍历历史记录，覆盖整个纹理范围。
                if (textureResource->isImported() && passNode == textureResource->getLastPass())
                {
                    barrier.newState = textureResource->getDefaultState();

                    findedRangeSet.clear();
                    auto& allRange = textureResource->getPhysicalResource()->getDesc().range;
                    for (auto it = recordIt; it != accessRecords.rend(); ++it)
                    {
                        auto& prevRecord = *it;

                        barrier.oldState = inferTextureState(
                            prevRecord.usage,
                            prevRecord.accessType,
                            prevRecord.range.aspect,
                            prevRecord.passNode->getPass()->getType()
                        );
                        const bool dontNeedBarrier = barrier.oldState == barrier.newState;

                        auto differences = subtractSubresourceRangeSet(prevRecord.range, findedRangeSet);
                        for (auto& diff : differences)
                        {
                            findedRangeSet.insert(diff);

                            if (!dontNeedBarrier)
                            {
                                barrier.range = diff;
                                mTransientResources.addPostPassTextureBarrier(barrier);
                                ++postTextureBarrierCount;
                            }
                        }

                        auto ranges = findedRangeSet.getRanges();
                        if (ranges.size() == 1 && subresourceRangeEqual(ranges[0], allRange))
                        {
                            break;
                        }
                    }

                    // 暂时假设导入纹理的每个subresource必定会在帧中使用
                    auto ranges = findedRangeSet.getRanges();
                    assert(ranges.size() == 1 && subresourceRangeEqual(ranges[0], allRange));
                }
            }
        }

        pass->setPreBufferBarrierRange(preBufferBarrierBegin, preBufferBarrierCount);
        pass->setPreTextureBarrierRange(preTextureBarrierBegin, preTextureBarrierCount);
        pass->setPostBufferBarrierRange(postBufferBarrierBegin, postBufferBarrierCount);
        pass->setPostTextureBarrierRange(postTextureBarrierBegin, postTextureBarrierCount);
    }

    bool RenderGraph::subresourceRangeEqual(const rhi::TextureRange& a, const rhi::TextureRange& b)
    {
        // 判断两个 subresource range 是否完全相同（aspect/mip/layer 都一致）
        return a.aspect == b.aspect &&
            a.baseMip == b.baseMip &&
            a.mipCount == b.mipCount &&
            a.baseLayer == b.baseLayer &&
            a.layerCount == b.layerCount;
    }

    bool RenderGraph::subresourceRangeCovers(const rhi::TextureRange& a, const rhi::TextureRange& b)
    {
        return (a.aspect & b.aspect) == b.aspect &&
            a.baseMip <= b.baseMip &&
            a.baseMip + a.mipCount >= b.baseMip + b.mipCount &&
            a.baseLayer <= b.baseLayer &&
            a.baseLayer + a.layerCount >= b.baseLayer + b.layerCount;
    }

    bool RenderGraph::checkSubresourceRangeHasOverlap(const rhi::TextureRange& a, const rhi::TextureRange& b)
    {
        // 快速判断两个 range 是否有任何重叠（aspect/mip/layer 任一维度都不相交则整体不相交）
        if (!(a.aspect & b.aspect)) return false;

        // 把半开区间 [base, base+count) 算出来，再判断是否相交
        const uint32_t aMipEnd = a.baseMip + a.mipCount;
        const uint32_t bMipEnd = b.baseMip + b.mipCount;
        const uint32_t aLayerEnd = a.baseLayer + a.layerCount;
        const uint32_t bLayerEnd = b.baseLayer + b.layerCount;

        // 半开区间相交条件：不是“一个完全在另一个左边/右边”
        const bool mipOverlap = !(aMipEnd <= b.baseMip || bMipEnd <= a.baseMip);
        const bool layerOverlap = !(aLayerEnd <= b.baseLayer || bLayerEnd <= a.baseLayer);
        return mipOverlap && layerOverlap;
    }

    std::optional<rhi::TextureRange> RenderGraph::intersectSubresourceRange(
        const rhi::TextureRange& a,
        const rhi::TextureRange& b
    )
    {
        // 计算两个 subresource range 的交集。
        // 如果 aspect/mip/layer 任一维度没有交集，返回 nullopt。
        // aspect 取交集；mip 和 layer 取两个半开区间的重叠部分。
        rhi::TextureRange r;
        r.aspect = a.aspect & b.aspect;
        const uint32_t mipBegin = std::max(a.baseMip, b.baseMip);
        const uint32_t mipEnd = std::min(a.baseMip + a.mipCount, b.baseMip + b.mipCount);
        const uint32_t layerBegin = std::max(a.baseLayer, b.baseLayer);
        const uint32_t layerEnd = std::min(a.baseLayer + a.layerCount, b.baseLayer + b.layerCount);
        r.baseMip = mipBegin;
        r.mipCount = (mipEnd > mipBegin) ? (mipEnd - mipBegin) : 0;
        r.baseLayer = layerBegin;
        r.layerCount = (layerEnd > layerBegin) ? (layerEnd - layerBegin) : 0;
        if (r.aspect == rhi::TextureAspect::None ||
            r.mipCount == 0 ||
            r.layerCount == 0)
            return std::nullopt;
        return r;
    }

    rhi::ResourceState RenderGraph::inferBufferState(rhi::BufferUsage usage, AccessType accessType, PassType passType)
    {
        rhi::ResourceState state = rhi::ResourceState::None;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Transfer_Src))
        {
            state |= rhi::ResourceState::Transfer_Src;
        }
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Transfer_Dst))
        {
            state |= rhi::ResourceState::Transfer_Dst;
        }
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Uniform))
        {
            state |= passType == PassType::Compute ?
                rhi::ResourceState::Compute_Shader_Uniform:
                rhi::ResourceState::Graphics_Shader_Uniform;
        }
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Storage))
        {
            if (accessType == AccessType::Read_Only)
            {
                state |= passType == PassType::Compute ?
                    rhi::ResourceState::Compute_Shader_Storage_Read :
                    rhi::ResourceState::Graphics_Shader_Storage_Read;
            }
            else if (accessType == AccessType::Write_Only)
            {
                state |= passType == PassType::Compute ?
                    rhi::ResourceState::Compute_Shader_Storage_Write :
                    rhi::ResourceState::Graphics_Shader_Storage_Write;
            }
            else
            {
                state |= passType == PassType::Compute ?
                    rhi::ResourceState::Compute_Shader_Storage :
                    rhi::ResourceState::Graphics_Shader_Storage;
            }
        }
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Index))
        {
            state |= rhi::ResourceState::Index_Attachment;
        }
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Vertex))
        {
            state |= rhi::ResourceState::Vertex_Attachment;
        }
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Indirect))
        {
            state |= rhi::ResourceState::Indirect_Argument;
        }
        return state;
    }

    rhi::ResourceState RenderGraph::inferTextureState(rhi::TextureUsage usage, AccessType accessType, rhi::TextureAspect aspect, PassType passType)
    {
        switch (usage)
        {
        case rhi::TextureUsage::Sampled:
        {
            return passType == PassType::Compute ?
                rhi::ResourceState::Compute_Shader_Sampled :
                rhi::ResourceState::Graphics_Shader_Sampled;
        }
        case rhi::TextureUsage::Storage:
        {
            if (passType == PassType::Compute)
            {
                if (accessType == AccessType::Read_Only)
                    return rhi::ResourceState::Compute_Shader_Storage_Read;
                if (accessType == AccessType::Write_Only)
                    return rhi::ResourceState::Compute_Shader_Storage_Write;
                return rhi::ResourceState::Compute_Shader_Storage;
            }
            else
            {
                if (accessType == AccessType::Read_Only)
                    return rhi::ResourceState::Graphics_Shader_Storage_Read;
                if (accessType == AccessType::Write_Only)
                    return rhi::ResourceState::Graphics_Shader_Storage_Write;
                return rhi::ResourceState::Graphics_Shader_Storage;
            }
        }
        case rhi::TextureUsage::Transfer_Src:
        {
            return rhi::ResourceState::Transfer_Src;
        }
        case rhi::TextureUsage::Transfer_Dst:
        {
            return rhi::ResourceState::Transfer_Dst;
        }
        case rhi::TextureUsage::Color_Attachment:
        {
            if (accessType == AccessType::Write_Only)
                return rhi::ResourceState::Color_Attachment_Write;
            return rhi::ResourceState::Color_Attachment;
        }
        case rhi::TextureUsage::Depth_Stencil_Attachment:
        {
            if (aspect == rhi::TextureAspect::Depth)
            {
                if (accessType == AccessType::Read_Only)
                    return rhi::ResourceState::Depth_Attachment_Read;
                return rhi::ResourceState::Depth_Attachment;
            }
            else if (aspect == rhi::TextureAspect::Stencil)
            {
                if (accessType == AccessType::Stencil_Read_Only)
                    return rhi::ResourceState::Stencil_Attachment_Read;
                return rhi::ResourceState::Stencil_Attachment;
            }
            else
            {
                if (!enumHasAllFlags(accessType, AccessType::Write_Only) && !enumHasAllFlags(accessType, AccessType::Stencil_Write_Only))
                    return rhi::ResourceState::Depth_Stencil_Attachment_Read;
                if (enumHasAllFlags(accessType, AccessType::Write_Only) && !enumHasAllFlags(accessType, AccessType::Stencil_Write_Only))
                    return rhi::ResourceState::Depth_Stencil_Read_Attachment;
                if (!enumHasAllFlags(accessType, AccessType::Write_Only) && enumHasAllFlags(accessType, AccessType::Stencil_Write_Only))
                    return rhi::ResourceState::Depth_Read_Stencil_Attachment;
                return rhi::ResourceState::Depth_Stencil_Attachment;
            }
        }
        default:
            return rhi::ResourceState::None;
        }
    }

    rhi::ResourceState RenderGraph::inferSingleBufferUsageState(rhi::BufferUsage usageBit, AccessType accessType, PassType passType)
    {
        // 只处理单个 BufferUsage bit, 直接返回对应 state.
        switch (usageBit)
        {
        case rhi::BufferUsage::Transfer_Src:
            return rhi::ResourceState::Transfer_Src;
        case rhi::BufferUsage::Transfer_Dst:
            return rhi::ResourceState::Transfer_Dst;
        case rhi::BufferUsage::Uniform:
            return passType == PassType::Compute ?
                rhi::ResourceState::Compute_Shader_Uniform :
                rhi::ResourceState::Graphics_Shader_Uniform;
        case rhi::BufferUsage::Storage:
        {
            if (passType == PassType::Compute)
            {
                if (accessType == AccessType::Read_Only)
                    return rhi::ResourceState::Compute_Shader_Storage_Read;
                else if (accessType == AccessType::Write_Only)
                    return rhi::ResourceState::Compute_Shader_Storage_Write;
                else
                    return rhi::ResourceState::Compute_Shader_Storage;
            }
            else
            {
                if (accessType == AccessType::Read_Only)
                    return rhi::ResourceState::Graphics_Shader_Storage_Read;
                else if (accessType == AccessType::Write_Only)
                    return rhi::ResourceState::Graphics_Shader_Storage_Write;
                else
                    return rhi::ResourceState::Graphics_Shader_Storage;
            }
        }
        case rhi::BufferUsage::Vertex:
            return rhi::ResourceState::Vertex_Attachment;
        case rhi::BufferUsage::Index:
            return rhi::ResourceState::Index_Attachment;
        case rhi::BufferUsage::Indirect:
            return rhi::ResourceState::Indirect_Argument;
        default:
            return rhi::ResourceState::None;
        }
    }

    void RenderGraph::SubresourceRangeSet::insert(const rhi::TextureRange& range)
    {
        // SubresourceRangeSet 维护一组互不重叠（或已合并）的 subresource range。
        // 插入新 range 时，尝试与集合中已有的 range 合并，以减少碎片。
        // 合并策略见 tryMergeSubresourceRanges 注释。
        rhi::TextureRange current = range;

        bool mergedSomething;
        do
        {
            mergedSomething = false;
            for (auto it = mRanges.begin(); it != mRanges.end();)
            {
                auto merged = tryMergeSubresourceRanges(current, *it);
                if (merged)
                {
                    current = *merged;
                    it = mRanges.erase(it);
                    mergedSomething = true;
                }
                else
                {
                    ++it;
                }
            }
        } while (mergedSomething);

        mRanges.emplace_back(current);
    }

    std::optional<rhi::TextureRange> RenderGraph::SubresourceRangeSet::tryMergeSubresourceRanges(
        const rhi::TextureRange& a,
        const rhi::TextureRange& b
    )
    {
        // 合并两个 range 的条件：
        //   1. 在 mip 和 layer 两个维度中，有且只有一个维度完全相同，另一个维度相邻或有重叠；
        //   2. aspectMask 可以取并集（即至少有一个共同的 aspect，或互补）。
        //
        // 直观理解：只有当两个 range 能拼成一个更大的“矩形”时才合并。
        // 例如：
        //   - mip [0,1) x layer [0,2) 和 mip [0,1) x layer [2,4) 可以合并成 mip [0,1) x layer [0,4)
        //   - mip [0,1) x layer [0,2) 和 mip [1,2) x layer [0,2) 可以合并成 mip [0,2) x layer [0,2)
        //   - mip [0,1) x layer [0,2) 和 mip [2,3) x layer [0,2) 中间隔了一层，不能合并
        //   - mip [0,2) x layer [0,2) 和 mip [1,3) x layer [0,2) 在 mip 上重叠，可以合并成 mip [0,3) x layer [0,2)
        
        // 1. 计算半开区间端点
        uint32_t aMipBeg = a.baseMip;
        uint32_t aMipEnd = a.baseMip + a.mipCount;
        uint32_t bMipBeg = b.baseMip;
        uint32_t bMipEnd = b.baseMip + b.mipCount;

        uint32_t aLayBeg = a.baseLayer;
        uint32_t aLayEnd = a.baseLayer + a.layerCount;
        uint32_t bLayBeg = b.baseLayer;
        uint32_t bLayEnd = b.baseLayer + b.layerCount;

        // 2. 判断是否满足“有且只有一个维度对齐，另一个维度相邻或重叠”
        bool mipEqual = (aMipBeg == bMipBeg && aMipEnd == bMipEnd);
        bool layerEqual = (aLayBeg == bLayBeg && aLayEnd == bLayEnd);

        bool mipAdjOrOverlap = (aMipEnd >= bMipBeg && bMipEnd >= aMipBeg);
        bool layerAdjOrOverlap = (aLayEnd >= bLayBeg && bLayEnd >= aLayBeg);

        if (!((mipEqual && layerAdjOrOverlap) ||
            (layerEqual && mipAdjOrOverlap))) {
            return std::nullopt;
        }

        // 3. 构造合并结果：取两个 range 在 mip/layer 上的并集，aspect 取并集
        rhi::TextureRange merged{};
        merged.aspect = a.aspect | b.aspect;
        merged.baseMip = std::min(aMipBeg, bMipBeg);
        merged.mipCount = std::max(aMipEnd, bMipEnd) - merged.baseMip;
        merged.baseLayer = std::min(aLayBeg, bLayBeg);
        merged.layerCount = std::max(aLayEnd, bLayEnd) - merged.baseLayer;

        return merged;
    }

    uint8_t RenderGraph::subtractSubresourceRange(
        const rhi::TextureRange& range,
        const rhi::TextureRange& cover,
        std::array<rhi::TextureRange, 5>& result
    )
    {
        // 计算 range - cover 的差集，最多产生 5 个子块。
        //
        // 在 2D（mip x layer）平面上，cover 与 range 的交集形成一个“洞”，
        // range 去掉这个洞后，剩余部分最多可分解为：
        //   1. 上方（mip 更小）
        //   2. 下方（mip 更大）
        //   3. 左侧（layer 更小，且在 mip 洞范围内）
        //   4. 右侧（layer 更大，且在 mip 洞范围内）
        // 如果 aspect 不完全重叠，还会多出一个 aspect 差集块。
        //
        // 注意：调用方保证 range 和 cover 的 aspect 有交集。

        uint8_t resultCount = 0;

        rhi::TextureAspect overlapedAspectMask = range.aspect & cover.aspect;

        // 外部已经检查过该条件
        assert(overlapedAspectMask != rhi::TextureAspect::None);

        // 如果 aspect 不完全重合，先把“只在 range 里但不在 cover 里”的 aspect 部分作为一个整体返回。
        // 这块在 mip/layer 维度上保持 range 的完整范围。
        if (overlapedAspectMask != range.aspect)
        {
            result[resultCount++] = {
                range.aspect & ~(overlapedAspectMask),
                range.baseMip,
                range.mipCount,
                range.baseLayer,
                range.layerCount
            };
        }

        uint32_t rMipEnd = range.baseMip + range.mipCount;
        uint32_t rLayerEnd = range.baseLayer + range.layerCount;
        uint32_t cMipEnd = cover.baseMip + cover.mipCount;
        uint32_t cLayerEnd = cover.baseLayer + cover.layerCount;

        bool overlap = !(rMipEnd <= cover.baseMip ||
                         cMipEnd <= range.baseMip ||
                         rLayerEnd <= cover.baseLayer ||
                         cLayerEnd <= range.baseLayer);

        // 如果 mip/layer 没有实际重叠，aspect 重叠的那部分范围直接作为一个整体返回。
        if (!overlap)
        {
            result[resultCount++] = {
                overlapedAspectMask,
                range.baseMip,
                range.mipCount,
                range.baseLayer,
                range.layerCount
            };
            return resultCount;
        }

        // 计算 mip/layer 上的交集（即“洞”）
        uint32_t mipMin = std::max(range.baseMip, cover.baseMip);
        uint32_t mipMax = std::min(rMipEnd, cMipEnd);
        uint32_t layerMin = std::max(range.baseLayer, cover.baseLayer);
        uint32_t layerMax = std::min(rLayerEnd, cLayerEnd);

        // 上方：mip 在洞的上方，layer 保持 range 全宽
        if (range.baseMip < mipMin)
        {
            result[resultCount++] = {
                overlapedAspectMask,
                range.baseMip,
                mipMin - range.baseMip,
                range.baseLayer,
                range.layerCount
            };
        }
        // 下方：mip 在洞的下方，layer 保持 range 全宽
        if (mipMax < rMipEnd)
        {
            result[resultCount++] = {
                overlapedAspectMask,
                mipMax,
                rMipEnd - mipMax,
                range.baseLayer,
                range.layerCount
            };
        }
        // 左侧：mip 在洞的范围内，layer 在洞的左侧
        if (range.baseLayer < layerMin)
        {
            result[resultCount++] = {
                overlapedAspectMask,
                mipMin,
                mipMax - mipMin,
                range.baseLayer,
                layerMin - range.baseLayer
            };
        }
        // 右侧：mip 在洞的范围内，layer 在洞的右侧
        if (layerMax < rLayerEnd)
        {
            result[resultCount++] = {
                overlapedAspectMask,
                mipMin,
                mipMax - mipMin,
                layerMax,
                rLayerEnd - layerMax
            };
        }

        return resultCount;
    }

    ArenaVector<rhi::TextureRange> RenderGraph::subtractSubresourceRangeSet(const rhi::TextureRange& range, const SubresourceRangeSet& set)
    {
        // 计算 range 被 set 中所有 range 依次“挖掉”后剩下的部分。
        //
        // 算法：
        //   1. work = { range }
        //   2. 对 set 中的每个 cover：
        //        next = {}
        //        对 work 中的每个 r：
        //          如果 r 和 cover 没有 aspect 交集，直接把 r 放入 next。
        //          否则用 subtractSubresourceRange(r, cover) 把 r 切成若干小块，全部放入 next。
        //        work = next
        //   3. 返回 work。
        //
        // 这样保证结果中每一块都不与 set 中的任何 range 重叠。

        ArenaVector<rhi::TextureRange> work = ArenaVector<rhi::TextureRange>(StlAllocator<rhi::TextureRange>(mArena));

        if (set.getRanges().empty())
        {
            work.emplace_back(range);
            return work;
        }

        ArenaVector<rhi::TextureRange> next = ArenaVector<rhi::TextureRange>(StlAllocator<rhi::TextureRange>(mArena));

        work.reserve(32);
        next.reserve(32);

        work.emplace_back(range);

        for (const auto& cover : set.getRanges())
        {
            next.clear();

            for (auto& r : work)
            {
                // 无交集, 直接加入结果
                if ((r.aspect & cover.aspect) == rhi::TextureAspect::None)
                {
                    next.emplace_back(r);
                    continue;
                }

                // 存在交集, 计算差集, 加入结果
                std::array<rhi::TextureRange, 5> parts;
                uint8_t partCount = subtractSubresourceRange(r, cover, parts);
                next.insert(next.end(), parts.begin(), parts.begin() + partCount);
            }

            work.swap(next);
        }

        return work;
    }

}
