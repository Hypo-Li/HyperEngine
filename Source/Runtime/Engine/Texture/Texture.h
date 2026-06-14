#pragma once
#include "SampleState.h"

#include <Core/Math/Vector.h>
#include <Core/Object/Object.h>
#include <RenderBase/TextureTypes.h>

namespace hyp
{
    enum class CompressionPreset : uint8_t
    {
        None,
        Default,
        High_Quality,
        Normal_Map,
        Grayscale,
        HDR,
    };

    class Texture : public Object
    {
        REFLECT_CLASS(Texture)
    public:
        Texture() noexcept;

        ~Texture() noexcept;

        struct Description
        {
            PixelFormat format;
            bool isSRGB;
            CompressionPreset compressionPreset;
            bool generateMips;
            uint32_t width;
            uint32_t height;
            uint32_t depth;
            uint32_t mipLevels;
            uint32_t arrayLayers;
        };

        const Description& getDescription() const noexcept
        {
            return mDescription;
        }

        void setSourcePixels(std::vector<uint8_t>&& sourcePixels) noexcept;

        void uploadToGpu();

        void setSampleState(const SampleState& sampleState) noexcept;

        const SampleState& getSampleState() const noexcept
        {
            return mSampleState;
        }

        TextureProxyHandle getOrCreateTextureProxy();

    protected:
        void setDescription(const Description& description) noexcept;

        virtual void createTextureProxy(TextureProxyHandle handle) = 0;

        void destroyTextureProxy(TextureProxyHandle handle) noexcept;

        TextureProxyHandle getTextureProxyHandle() const noexcept
        {
            return mProxyHandle;
        }

        static std::vector<uint8_t> generateMipmaps(std::span<const uint8_t> mip0Data, const Description& description);

        static uint32_t convertToVkFormat(PixelFormat format, CompressionPreset compressionPreset, bool isSRGB) noexcept;

    private:
        Description mDescription;
        SampleState mSampleState;

        std::vector<uint8_t> mSourcePixels;

        struct Mip
        {
            uint32_t level;
            std::vector<uint8_t> data;
        };
        std::vector<Mip> mMips;

        TextureProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<Texture::Description>();

    template <> Type* Reflection::createType<Texture>();
}
