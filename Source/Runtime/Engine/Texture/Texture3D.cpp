#include "Texture3D.h"

#include "../Context.h"
#include <RenderBase/RenderSystem.h>
#include <RenderBase/TextureProxy.h>

namespace hyp
{
    REGISTER_CLASS(Texture3D)

    Texture3D::Texture3D()
    {
    }

    Texture3D::~Texture3D() noexcept
    {
    }

    void Texture3D::initialize(
        uint32_t width,
        uint32_t height,
        uint32_t depth,
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
        description.depth = depth;
        description.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(std::max(width, height), depth)))) + 1;
        description.arrayLayers = 1;
        setDescription(description);
    }

    void Texture3D::createTextureProxy(TextureProxyHandle handle)
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [handle,
            desc = getDescription()](RenderFrameContext& context)
            {
                vk::Image::CreateInfo imageInfo;
                imageInfo.imageType = VK_IMAGE_TYPE_3D;
                imageInfo.format = static_cast<VkFormat>(convertToVkFormat(desc.format, desc.compressionPreset, desc.isSRGB));
                imageInfo.extent = { desc.width, desc.height, desc.depth };
                imageInfo.mipLevels = desc.mipLevels;
                imageInfo.arrayLayers = 1;
                imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

                context.getProxyManager().createProxy(handle, imageInfo);
            }
        );
    }

    template <> Type* Reflection::createType<Texture3D>()
    {
        auto c = new TClass<Texture3D, Texture>("Texture3D");
        return c;
    }

}
