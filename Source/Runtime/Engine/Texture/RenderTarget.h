#pragma once
#include "Texture.h"

namespace hyp
{
    class RenderTarget : public Texture
    {
        REFLECT_CLASS(RenderTarget);
    public:
        RenderTarget();

        ~RenderTarget() noexcept;

        void setName(HString name) noexcept;

        HString getName() const noexcept
        {
            return mName;
        }

        void setSize(uint32_t width, uint32_t height) noexcept;

        void setFormat(PixelFormat format) noexcept;

        void apply();

    private:
        void createTextureProxy(TextureProxyHandle handle) override;

    private:
        HString mName;
        bool mDirty = false;
    };

    template <> Type* Reflection::createType<RenderTarget>();
}
