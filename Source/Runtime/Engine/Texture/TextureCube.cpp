#include "TextureCube.h"

#include "../Context.h"
#include <RenderBase/RenderSystem.h>
#include <RenderBase/TextureProxy.h>

namespace hyp
{
    REGISTER_CLASS(TextureCube)

    TextureCube::TextureCube()
    {
    }

    TextureCube::~TextureCube() noexcept
    {
    }

    void TextureCube::initialize(
        uint32_t width,
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
        description.height = width;
        description.depth = 1;
        description.mipLevels = static_cast<uint32_t>(std::floor(std::log2(width))) + 1;
        description.arrayLayers = 6;
        setDescription(description);
    }

    void TextureCube::createTextureProxy(TextureProxyHandle handle)
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
                imageInfo.arrayLayers = desc.arrayLayers;
                imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
                imageInfo.isCube = true;

                context.getProxyManager().createProxy(handle, imageInfo);
            }
        );
    }

    template <> Type* Reflection::createType<TextureCube>()
    {
        auto c = new TClass<TextureCube, Texture>("TextureCube");
        return c;
    }
}
