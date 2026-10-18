#include "Texture3D.h"

#include "../Context.h"
#include <RenderBase/RenderFrontend.h>
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
        uint32_t mipLevels
    )
    {
        Description description;
        description.format = format;
        description.width = width;
        description.height = height;
        description.depth = depth;
        description.mipLevels = computeMipLevels(std::max(std::max(width, height), depth), mipLevels);
        description.arrayLayers = 1;
        setDescription(description);
    }

    void Texture3D::createTextureProxy(TextureProxyHandle handle)
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [handle,
            desc = getDescription(),
            rhiFormat = getRHITextureFormat()](FrameContext& context)
            {
                rhi::TextureDesc textureDesc;
                textureDesc.type = rhi::TextureType::Texture_3D;
                textureDesc.format = rhiFormat;
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
        c->setDisplayName("Texture 3D");
        return c;
    }

}
