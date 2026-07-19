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
            desc = getDescription()](FrameContext& context)
            {
                rhi::TextureDesc textureDesc;
                textureDesc.type = rhi::TextureType::Texture_3D;
                textureDesc.format = convertToRHITextureFormat(desc.format, desc.compressionPreset, desc.compressionFormatFamily, desc.isSRGB);
                textureDesc.extent = { desc.width, desc.height, desc.depth };
                textureDesc.mipCount = desc.mipLevels;
                textureDesc.layerCount = 1;
                textureDesc.usage = rhi::TextureUsage::Transfer_Src | rhi::TextureUsage::Transfer_Dst | rhi::TextureUsage::Sampled;

                context.getProxyManager().createProxy(handle, textureDesc);
            }
        );
    }

    template <> Type* Reflection::createType<Texture3D>()
    {
        auto c = new TClass<Texture3D, Texture>("Texture3D");
        return c;
    }

}
