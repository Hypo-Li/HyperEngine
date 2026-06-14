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
            desc = getDescription()](RenderFrameContext& context) mutable
            {
                vk::Image::CreateInfo imageInfo;
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.format = static_cast<VkFormat>(convertToVkFormat(desc.format, desc.compressionPreset, desc.isSRGB));
                imageInfo.extent = { desc.width, desc.height, desc.depth };
                imageInfo.mipLevels = 1;
                imageInfo.arrayLayers = 1;
                imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                context.getProxyManager().createProxy(proxyHandle, imageInfo);
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
