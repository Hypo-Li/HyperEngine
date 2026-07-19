#include "Texture2DArray.h"

#include "../Context.h"
#include <RenderBase/RenderSystem.h>
#include <RenderBase/TextureProxy.h>

namespace hyp
{
    REGISTER_CLASS(Texture2DArray)

    Texture2DArray::Texture2DArray()
    {
    }

    Texture2DArray::~Texture2DArray() noexcept
    {
    }

    void Texture2DArray::initialize(
        uint32_t width,
        uint32_t height,
        uint32_t layers,
        PixelFormat format,
        bool isSRGB,
        CompressionPreset compressionPreset,
        bool generateMips)
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
        description.arrayLayers = layers;
        setDescription(description);
    }

    void Texture2DArray::createTextureProxy(TextureProxyHandle handle)
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [handle,
            desc = getDescription()](FrameContext& context)
            {
                rhi::TextureDesc textureDesc;
                textureDesc.type = rhi::TextureType::Texture_2D;
                textureDesc.format = convertToRHITextureFormat(desc.format, desc.compressionPreset, desc.compressionFormatFamily, desc.isSRGB);
                textureDesc.extent = { desc.width, desc.height, 1 };
                textureDesc.mipCount = desc.mipLevels;
                textureDesc.layerCount = desc.arrayLayers;
                textureDesc.usage = rhi::TextureUsage::Transfer_Src | rhi::TextureUsage::Transfer_Dst | rhi::TextureUsage::Sampled;

                context.getProxyManager().createProxy(handle, textureDesc);
            }
        );
    }

    template <> Type* Reflection::createType<Texture2DArray>()
    {
        auto c = new TClass<Texture2DArray, Texture>("Texture2DArray");
        return c;
    }

}
