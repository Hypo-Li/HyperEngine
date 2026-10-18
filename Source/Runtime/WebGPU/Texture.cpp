#include "Texture.h"
#include "WgpuFormatUtil.h"

#include <Core/Common/LogSystem.h>

namespace hyp::wgpu
{
    static WGPUTextureDimension convertTextureType(rhi::TextureType type) noexcept
    {
        switch (type)
        {
        case rhi::TextureType::Texture_2D:   return WGPUTextureDimension_2D;
        case rhi::TextureType::Texture_3D:   return WGPUTextureDimension_3D;
        case rhi::TextureType::Texture_Cube: return WGPUTextureDimension_2D;
        case rhi::TextureType::Texture_2D_Array: return WGPUTextureDimension_2D;
        default:                             return WGPUTextureDimension_2D;
        }
    }

    static WGPUTextureUsage convertTextureUsage(rhi::TextureUsage usage) noexcept
    {
        WGPUTextureUsage result = WGPUTextureUsage_None;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Transfer_Src))
            result |= WGPUTextureUsage_CopySrc;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Transfer_Dst))
            result |= WGPUTextureUsage_CopyDst;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Sampled))
            result |= WGPUTextureUsage_TextureBinding;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Storage))
            result |= WGPUTextureUsage_StorageBinding;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Color_Attachment))
            result |= WGPUTextureUsage_RenderAttachment;
        if (enumHasAnyFlags(usage, rhi::TextureUsage::Depth_Stencil_Attachment))
            result |= WGPUTextureUsage_RenderAttachment;
        return result;
    }

    static WGPUTextureAspect convertTextureAspect(rhi::TextureAspect aspect) noexcept
    {
        if (aspect == rhi::TextureAspect::Depth)
            return WGPUTextureAspect_DepthOnly;
        if (aspect == rhi::TextureAspect::Stencil)
            return WGPUTextureAspect_StencilOnly;
        return WGPUTextureAspect_All;
    }

    Texture::Texture(const Device& device, const rhi::TextureDesc& desc, rhi::MemoryType memoryType)
        : rhi::Texture(device, desc, memoryType)
    {
        WGPUTextureDescriptor textureDesc = {};
        textureDesc.nextInChain = nullptr;
        textureDesc.label = WGPUStringView{ "UnnamedTexture", WGPU_STRLEN };
        textureDesc.usage = convertTextureUsage(desc.usage);
        textureDesc.dimension = convertTextureType(desc.type);
        textureDesc.size.width = desc.extent.x;
        textureDesc.size.height = desc.extent.y;
        textureDesc.mipLevelCount = desc.mipCount;
        textureDesc.sampleCount = desc.samples;
        if (desc.type == rhi::TextureType::Texture_3D)
            textureDesc.size.depthOrArrayLayers = desc.extent.z;
        else if (desc.type == rhi::TextureType::Texture_Cube)
            textureDesc.size.depthOrArrayLayers = 6;
        else
            textureDesc.size.depthOrArrayLayers = desc.layerCount;

        textureDesc.format = convertTextureFormat(desc.format);

        mTexture = wgpuDeviceCreateTexture(device.getHandle(), &textureDesc);
    }

    Texture::Texture(const Device& device, const rhi::TextureDesc& desc, rhi::MemoryType memoryType, WGPUTexture handle)
        : rhi::Texture(device, desc, memoryType), mTexture(handle), mOwnsHandle(false)
    {
    }

    Texture::~Texture()
    {
        if (mTexture && mOwnsHandle)
        {
            wgpuTextureRelease(mTexture);
            mTexture = nullptr;
        }
    }

    void Texture::setDebugName(HString name)
    {
        rhi::Texture::setDebugName(name);
        WGPUStringView label = WGPUStringView{ getDebugName().toCString(), WGPU_STRLEN };
        wgpuTextureSetLabel(mTexture, label);
    }

    TRef<rhi::TextureView> Texture::createViewImpl(const rhi::TextureViewDesc& desc)
    {
        incViewCount();
        return new TextureView(*this, desc);
    }

    TextureView::TextureView(Texture& texture, const rhi::TextureViewDesc& desc)
        : rhi::TextureView(texture, desc)
    {
        WGPUTextureFormat compatibleFormat = convertTextureFormat(desc.format);
        if (desc.range.aspect == rhi::TextureAspect::Depth &&
            rhi::isDepthStencilFormat(desc.format))
        {
            switch (desc.format)
            {
            case rhi::TextureFormat::D16_UNORM_S8_UINT:
                compatibleFormat = WGPUTextureFormat_Depth16Unorm;
            case rhi::TextureFormat::D24_UNORM_S8_UINT:
                compatibleFormat = WGPUTextureFormat_Depth24Plus;
            case rhi::TextureFormat::D32_SFLOAT_S8_UINT:
                compatibleFormat = WGPUTextureFormat_Depth32Float;
            }
        }
        else if (desc.range.aspect == rhi::TextureAspect::Stencil &&
            rhi::isDepthStencilFormat(desc.format))
        {
            compatibleFormat = WGPUTextureFormat_Stencil8;
        }

        WGPUTextureViewDescriptor viewDesc = {};
        viewDesc.nextInChain = nullptr;
        viewDesc.label = WGPUStringView{ "UnnamedTextureView", WGPU_STRLEN };
        viewDesc.format = compatibleFormat;
        viewDesc.dimension = convertTextureViewType(desc.type);
        viewDesc.baseMipLevel = desc.range.baseMip;
        viewDesc.mipLevelCount = desc.range.mipCount;
        viewDesc.baseArrayLayer = desc.range.baseLayer;
        viewDesc.arrayLayerCount = desc.range.layerCount;
        viewDesc.aspect = convertTextureAspect(desc.range.aspect);
        viewDesc.usage = convertTextureUsage(texture.getDesc().usage);

        mTextureView = wgpuTextureCreateView(texture.getHandle(), &viewDesc);
    }

    TextureView::~TextureView()
    {
        if (mTextureView)
        {
            wgpuTextureViewRelease(mTextureView);
            mTextureView = nullptr;
        }
    }

    void TextureView::setDebugName(HString name)
    {
        rhi::TextureView::setDebugName(name);
        WGPUStringView label = WGPUStringView{ getDebugName().toCString(), WGPU_STRLEN };
        wgpuTextureViewSetLabel(mTextureView, label);
    }

    const Texture& TextureView::getWgpuTexture() const noexcept
    {
        return static_cast<const Texture&>(getTexture());
    }
}
