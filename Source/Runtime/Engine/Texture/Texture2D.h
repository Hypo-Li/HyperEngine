#pragma once
#include "Texture.h"

namespace hyp
{
    class Texture2D : public Texture
    {
        REFLECT_CLASS(Texture2D)
    public:
        Texture2D();

        ~Texture2D() noexcept;

        void initialize(
            uint32_t width,
            uint32_t height,
            PixelFormat format,
            bool isSRGB,
            CompressionPreset compressionPreset,
            bool generateMips = true
        );

    protected:
        void createTextureProxy(TextureProxyHandle handle) override;
    };

    template <> Type* Reflection::createType<Texture2D>();
}
