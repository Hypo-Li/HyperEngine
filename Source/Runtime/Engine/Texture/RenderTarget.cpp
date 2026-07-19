#include "RenderTarget.h"
#include "../Context.h"

#include <RenderBase/TextureProxy.h>
#include <RenderBase/RenderFrontend.h>

namespace hyp
{
    REGISTER_CLASS(RenderTarget)

    RenderTarget::RenderTarget()
    {

    }

    RenderTarget::~RenderTarget() noexcept
    {
    }

    void RenderTarget::setName(HString name) noexcept
    {
        if (mName == name)
            return;

        mName = name;
        mDirty = true;
    }

    void RenderTarget::setSize(uint32_t width, uint32_t height) noexcept
    {
        if (getDescription().width == width && getDescription().height == height)
            return;

        Description desc = getDescription();
        desc.width = width;
        setDescription(desc);
        mDirty = true;
    }

    void RenderTarget::setFormat(PixelFormat format) noexcept
    {
        if (getDescription().format == format)
            return;

        Description desc = getDescription();
        desc.format = format;
        setDescription(desc);
        mDirty = true;
    }

    void RenderTarget::apply()
    {
        if (mDirty)
        {
            destroyTextureProxy(getTextureProxyHandle());
            createTextureProxy(getTextureProxyHandle());
        }

        mDirty = false;
    }

    void RenderTarget::createTextureProxy(TextureProxyHandle proxyHandle)
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle,
            desc = getDescription()](FrameContext& context) mutable
            {
                rhi::TextureDesc textureDesc;
                textureDesc.type = rhi::TextureType::Texture_2D;
                textureDesc.format = convertToRHITextureFormat(desc.format, desc.compressionPreset, desc.compressionFormatFamily, desc.isSRGB);
                textureDesc.extent = { desc.width, desc.height, 1 };
                textureDesc.mipCount = 1;
                textureDesc.layerCount = 1;
                textureDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Color_Attachment;

                context.getProxyManager().createProxy(proxyHandle, textureDesc);
            }
        );
    }

    template <> Type* Reflection::createType<RenderTarget>()
    {
        auto c = new TClass<RenderTarget, Texture>("RenderTarget");
        c->addProperty("Name", &RenderTarget::mName);
        return c;
    }

}
