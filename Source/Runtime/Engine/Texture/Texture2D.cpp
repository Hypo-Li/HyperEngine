#include "Texture2D.h"

#include "../Context.h"
#include <RenderBase/RenderSystem.h>
#include <RenderBase/TextureProxy.h>

namespace hyp
{
    REGISTER_CLASS(Texture2D)

    Texture2D::Texture2D()
    {
    }

    Texture2D::~Texture2D() noexcept
    {
    }

    void Texture2D::initialize(
        uint32_t width,
        uint32_t height,
        PixelFormat format,
        bool isSRGB,
        CompressionPreset compressionPreset,
        bool generateMips
    )
    {
        Description description;
        description.format = format;
        description.isSRGB = isSRGB;
        description.compressionPreset = compressionPreset;
        description.generateMips = generateMips;
        description.width = width;
        description.height = height;
        description.depth = 1;
        description.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
        description.arrayLayers = 1;
        setDescription(description);
    }

    void Texture2D::createTextureProxy(TextureProxyHandle handle)
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [handle,
            desc = getDescription()](RenderFrameContext& context)
            {
                vk::Image::CreateInfo imageInfo;
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.format = static_cast<VkFormat>(convertToVkFormat(desc.format, desc.compressionPreset, desc.isSRGB));
                imageInfo.extent = { desc.width, desc.height, 1 };
                imageInfo.mipLevels = desc.mipLevels;
                imageInfo.arrayLayers = 1;
                imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

                context.getProxyManager().createProxy(handle, imageInfo);
            }
        );
    }

    template <> Type* Reflection::createType<Texture2D>()
    {
        auto c = new TClass<Texture2D, Texture>("Texture2D");
        return c;
    }
}
