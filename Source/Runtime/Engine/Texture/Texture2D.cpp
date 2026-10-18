#include "Texture2D.h"

#include "../Context.h"
#include <RenderBase/RenderFrontend.h>
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
        uint32_t mipLevels
    )
    {
        Description description;
        description.format = format;
        description.width = width;
        description.height = height;
        description.depth = 1;
        description.mipLevels = computeMipLevels(std::max(width, height), mipLevels);
        description.arrayLayers = 1;
        setDescription(description);
    }

    void Texture2D::createTextureProxy(TextureProxyHandle handle)
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [handle,
            desc = getDescription(),
            rhiFormat = getRHITextureFormat()](FrameContext& context)
            {
                rhi::TextureDesc textureDesc;
                textureDesc.type = rhi::TextureType::Texture_2D;
                textureDesc.format = rhiFormat;
                textureDesc.extent = { desc.width, desc.height, 1 };
                textureDesc.mipCount = desc.mipLevels;
                textureDesc.layerCount = 1;
                textureDesc.usage = rhi::TextureUsage::Transfer_Src | rhi::TextureUsage::Transfer_Dst | rhi::TextureUsage::Sampled;

                context.getProxyManager().createProxy(handle, textureDesc);
            }
        );
    }

    template <> Type* Reflection::createType<Texture2D>()
    {
        auto c = new TClass<Texture2D, Texture>("Texture2D");
        c->setDisplayName("Texture 2D");
        return c;
    }
}
