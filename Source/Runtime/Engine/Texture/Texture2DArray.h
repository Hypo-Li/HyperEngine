#pragma once
#include "Texture.h"

namespace hyp
{
    class Texture2DArray : public Texture
    {
        REFLECT_CLASS(Texture2DArray)
    public:
        Texture2DArray();

        ~Texture2DArray() noexcept;

        void initialize(
            uint32_t width,
            uint32_t height,
            uint32_t layers,
            PixelFormat format,
            bool isSRGB,
            CompressionPreset compressionPreset,
            bool generateMips = true
        );

    protected:
        void createTextureProxy(TextureProxyHandle handle) override;
    };

    template <> Type* Reflection::createType<Texture2DArray>();
}
