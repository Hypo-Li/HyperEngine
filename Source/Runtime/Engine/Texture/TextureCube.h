#pragma once
#include "Texture.h"

namespace hyp
{
    class TextureCube : public Texture
    {
        REFLECT_CLASS(TextureCube)
    public:
        TextureCube();

        ~TextureCube() noexcept;

        void initialize(
            uint32_t width,
            PixelFormat format,
            bool isSRGB,
            CompressionPreset compressionPreset,
            bool generateMips = true,
            uint32_t mipLevels = 0
        );

    protected:
        void createTextureProxy(TextureProxyHandle handle) override;
    };

    template <> Type* Reflection::createType<TextureCube>();

    std::vector<float3> equirectMapToCubemap(
        std::span<const float3> equirectMapPixels,
        uint32_t width, uint32_t height,
        uint32_t cubemapSize,
        float maxLuminance = 1e6f
    );
}
