#include "Resource.h"
#include "RenderGraph.h"

namespace hyp::rg
{
    VirtualResource::VirtualResource(HString name, ResourceType type, bool imported) noexcept
        : mName(name), mType(type), mIsImported(imported), mIsRegistered(false)
    {
    }

    BufferResource::BufferResource(HString name, const Description& desc, Arena& arena) noexcept
        : VirtualResource(name, ResourceType::Buffer, false),
        mAccessRecords(StlAllocator<BufferAccessRecord>(arena))
    {
        mKey.desc = desc;
        mKey.name = name;
        mAccessRecords.reserve(8);
    }

    BufferResource::BufferResource(HString name, PhysicalResource* physicalResource, Arena& arena) noexcept
        : VirtualResource(name, ResourceType::Buffer, true),
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
        mPhysicalResource = transientResources.acquireBuffer(mKey);
    }

    void BufferResource::aggregateUsage() noexcept
    {
        for (auto& record : mAccessRecords)
        {
            mKey.desc.usage |= record.usage;
        }
    }

    TextureResource::TextureResource(HString name, const Description& desc, Arena& arena) noexcept
        : VirtualResource(name, ResourceType::Texture, false),
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
        mPhysicalResource = transientResources.acquireTexture(mKey);
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
