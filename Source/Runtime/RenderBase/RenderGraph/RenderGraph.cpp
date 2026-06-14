#include "RenderGraph.h"

#include <algorithm>
#include <sstream>
#include <memory>

namespace hyp::rg
{
    BufferHandle RenderGraph::Builder::createBuffer(HString name, const BufferDescription& desc)
    {
        return mRenderGraph.createResource<BufferResource>(name, desc, mRenderGraph.getArena());
    }

    BufferHandle RenderGraph::Builder::importBuffer(HString name, BufferResource::PhysicalResource* physicalResource)
    {
        return mRenderGraph.importBuffer(name, physicalResource);
    }

    BufferHandle RenderGraph::Builder::accessBuffer(BufferHandle handle, BufferUsage usage, AccessType accessType)
    {
        return mRenderGraph.access<BufferResource>(mPassNode, handle, usage, accessType);
    }

    TextureHandle RenderGraph::Builder::createTexture(HString name, const TextureDescription& desc)
    {
        return mRenderGraph.createResource<TextureResource>(name, desc, mRenderGraph.getArena());
    }

    TextureHandle RenderGraph::Builder::importTexture(HString name, TextureResource::PhysicalResource* physicalResource, VkImageLayout defaultLayout)
    {
        return mRenderGraph.importTexture(name, physicalResource, defaultLayout);
    }

    TextureHandle RenderGraph::Builder::createSubTexture(HString name, TextureHandle handle, const SubTextureDescription& desc)
    {
        return mRenderGraph.createSubTexture(name, handle, desc);
    }

    TextureHandle RenderGraph::Builder::syncTextures(TextureHandle source, TextureHandle target)
    {
        auto checkSyncResources = [=]() -> bool
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

    TextureHandle RenderGraph::Builder::addColorAttachment(TextureHandle handle, VkAttachmentLoadOp loadOp, ClearColorValue clearColorValue)
    {
        AccessType accessType = AccessType::Write_Only;
        if (loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
            accessType |= AccessType::Read_Only;

        PassBase* pass = mPassNode->getPass();
        assert(pass->getType() == PassType::Graphics);
        static_cast<GraphicsPass*>(pass)->addColorAttachment(handle, loadOp, clearColorValue);

        return mRenderGraph.access<TextureResource>(mPassNode, handle, TextureUsage::Color_Attachment, accessType);
    }

    TextureHandle RenderGraph::Builder::addDepthAttachment(TextureHandle handle, VkAttachmentLoadOp loadOp, bool write, ClearDepthValue clearDepthValue)
    {
        AccessType accessType = AccessType::None;
        if (loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
            accessType |= AccessType::Read_Only;
        if (write)
            accessType |= AccessType::Write_Only;

        PassBase* pass = mPassNode->getPass();
        assert(pass->getType() == PassType::Graphics);
        static_cast<GraphicsPass*>(pass)->addDepthStencilAttachment(handle, loadOp, VK_ATTACHMENT_LOAD_OP_DONT_CARE, { clearDepthValue, 0u });

        return mRenderGraph.access<TextureResource>(mPassNode, handle, TextureUsage::Depth_Attachment, accessType);
    }

    TextureHandle RenderGraph::Builder::addStencilAttachment(TextureHandle handle, VkAttachmentLoadOp loadOp, bool write, uint8_t clearStencil)
    {
        AccessType accessType = AccessType::None;
        if (loadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
            accessType |= AccessType::Stencil_Read_Only;
        if (write)
            accessType |= AccessType::Stencil_Write_Only;

        PassBase* pass = mPassNode->getPass();
        assert(pass->getType() == PassType::Graphics);
        static_cast<GraphicsPass*>(pass)->addDepthStencilAttachment(handle, VK_ATTACHMENT_LOAD_OP_DONT_CARE, loadOp, { ClearDepthValue::Zero, clearStencil });

        return mRenderGraph.access<TextureResource>(mPassNode, handle, TextureUsage::Stencil_Attachment, accessType);
    }

    TextureHandle RenderGraph::Builder::addDepthStencilAttachment(
        TextureHandle handle,
        VkAttachmentLoadOp depthLoadOp,
        bool writeDepth,
        VkAttachmentLoadOp stencilLoadOp,
        bool writeStencil,
        ClearDepthStencilValue clearDepthStencilValue
    )
    {
        AccessType accessType = AccessType::None;
        if (depthLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
            accessType |= AccessType::Read_Only;
        if (stencilLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
            accessType |= AccessType::Stencil_Read_Only;
        if (writeDepth)
            accessType |= AccessType::Write_Only;
        if (writeStencil)
            accessType |= AccessType::Stencil_Write_Only;

        PassBase* pass = mPassNode->getPass();
        assert(pass->getType() == PassType::Graphics);
        static_cast<GraphicsPass*>(pass)->addDepthStencilAttachment(handle, depthLoadOp, stencilLoadOp, clearDepthStencilValue);

        return mRenderGraph.access<TextureResource>(mPassNode, handle, TextureUsage::Depth_Stencil_Attachment, accessType);
    }

    TextureHandle RenderGraph::Builder::sample(TextureHandle handle)
    {
        return mRenderGraph.access<TextureResource>(mPassNode, handle, TextureUsage::Sampled, AccessType::Read_Only);
    }

    TextureHandle RenderGraph::Builder::storage(TextureHandle handle, AccessType accessType)
    {
        return mRenderGraph.access<TextureResource>(mPassNode, handle, TextureUsage::Storage, accessType);
    }

    TextureHandle RenderGraph::Builder::transfer(TextureHandle handle, AccessType accessType)
    {
        return mRenderGraph.access<TextureResource>(mPassNode, handle, TextureUsage::Transfer, accessType);
    }

    void RenderGraph::Builder::setRenderArea(VkRect2D renderArea)
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

    RenderGraph::RenderGraph(TransientResources& transientResources, vk::Swapchain& swapchain) noexcept :
        mTransientResources(transientResources),
        mSwapchain(swapchain),
        mArena(256 * 1024),
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

            for (const auto& dependencyRecord : pass->getDependencyRecords())
            {
                VirtualResource* resource = getResource(dependencyRecord.resource);
                resource->registerPass(passNode);

                if (resource->getType() == ResourceType::Buffer)
                {
                    BufferResource* bufferResource = static_cast<BufferResource*>(resource);
                    bufferResource->addAccessRecord(
                        passNode,
                        static_cast<BufferUsage>(dependencyRecord.usage),
                        dependencyRecord.accessType
                    );
                }
                else if (resource->getType() == ResourceType::Texture)
                {
                    TextureResource* textureResource = static_cast<TextureResource*>(resource);
                    const TextureDescription& textureDesc = textureResource->getDescription();

                    textureResource->addAccessRecord(
                        passNode,
                        static_cast<TextureUsage>(dependencyRecord.usage),
                        dependencyRecord.accessType,
                        VkImageSubresourceRange{
                            vk::Image::getAspectMask(textureDesc.format),
                            0, textureDesc.mipLevels,
                            0, textureDesc.arrayLayers
                        }
                    );
                }
                else if (resource->getType() == ResourceType::SubTexture)
                {
                    SubTextureResource* subTextureResource = static_cast<SubTextureResource*>(resource);
                    const SubTextureDescription& subTextureDesc = subTextureResource->getDescription();

                    subTextureResource->getTextureResource()->addAccessRecord(
                        passNode,
                        static_cast<TextureUsage>(dependencyRecord.usage),
                        dependencyRecord.accessType,
                        VkImageSubresourceRange{
                            subTextureDesc.aspectMask,
                            subTextureDesc.baseMipLevel, subTextureDesc.levelCount,
                            subTextureDesc.baseArrayLayer, subTextureDesc.layerCount
                        }
                    );

                    subTextureResource->getTextureResource()->registerPass(passNode);
                }
            }
        }

        // 虚拟资源实例化
        for (VirtualResource* resource : mResources)
        {
            bool registered = resource->getType() == ResourceType::SubTexture ?
                static_cast<SubTextureResource*>(resource)->getTextureResource()->isRegistered() : resource->isRegistered();
            if (!resource->isDevirtualized() && registered)
            {
                resource->devirtualize(mTransientResources);
            }
        }

        // Pass实例化
        for (uint32_t i = 0; i < activePassNodeCount; ++i)
        {
            PassBase* pass = mPassNodes[i]->getPass();
            if (pass->getType() == PassType::Graphics)
            {
                inferRenderPassKey(mPassNodes[i]);
            }
            pass->devirtualize(mTransientResources);
        }

        // 推导Barriers
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

    TextureHandle RenderGraph::importTexture(HString name, TextureResource::PhysicalResource* physicalResource, VkImageLayout defaultLayout)
    {
        return importResource<TextureResource>(name, physicalResource, defaultLayout, getArena());
    }

    vk::Buffer* RenderGraph::getBuffer(BufferHandle handle) const noexcept
    {
        return static_cast<BufferResource*>(getResource(handle))->getPhysicalResource();
    }

    vk::ImageView* RenderGraph::getTexture(TextureHandle handle) const noexcept
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

    TextureHandle RenderGraph::createSubTexture(HString name, TextureHandle texture, const SubTextureDescription& desc)
    {
        TextureHandle handle(static_cast<uint16_t>(mResources.size()), texture.getNodeId(), texture.getVersion());
        assert(mResources.size() > texture.getResourceId());
        TextureResource* textureResource = static_cast<TextureResource*>(mResources[texture.getResourceId()]);
        // mResources.emplace_back(new SubTextureResource(name, textureResource, desc, tag));
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
            std::destroy_at(passNode->getPass());
            mArena.free(passNode->getPass(), passNode->getPass()->getTypeSize());
            mArena.destroy(passNode);
        }
        mPassNodes.clear();
    }

    void RenderGraph::inferRenderPassKey(PassNode* passNode)
    {
        GraphicsPass* const pass = static_cast<GraphicsPass*>(passNode->getPass());
        RenderPassKey& renderPassKey = pass->mRenderPassKey;
        uint32_t attachmentCount = pass->mDepthStencilAttachment.isValid() ?
            pass->mColorAttachmentCount + 1 : pass->mColorAttachmentCount;
        uint8_t samples = 0;
        renderPassKey.stencilStoreOp = AttachmentStoreOp::DontCare;
        renderPassKey.stencilLoadOp = pass->mStencilAttachmentLoadOp;

        for (uint32_t i = 0; i < attachmentCount; ++i)
        {
            bool isColorAttachment = i < pass->mColorAttachmentCount;
            TextureHandle textureHandle = isColorAttachment ? pass->mColorAttachments[i] : pass->mDepthStencilAttachment;
            AttachmentLoadOp loadOp = isColorAttachment ? pass->mColorAttachmentLoadOps[i] : pass->mDepthAttachmentLoadOp;
            AttachmentKey& attachmentKey = isColorAttachment ? renderPassKey.colorAttachments[i] : renderPassKey.depthStencilAttachment;

            vk::ImageView* imageView = getTexture(textureHandle);
            VirtualResource* resource = getResource(textureHandle);
            TextureResource* textureResource = nullptr;
            SubTextureResource* subTextureResource = nullptr;
            if (resource->getType() == ResourceType::Texture)
            {
                textureResource = static_cast<TextureResource*>(resource);
            }
            else if (resource->getType() == ResourceType::SubTexture)
            {
                subTextureResource = static_cast<SubTextureResource*>(resource);
                textureResource = subTextureResource->getTextureResource();
            }
            assert(textureResource);
            const auto& textureDesc = textureResource->getDescription();
            auto accessRecords = textureResource->getAccessRecords();
            if (!samples)
                samples = textureDesc.samples;

            attachmentKey.format = convertFromVkFormat(textureDesc.format);
            attachmentKey.loadOp = loadOp;

            // 找到attachment在其资源访问记录中的迭代器
            const auto recordIt = std::find_if(
                accessRecords.begin(),
                accessRecords.end(),
                [passNode, imageView](const TextureAccessRecord& record)
                {
                    return record.passNode == passNode && subresourceRangeEqual(record.range, imageView->getRange());
                }
            );
            assert(recordIt != accessRecords.end());
            auto& record = *recordIt;

            // 推导storeOp
            // 对于普通纹理, 如果后续没有其它pass对本attachment纹理的SubresourceRange内的访问操作, 则为DontCare, 否则为Store
            // 对于导入纹理, 应该始终Store
            if (textureResource->isImported())
            {
                attachmentKey.storeOp = AttachmentStoreOp::Store;
            }
            else
            {
                attachmentKey.storeOp = AttachmentStoreOp::DontCare;
                for (auto it = std::next(recordIt); it != accessRecords.end(); ++it)
                {
                    auto& prevRecord = *it;
                    if (prevRecord.passNode == passNode)
                        continue;

                    if (checkSubresourceRangeHasOverlap(prevRecord.range, record.range))
                    {
                        attachmentKey.storeOp = AttachmentStoreOp::Store;

                        // color attachment可以提前跳出循环
                        if (isColorAttachment)
                            break;

                        // 非color attachment需要检测到有stencil访问才能提前跳出循环
                        if (prevRecord.range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT &&
                            record.range.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
                        {
                            renderPassKey.stencilStoreOp = AttachmentStoreOp::Store;
                            break;
                        }
                    }
                }
            }

            // 推导layout
            // 布局转换由barriers执行, 因此初始布局, 结束布局与subpassLayout保持一致
            attachmentKey.layout = convertFromVkImageLayout(inferImageLayout(record.usage, record.accessType));
        }

        renderPassKey.samples = samples;

        pass->resolveFramebufferKey(*this);
    }

    void RenderGraph::inferBarriers(PassNode* passNode)
    {
        uint16_t prevBufferBarrierBegin = mTransientResources.getPassPrevBufferBarrierCount();
        uint16_t prevBufferBarrierCount = 0;
        uint16_t prevImageBarrierBegin = mTransientResources.getPassPrevImageBarrierCount();
        uint16_t prevImageBarrierCount = 0;
        uint16_t postBufferBarrierBegin = mTransientResources.getPassPostBufferBarrierCount();
        uint16_t postBufferBarrierCount = 0;
        uint16_t postImageBarrierBegin = mTransientResources.getPassPostImageBarrierCount();
        uint16_t postImageBarrierCount = 0;

        PassBase* pass = passNode->getPass();
        // 遍历资源, 推导对应的 barriers
        for (const auto& dependencyRecord : pass->getDependencyRecords())
        {
            VirtualResource* resource = getResource(dependencyRecord.resource);
            if (resource->getType() == ResourceType::Buffer)
            {
                BufferResource* bufferResource = static_cast<BufferResource*>(resource);
                auto accessRecords = bufferResource->getAccessRecords();

                auto recordIt = std::find_if(
                    accessRecords.rbegin(),
                    accessRecords.rend(),
                    [passNode](const BufferAccessRecord& record)
                    {
                        return record.passNode == passNode;
                    }
                );
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
                    if (prevRecord.accessType != AccessType::Read_Only || record.accessType != AccessType::Read_Only)
                    {
                        VkBufferMemoryBarrier2 barrier = {};
                        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                        inferBufferStageAccessMask(
                            prevRecord.passNode->getPass()->getType(),
                            prevRecord.usage,
                            prevRecord.accessType,
                            false,
                            barrier.srcStageMask,
                            barrier.srcAccessMask
                        );
                        inferBufferStageAccessMask(
                            pass->getType(),
                            record.usage,
                            record.accessType,
                            true,
                            barrier.dstStageMask,
                            barrier.dstAccessMask
                        );
                        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.buffer = bufferResource->getPhysicalResource()->getHandle();
                        barrier.offset = 0;
                        barrier.size = bufferResource->getPhysicalResource()->getSize();
                        mTransientResources.addPassPrevBufferBarrier(barrier);
                        ++prevBufferBarrierCount;
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
                TextureResource* textureResource = nullptr;
                vk::ImageView* imageView = nullptr;
                if (resource->getType() == ResourceType::Texture)
                {
                    textureResource = static_cast<TextureResource*>(resource);
                    imageView = textureResource->getPhysicalResource();
                }
                else if (resource->getType() == ResourceType::SubTexture)
                {
                    SubTextureResource* subTextureResource = static_cast<SubTextureResource*>(resource);
                    textureResource = subTextureResource->getTextureResource();
                    imageView = subTextureResource->getPhysicalResource();
                }
                assert(textureResource && imageView);
                auto accessRecords = textureResource->getAccessRecords();
                const VkImageSubresourceRange& range = imageView->getRange();

                // 找到当前pass的访问记录
                auto recordIt = std::find_if(
                    accessRecords.rbegin(),
                    accessRecords.rend(),
                    [passNode, range](const TextureAccessRecord& record)
                    {
                        return record.passNode == passNode && subresourceRangeEqual(record.range, range);
                    }
                );
                assert(recordIt != accessRecords.rend());
                auto& record = *recordIt;
                
                VkImageMemoryBarrier2 barrier = {};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                inferImageStageAccessMask(
                    pass->getType(),
                    record.usage,
                    record.accessType,
                    true,
                    barrier.dstStageMask,
                    barrier.dstAccessMask
                );
                barrier.newLayout = inferImageLayout(record.usage, record.accessType);
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = textureResource->getPhysicalResource()->getImage().getHandle();

                const bool readOnlyAccess = !enumHasAnyFlags(record.accessType, AccessType::Write_Only | AccessType::Stencil_Write_Only);

                // 需要向前找到最近的N个能覆盖当前pass访问range(以下简称PassRange)的访问记录
                // 声明一个FindedRangeSet, 其代表已经找到的覆盖了PassRange的部分, 初始为空;
                // 每向前遍历到一个非本pass的访问记录, 则将其访问range与PassRange求交集,
                // 如果存在交集, 则再用交集减去FindedRangeSet, 得到若干个range组成的差集,
                // 根据这些差集构建barrier, 并将它们插入到FindedRangeSet中, 继续向前遍历,
                // 直到遍历完成或FindedRangeSet全等于PassRange
                SubresourceRangeSet findedRangeSet(mArena);
                bool findAllRange = false;
                for (auto it = std::next(recordIt); it != accessRecords.rend(); ++it)
                {
                    auto& prevRecord = *it;

                    if (prevRecord.passNode == passNode)
                        continue;

                    auto intersection = intersectSubresourceRange(prevRecord.range, record.range);
                    if (!intersection)
                        continue;

                    barrier.srcStageMask = 0;
                    barrier.srcAccessMask = 0;
                    inferImageStageAccessMask(
                        prevRecord.passNode->getPass()->getType(),
                        prevRecord.usage,
                        prevRecord.accessType,
                        false,
                        barrier.srcStageMask,
                        barrier.srcAccessMask
                    );
                    barrier.oldLayout = inferImageLayout(prevRecord.usage, prevRecord.accessType);

                    // TODO: RAR barrier 省略条件待验证
                    // 当前假设：同布局 + 只读 → barrier 可省略
                    // 如果出现跨阶段 hazard，再补充执行依赖
                    const bool readAfterRead = readOnlyAccess && !enumHasAnyFlags(prevRecord.accessType, AccessType::Write_Only | AccessType::Stencil_Write_Only);
                    const bool dontNeedBarrier = readAfterRead && barrier.oldLayout == barrier.newLayout;

                    auto differences = subtractSubresourceRangeSet(*intersection, findedRangeSet);
                    for (auto& diff : differences)
                    {
                        findedRangeSet.insert(diff);

                        if (!dontNeedBarrier)
                        {
                            barrier.subresourceRange = diff;
                            mTransientResources.addPassPrevImageBarrier(barrier);
                            ++prevImageBarrierCount;
                        }
                    }

                    auto ranges = findedRangeSet.getRanges();
                    if (ranges.size() == 1 && subresourceRangeEqual(ranges[0], record.range))
                    {
                        findAllRange = true;
                        break;
                    }
                }

                // 没有找到所有的range, 存在range尚未被访问过
                if (!findAllRange)
                {
                    const bool dontNeedBarrier = readOnlyAccess && textureResource->isImported() && textureResource->getDefaultLayout() == barrier.newLayout;

                    // 保守起见, 我们规定, 导入纹理必须在第一次访问前插入barrier,
                    // 即使它的访问布局和默认布局可能一致, 而最后一次访问后可以因为布局一致而不插入barrier
                    //if (!dontNeedBarrier)
                    {
                        auto unaccessedRanges = subtractSubresourceRangeSet(record.range, findedRangeSet);
                        for (auto& r : unaccessedRanges)
                        {
                            // 如果是导入纹理, 则从默认布局转换到目标布局
                            // 否则从Undefined布局转换到目标布局
                            barrier.srcStageMask = VK_PIPELINE_STAGE_NONE;
                            barrier.srcAccessMask = 0;
                            barrier.oldLayout = textureResource->isImported() ? textureResource->getDefaultLayout() : VK_IMAGE_LAYOUT_UNDEFINED;
                            barrier.subresourceRange = r;

                            mTransientResources.addPassPrevImageBarrier(barrier);
                            ++prevImageBarrierCount;
                        }
                    }
                }

                // 如果本次访问是导入纹理的最后一次访问, 则需要推导post barrier
                if (textureResource->isImported() && passNode == textureResource->getLastPass())
                {
                    barrier.dstStageMask = VK_PIPELINE_STAGE_NONE;
                    barrier.dstAccessMask = 0;
                    barrier.newLayout = textureResource->getDefaultLayout();

                    findedRangeSet.clear();
                    auto& allRange = textureResource->getPhysicalResource()->getRange();
                    for (auto it = recordIt; it != accessRecords.rend(); ++it)
                    {
                        auto& prevRecord = *it;

                        // because intersection == prevRecord.range
                        // auto intersection = intersectSubresourceRange(prevRecord.range, allRange);
                        
                        barrier.srcStageMask = 0;
                        barrier.srcAccessMask = 0;
                        inferImageStageAccessMask(
                            prevRecord.passNode->getPass()->getType(),
                            prevRecord.usage,
                            prevRecord.accessType,
                            false,
                            barrier.srcStageMask,
                            barrier.srcAccessMask
                        );
                        barrier.oldLayout = inferImageLayout(prevRecord.usage, prevRecord.accessType);
                        const bool dontNeedBarrier = barrier.oldLayout == barrier.newLayout;

                        auto differences = subtractSubresourceRangeSet(prevRecord.range, findedRangeSet);
                        for (auto& diff : differences)
                        {
                            findedRangeSet.insert(diff);

                            if (!dontNeedBarrier)
                            {
                                barrier.subresourceRange = diff;
                                mTransientResources.addPassPostImageBarrier(barrier);
                                ++postImageBarrierCount;
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

        pass->setPrevBufferBarrierRange(prevBufferBarrierBegin, prevBufferBarrierCount);
        pass->setPrevImageBarrierRange(prevImageBarrierBegin, prevImageBarrierCount);
        pass->setPostBufferBarrierRange(postBufferBarrierBegin, postBufferBarrierCount);
        pass->setPostImageBarrierRange(postImageBarrierBegin, postImageBarrierCount);
    }

    bool RenderGraph::subresourceRangeEqual(const VkImageSubresourceRange& a, const VkImageSubresourceRange& b)
    {
        return a.aspectMask == b.aspectMask &&
            a.baseMipLevel == b.baseMipLevel &&
            a.levelCount == b.levelCount &&
            a.baseArrayLayer == b.baseArrayLayer &&
            a.layerCount == b.layerCount;
    }

    bool RenderGraph::checkSubresourceRangeHasOverlap(const VkImageSubresourceRange& a, const VkImageSubresourceRange& b)
    {
        if (!(a.aspectMask & b.aspectMask)) return false;
        const uint32_t aMipEnd = a.baseMipLevel + a.levelCount;
        const uint32_t bMipEnd = b.baseMipLevel + b.levelCount;
        const uint32_t aLayerEnd = a.baseArrayLayer + a.layerCount;
        const uint32_t bLayerEnd = b.baseArrayLayer + b.layerCount;
        const bool mipOverlap = !(aMipEnd <= b.baseMipLevel || bMipEnd <= a.baseMipLevel);
        const bool layerOverlap = !(aLayerEnd <= b.baseArrayLayer || bLayerEnd <= a.baseArrayLayer);
        return mipOverlap && layerOverlap;
    }

    std::optional<VkImageSubresourceRange> RenderGraph::intersectSubresourceRange(const VkImageSubresourceRange& a, const VkImageSubresourceRange& b)
    {
        VkImageSubresourceRange r;
        r.aspectMask = a.aspectMask & b.aspectMask;
        const uint32_t mipBegin = std::max(a.baseMipLevel, b.baseMipLevel);
        const uint32_t mipEnd = std::min(a.baseMipLevel + a.levelCount, b.baseMipLevel + b.levelCount);
        const uint32_t layerBegin = std::max(a.baseArrayLayer, b.baseArrayLayer);
        const uint32_t layerEnd = std::min(a.baseArrayLayer + a.layerCount, b.baseArrayLayer + b.layerCount);
        r.baseMipLevel = mipBegin;
        r.levelCount = (mipEnd > mipBegin) ? (mipEnd - mipBegin) : 0;
        r.baseArrayLayer = layerBegin;
        r.layerCount = (layerEnd > layerBegin) ? (layerEnd - layerBegin) : 0;
        if (r.aspectMask == VK_IMAGE_ASPECT_NONE ||
            r.levelCount == 0 ||
            r.layerCount == 0)
            return std::nullopt;
        return r;
    }

    VkImageLayout RenderGraph::inferImageLayout(TextureUsage usage, AccessType accessType)
    {
        switch (usage)
        {
        case TextureUsage::Color_Attachment:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case TextureUsage::Depth_Attachment:
        {
            if (!enumHasAllFlags(accessType, AccessType::Write_Only))
                return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
            return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        }
        case TextureUsage::Stencil_Attachment:
        {
            if (!enumHasAllFlags(accessType, AccessType::Stencil_Write_Only))
                return VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL;
            return VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
        }
        case TextureUsage::Depth_Stencil_Attachment:
        {
            if (enumHasAllFlags(accessType, AccessType::Write_Only) && !enumHasAllFlags(accessType, AccessType::Stencil_Write_Only))
                return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
            if (!enumHasAllFlags(accessType, AccessType::Write_Only) && enumHasAllFlags(accessType, AccessType::Stencil_Write_Only))
                return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
            if (!enumHasAllFlags(accessType, AccessType::Write_Only) && !enumHasAllFlags(accessType, AccessType::Stencil_Write_Only))
                return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        case TextureUsage::Sampled:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case TextureUsage::Storage:
            return VK_IMAGE_LAYOUT_GENERAL;
        case TextureUsage::Transfer:
        {
            if (!enumHasAllFlags(accessType, AccessType::Write_Only))
                return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        default:
            return VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    void RenderGraph::inferBufferStageAccessMask(
        PassType passType,
        BufferUsage usage,
        AccessType accessType,
        bool isDst,
        VkPipelineStageFlags2& stageMask,
        VkAccessFlags2& accessMask
    )
    {
        if (enumHasAnyFlags(usage, BufferUsage::Uniform))
        {
            if (passType == PassType::Compute)
                stageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            else if (passType == PassType::Graphics || passType == PassType::Present)
                stageMask |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

            if (isDst)
                accessMask |= VK_ACCESS_2_UNIFORM_READ_BIT;
        }
        if (enumHasAnyFlags(usage, BufferUsage::Storage))
        {
            if (passType == PassType::Compute)
                stageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            else if (passType == PassType::Graphics || passType == PassType::Present)
                stageMask |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

            if (enumHasAllFlags(accessType, AccessType::Read_Only) && isDst)
                accessMask |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            if (enumHasAllFlags(accessType, AccessType::Write_Only))
                accessMask |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }
        if (enumHasAnyFlags(usage, BufferUsage::Vertex))
        {
            stageMask |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;

            if (isDst)
                accessMask |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        }
        if (enumHasAnyFlags(usage, BufferUsage::Index))
        {
            stageMask |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;

            if (isDst)
                accessMask |= VK_ACCESS_2_INDEX_READ_BIT;
        }
        if (enumHasAnyFlags(usage, BufferUsage::Indirect))
        {
            stageMask |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

            if (isDst)
                accessMask |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        }
        if (enumHasAnyFlags(usage, BufferUsage::Transfer))
        {
            stageMask |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            if (enumHasAllFlags(accessType, AccessType::Write_Only))
                accessMask |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
            else
                accessMask |= VK_ACCESS_2_TRANSFER_READ_BIT;
        }
    }

    void RenderGraph::inferImageStageAccessMask(
        PassType passType,
        TextureUsage usage,
        AccessType accessType,
        bool isDst,
        VkPipelineStageFlags2& stageMask,
        VkAccessFlags2& accessMask
    )
    {
        switch (usage)
        {
        case TextureUsage::Color_Attachment:
        {
            stageMask |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            if (enumHasAllFlags(accessType, AccessType::Read_Only) && isDst)
                accessMask |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
            if (enumHasAllFlags(accessType, AccessType::Write_Only))
                accessMask |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        }
        case TextureUsage::Depth_Attachment:
        case TextureUsage::Stencil_Attachment:
        case TextureUsage::Depth_Stencil_Attachment:
        {
            stageMask |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            if (enumHasAllFlags(accessType, AccessType::Read_Only) && isDst)
                accessMask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            // NOTE: 对于Depth/Stencil Attachment, 不存在只写操作, 写时必定会读
            if (enumHasAllFlags(accessType, AccessType::Write_Only))
                accessMask |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        }
        case TextureUsage::Sampled:
        {
            if (passType == PassType::Compute)
                stageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            else if (passType == PassType::Graphics || passType == PassType::Present)
                stageMask |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

            if (isDst)
                accessMask |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            break;
        }
        case TextureUsage::Storage:
        {
            if (passType == PassType::Compute)
                stageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            else if (passType == PassType::Graphics || passType == PassType::Present)
                stageMask |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

            if (enumHasAllFlags(accessType, AccessType::Read_Only) && isDst)
                accessMask |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            if (enumHasAllFlags(accessType, AccessType::Write_Only))
                accessMask |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            break;
        }
        case TextureUsage::Transfer:
        {
            stageMask |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            if (enumHasAllFlags(accessType, AccessType::Write_Only))
                accessMask |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
            else
                accessMask |= VK_ACCESS_2_TRANSFER_READ_BIT;
            break;
        }
        /*case TextureUsage::Present:
        {
            if (!isDst)
            {
                stageMask |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                accessMask |= VK_ACCESS_2_MEMORY_WRITE_BIT;
            }
            break;
        }*/
        default:
            break;
        }
    }

    void RenderGraph::SubresourceRangeSet::insert(const VkImageSubresourceRange& range)
    {
        VkImageSubresourceRange current = range;

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

    std::optional<VkImageSubresourceRange> RenderGraph::SubresourceRangeSet::tryMergeSubresourceRanges(const VkImageSubresourceRange& a, const VkImageSubresourceRange& b)
    {
        // 合并条件:
        // 1. mip和array有且只有一个维度beg与end相同, 另一个维度二者相邻或有重合
        // 2. aspect: 取并集
        
        // 1. 计算区间端点
        uint32_t aMipBeg = a.baseMipLevel;
        uint32_t aMipEnd = a.baseMipLevel + a.levelCount;
        uint32_t bMipBeg = b.baseMipLevel;
        uint32_t bMipEnd = b.baseMipLevel + b.levelCount;

        uint32_t aLayBeg = a.baseArrayLayer;
        uint32_t aLayEnd = a.baseArrayLayer + a.layerCount;
        uint32_t bLayBeg = b.baseArrayLayer;
        uint32_t bLayEnd = b.baseArrayLayer + b.layerCount;

        // 2. 判断是否满足“有且只有一个维度对齐，另一个维度相邻或重叠”
        bool mipEqual = (aMipBeg == bMipBeg && aMipEnd == bMipEnd);
        bool layerEqual = (aLayBeg == bLayBeg && aLayEnd == bLayEnd);

        bool mipAdjOrOverlap = (aMipEnd >= bMipBeg && bMipEnd >= aMipBeg);
        bool layerAdjOrOverlap = (aLayEnd >= bLayBeg && bLayEnd >= aLayBeg);

        if (!((mipEqual && layerAdjOrOverlap) ||
            (layerEqual && mipAdjOrOverlap))) {
            return std::nullopt;
        }

        // 3. 构造合并结果
        VkImageSubresourceRange merged{};
        merged.aspectMask = a.aspectMask | b.aspectMask;
        merged.baseMipLevel = std::min(aMipBeg, bMipBeg);
        merged.levelCount = std::max(aMipEnd, bMipEnd) - merged.baseMipLevel;
        merged.baseArrayLayer = std::min(aLayBeg, bLayBeg);
        merged.layerCount = std::max(aLayEnd, bLayEnd) - merged.baseArrayLayer;

        return merged;
    }

    uint8_t RenderGraph::subtractSubresourceRange(const VkImageSubresourceRange& range, const VkImageSubresourceRange& cover, std::array<VkImageSubresourceRange, 5>& result)
    {
        uint8_t resultCount = 0;

        VkImageAspectFlags overlapedAspectMask = range.aspectMask & cover.aspectMask;

        // 外部已经检查过该条件
        assert(overlapedAspectMask != VK_IMAGE_ASPECT_NONE);

        if (overlapedAspectMask != range.aspectMask)
        {
            result[resultCount++] = {
                range.aspectMask & ~(overlapedAspectMask),
                range.baseMipLevel,
                range.levelCount,
                range.baseArrayLayer,
                range.layerCount
            };
        }

        uint32_t rMipEnd = range.baseMipLevel + range.levelCount;
        uint32_t rLayerEnd = range.baseArrayLayer + range.layerCount;
        uint32_t cMipEnd = cover.baseMipLevel + cover.levelCount;
        uint32_t cLayerEnd = cover.baseArrayLayer + cover.layerCount;

        bool overlap = !(rMipEnd <= cover.baseMipLevel ||
                         cMipEnd <= range.baseMipLevel ||
                         rLayerEnd <= cover.baseArrayLayer ||
                         cLayerEnd <= range.baseArrayLayer);

        if (!overlap)
        {
            result[resultCount++] = {
                overlapedAspectMask,
                range.baseMipLevel,
                range.levelCount,
                range.baseArrayLayer,
                range.layerCount
            };
            return resultCount;
        }

        // 交集
        uint32_t mipMin = std::max(range.baseMipLevel, cover.baseMipLevel);
        uint32_t mipMax = std::min(rMipEnd, cMipEnd);
        uint32_t layerMin = std::max(range.baseArrayLayer, cover.baseArrayLayer);
        uint32_t layerMax = std::min(rLayerEnd, cLayerEnd);

        // 上方
        if (range.baseMipLevel < mipMin)
        {
            result[resultCount++] = {
                overlapedAspectMask,
                range.baseMipLevel,
                mipMin - range.baseMipLevel,
                range.baseArrayLayer,
                range.layerCount
            };
        }
        // 下方
        if (mipMax < rMipEnd)
        {
            result[resultCount++] = {
                overlapedAspectMask,
                mipMax,
                rMipEnd - mipMax,
                range.baseArrayLayer,
                range.layerCount
            };
        }
        // 左侧
        if (range.baseArrayLayer < layerMin)
        {
            result[resultCount++] = {
                overlapedAspectMask,
                mipMin,
                mipMax - mipMin,
                range.baseArrayLayer,
                layerMin - range.baseArrayLayer
            };
        }
        // 右侧
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

    ArenaVector<VkImageSubresourceRange> RenderGraph::subtractSubresourceRangeSet(const VkImageSubresourceRange& range, const SubresourceRangeSet& set)
    {
        ArenaVector<VkImageSubresourceRange> work = ArenaVector<VkImageSubresourceRange>(StlAllocator<VkImageSubresourceRange>(mArena));

        if (set.getRanges().empty())
        {
            work.emplace_back(range);
            return work;
        }

        ArenaVector<VkImageSubresourceRange> next = ArenaVector<VkImageSubresourceRange>(StlAllocator<VkImageSubresourceRange>(mArena));

        work.reserve(32);
        next.reserve(32);

        work.emplace_back(range);

        for (const auto& cover : set.getRanges())
        {
            next.clear();

            for (auto& r : work)
            {
                // 无交集, 直接加入结果
                if ((r.aspectMask & cover.aspectMask) == VK_IMAGE_ASPECT_NONE)
                {
                    next.emplace_back(r);
                    continue;
                }

                // 存在交集, 计算差集, 加入结果
                std::array<VkImageSubresourceRange, 5> parts;
                uint8_t partCount = subtractSubresourceRange(r, cover, parts);
                next.insert(next.end(), parts.begin(), parts.begin() + partCount);
            }

            work.swap(next);
        }

        return work;
    }

}
