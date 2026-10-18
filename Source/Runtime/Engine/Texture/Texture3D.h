#pragma once
#include "Texture.h"

namespace hyp
{
    class Texture3D : public Texture
    {
        REFLECT_CLASS(Texture3D)
    public:
        Texture3D();

        ~Texture3D() noexcept;

        void initialize(
            uint32_t width,
            uint32_t height,
            uint32_t depth,
            PixelFormat format,
            uint32_t mipLevels = 0
        );

    protected:
        void createTextureProxy(TextureProxyHandle handle) override;
    };

    template <> Type* Reflection::createType<Texture3D>();
}
