#include "Resource.h"
#include "RenderGraph.h"

namespace hyp::rg
{
    VirtualResource::VirtualResource(HString name, ResourceType type, bool imported) noexcept
        : mName(name), mType(type), mIsImported(imported), mIsRegistered(false)
    {
    }

    BufferResource::BufferResource(HString name, const Description& desc, Arena& arena, uint32_t maxUnusedFrames) noexcept
        : VirtualResource(name, ResourceType::Buffer, false),
        mMaxUnusedFrames(maxUnusedFrames),
        mAccessRecords(StlAllocator<BufferAccessRecord>(arena))
    {
        mKey.desc = desc;
        mKey.name = name;
        mAccessRecords.reserve(8);
    }

    BufferResource::BufferResource(HString name, PhysicalResource* physicalResource, Arena& arena) noexcept
        : VirtualResource(name, ResourceType::Buffer, true),
        mMaxUnusedFrames(TransientResources::kDefaultMaxUnusedFrames),
        mPhysicalResource(physicalResource),
        mAccessRecords(StlAllocator<BufferAccessRecord>(arena))
    {
        mKey.desc.size = physicalResource->getDesc().size;
        mKey.name = name;
        mAccessRecords.reserve(8);
    }

    void BufferResource::devirtualize(TransientResources& transientResources) noexcept
    {
        assert(!isImported());

        aggregateUsage();
        mPhysicalResource = transientResources.acquireBuffer(mKey, mMaxUnusedFrames);
    }

    void BufferResource::aggregateUsage() noexcept
    {
        for (auto& record : mAccessRecords)
        {
            mKey.desc.usage |= record.usage;
        }
    }

    TextureResource::TextureResource(HString name, const Description& desc, Arena& arena, uint32_t maxUnusedFrames) noexcept
        : VirtualResource(name, ResourceType::Texture, false),
        mMaxUnusedFrames(maxUnusedFrames),
        mAccessRecords(StlAllocator<TextureAccessRecord>(arena))
    {
        mKey.desc = desc;
        mKey.name = name;
        mAccessRecords.reserve(8);
    }

    TextureResource::TextureResource(
        HString name,
        PhysicalResource* physicalResource,
        rhi::ResourceState defaultState,
        Arena& arena
    ) noexcept :
        VirtualResource(name, ResourceType::Texture, true),
        mMaxUnusedFrames(TransientResources::kDefaultMaxUnusedFrames),
        mPhysicalResource(physicalResource),
        mDefaultState(defaultState),
        mAccessRecords(StlAllocator<TextureAccessRecord>(arena))
    {
        mKey.desc = physicalResource->getTexture().getDesc();
        mKey.name = name;
        mAccessRecords.reserve(8);
    }

    void TextureResource::devirtualize(TransientResources& transientResources) noexcept
    {
        assert(!isImported());

        aggregateUsage();
        mPhysicalResource = transientResources.acquireTexture(mKey, mMaxUnusedFrames);
    }

    void TextureResource::aggregateUsage() noexcept
    {
        for (auto& record : mAccessRecords)
        {
            mKey.desc.usage |= record.usage;
        }
    }

    SubTextureResource::SubTextureResource(
        HString name,
        uint16_t textureResourceId,
        TextureResource* textureResource,
        const Description& desc
    ) noexcept :
        VirtualResource(name, ResourceType::SubTexture, false),
        mTextureResourceId(textureResourceId),
        mTextureResource(textureResource)
    {
        mKey.desc = desc;
        mKey.name = name;
    }

    void SubTextureResource::devirtualize(TransientResources& transientResources) noexcept
    {
        assert(mTextureResource->getPhysicalResource());
        mPhysicalResource = transientResources.acquireSubTexture(*mTextureResource->getPhysicalResource(), mKey);
    }

}
