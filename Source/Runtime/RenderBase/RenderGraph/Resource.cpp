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
        mKey.description = desc;
        mKey.name = name;
        mAccessRecords.reserve(8);
    }

    BufferResource::BufferResource(HString name, PhysicalResource* physicalResource, Arena& arena) noexcept
        : VirtualResource(name, ResourceType::Buffer, true),
        mPhysicalResource(physicalResource),
        mAccessRecords(StlAllocator<BufferAccessRecord>(arena))
    {
        mKey.description.size = physicalResource->getSize();
        mKey.name = name;
        mAccessRecords.reserve(8);
    }

    void BufferResource::devirtualize(TransientResources& transientResources) noexcept
    {
        assert(!isImported());

        resolveUsage();
        mPhysicalResource = transientResources.acquireBuffer(mKey);

        if (vkSetDebugUtilsObjectNameEXT)
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = {};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
            nameInfo.objectHandle = uint64_t(mPhysicalResource->getHandle());
            nameInfo.pObjectName = getName().toCString();
            vkSetDebugUtilsObjectNameEXT(transientResources.getDevice().getHandle(), &nameInfo);
        }
    }

    void BufferResource::resolveUsage() noexcept
    {
        for (auto& record : mAccessRecords)
        {
            if (enumHasAnyFlags(record.usage, BufferUsage::Uniform))
            {
                mKey.description.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            }
            if (enumHasAnyFlags(record.usage, BufferUsage::Storage))
            {
                mKey.description.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            }
            if (enumHasAnyFlags(record.usage, BufferUsage::Vertex))
            {
                mKey.description.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            }
            if (enumHasAnyFlags(record.usage, BufferUsage::Index))
            {
                mKey.description.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            }
            if (enumHasAnyFlags(record.usage, BufferUsage::Indirect))
            {
                mKey.description.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            }
            if (enumHasAnyFlags(record.usage, BufferUsage::Transfer))
            {
                mKey.description.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            }
        }
    }

    TextureResource::TextureResource(HString name, const Description& desc, Arena& arena) noexcept
        : VirtualResource(name, ResourceType::Texture, false),
        mAccessRecords(StlAllocator<TextureAccessRecord>(arena))
    {
        mKey.description = desc;
        mKey.name = name;
        mAccessRecords.reserve(8);
    }

    TextureResource::TextureResource(
        HString name,
        PhysicalResource* physicalResource,
        VkImageLayout defaultLayout,
        Arena& arena
    ) noexcept :
        VirtualResource(name, ResourceType::Texture, true),
        mPhysicalResource(physicalResource),
        mDefaultLayout(defaultLayout),
        mAccessRecords(StlAllocator<TextureAccessRecord>(arena))
    {
        const auto& image = physicalResource->getImage();
        mKey.description.imageType = image.getType();
        mKey.description.format = image.getFormat();
        mKey.description.extent = image.getExtent();
        mKey.description.mipLevels = image.getMipLevels();
        mKey.description.arrayLayers = image.getArrayLayers();
        mKey.description.samples = image.getSamples();
        mKey.description.isCube = image.isCube();
        mKey.name = name;
        mAccessRecords.reserve(8);
    }

    void TextureResource::devirtualize(TransientResources& transientResources) noexcept
    {
        assert(!isImported());

        resolveUsage();
        mPhysicalResource = transientResources.acquireTexture(mKey);

        if (vkSetDebugUtilsObjectNameEXT)
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = {};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
            nameInfo.objectHandle = uint64_t(mPhysicalResource->getImage().getHandle());
            nameInfo.pObjectName = getName().toCString();
            vkSetDebugUtilsObjectNameEXT(transientResources.getDevice().getHandle(), &nameInfo);
        }
    }

    void TextureResource::resolveUsage() noexcept
    {
        for (auto& record : mAccessRecords)
        {
            switch (record.usage)
            {
            case TextureUsage::Color_Attachment:
                mKey.description.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                break;
            case TextureUsage::Depth_Attachment:
            case TextureUsage::Stencil_Attachment:
            case TextureUsage::Depth_Stencil_Attachment:
                mKey.description.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                break;
            case TextureUsage::Sampled:
                mKey.description.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
                break;
            case TextureUsage::Storage:
                mKey.description.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
                break;
            case TextureUsage::Transfer:
                mKey.description.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                break;
            default:
                break;
            }
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
        mKey.description = desc;
        mKey.name = name;
    }

    void SubTextureResource::devirtualize(TransientResources& transientResources) noexcept
    {
        assert(mTextureResource->getPhysicalResource());
        mPhysicalResource = transientResources.acquireSubTexture(mTextureResource->getPhysicalResource(), mKey);

        if (vkSetDebugUtilsObjectNameEXT)
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = {};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
            nameInfo.objectHandle = uint64_t(mPhysicalResource->getHandle());
            nameInfo.pObjectName = getName().toCString();
            vkSetDebugUtilsObjectNameEXT(transientResources.getDevice().getHandle(), &nameInfo);
        }
    }

}
