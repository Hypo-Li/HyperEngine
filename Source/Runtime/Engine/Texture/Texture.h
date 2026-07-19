#pragma once
#include "SampleState.h"

#include <Core/Math/Vector.h>
#include <Core/Object/Object.h>
#include <Core/Common/BulkData.h>
#include <RHI/Texture.h>

namespace hyp
{
    enum class PixelFormat
    {
        Undefined,

        R8_UNORM,
        RG8_UNORM,
        RGB8_UNORM,
        RGBA8_UNORM,

        R16_UNORM,
        RG16_UNORM,
        RGB16_UNORM,
        RGBA16_UNORM,

        R16_SFLOAT,
        RG16_SFLOAT,
        RGB16_SFLOAT,
        RGBA16_SFLOAT,

        R32_SFLOAT,
        RG32_SFLOAT,
        RGB32_SFLOAT,
        RGBA32_SFLOAT,
    };

    template <> Type* Reflection::createType<PixelFormat>();

    enum class CompressionPreset : uint8_t
    {
        None,
        Default,
        High_Quality,
        Normal_Map,
        Grayscale,
        HDR,
    };

    template <> Type* Reflection::createType<CompressionPreset>();

    // CompressionPreset 描述"压缩的语义意图"(默认/高质量/法线贴图/灰度/HDR),
    // CompressionFormatFamily 描述"用哪一族具体的压缩纹理格式去实现这个意图"——
    // 两者结合才能推出最终的 rhi::TextureFormat。拆开是为了让 mMips 里实际烘焙出来的格式族
    // 可以脱离 rhi::TextureFormat 的具体细节单独存储/展示,以后针对不同目标平台
    // (比如移动端的 ASTC/ETC2)只需要在这个维度上扩展。目前只实现了 BC。
    enum class CompressionFormatFamily : uint8_t
    {
        None,
        BC,
    };

    template <> Type* Reflection::createType<CompressionFormatFamily>();

    class Texture : public Object
    {
        REFLECT_CLASS(Texture)
    public:
        Texture() noexcept;

        ~Texture() noexcept;

        struct Description
        {
            PixelFormat format = PixelFormat::Undefined;
            bool isSRGB = false;
            CompressionPreset compressionPreset = CompressionPreset::None;
            // 实际烘焙 mMips 时用的压缩格式族——setDescription() 靠它和 compressionPreset 一起
            // 判断要不要重新烘焙,详见 setDescription() 的实现注释
            CompressionFormatFamily compressionFormatFamily = CompressionFormatFamily::BC;
            bool generateMips = false;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t depth = 1;
            uint32_t mipLevels = 1;
            uint32_t arrayLayers = 1;
            bool isCube = false;
        };

        const Description& getDescription() const noexcept
        {
            return mDescription;
        }

        void setSourcePixels(std::vector<uint8_t>&& sourcePixels) noexcept;

        void setSourcePixels(std::span<const uint8_t> sourcePixels) noexcept;

        // getOrCreateTextureProxy() 首次创建 proxy 时会自动调一次, 不需要在那之前手动调用；
        // 只有 setSourcePixels() 改了已经在用的纹理内容、需要把新数据重新传上去时才需要手动调
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

        static rhi::TextureFormat convertToRHITextureFormat(
            PixelFormat format,
            CompressionPreset compressionPreset,
            CompressionFormatFamily compressionFormatFamily,
            bool isSRGB
        ) noexcept;

    private:
        // 从 mSourcePixels(要求当前已 Loaded)重新生成 mMips;setSourcePixels() 和
        // setDescription() 的重新烘焙路径共用这一份逻辑
        void bakeMips();

        // uploadToGpu() 和 getOrCreateTextureProxy() 首次创建 proxy 时都要把 mMips 传上去,
        // 共用这一份实现——proxyHandle 由调用方传入, 避免 getOrCreateTextureProxy() 内部
        // 调它时又反过来触发一次 getOrCreateTextureProxy() 的多余判断
        void uploadToGpuInternal(TextureProxyHandle proxyHandle);

    private:
        Description mDescription;
        SampleState mSampleState;

        BulkData mSourcePixels;
        struct Mip
        {
            uint32_t level;
            BulkData data;
        };
        std::vector<Mip> mMips;

        TextureProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<Texture::Description>();

    template <> Type* Reflection::createType<Texture::Mip>();

    template <> Type* Reflection::createType<Texture>();
}
