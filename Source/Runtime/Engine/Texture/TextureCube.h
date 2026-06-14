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
            bool generateMips = true
        );

    protected:
        void createTextureProxy(TextureProxyHandle handle) override;
    };

    template <> Type* Reflection::createType<TextureCube>();
}
