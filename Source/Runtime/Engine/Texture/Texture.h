#pragma once
#include "SampleState.h"

#include <RenderBase/ProxyHandle.h>

#include <Core/Math/Vector.h>
#include <Core/Object/Object.h>
#include <Core/Common/BulkData.h>
#include <Core/Common/Hash.h>
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

    uint32_t getPixelSizeByFormat(PixelFormat format) noexcept;

    bool pixelFormatHasAlpha(PixelFormat format) noexcept;

    class ITextureListener
    {
    public:
        // 纹理的 GPU 资源被重建之后调用
        virtual void onTextureChanged() = 0;

    protected:
        ~ITextureListener() = default;
    };

    class Texture : public Object
    {
        REFLECT_CLASS(Texture)
    public:
        Texture() noexcept;

        ~Texture() noexcept;

        struct Description
        {
            PixelFormat format = PixelFormat::Undefined;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t depth = 1;
            uint32_t mipLevels = 1;
            uint32_t arrayLayers = 1;
            bool isCube = false;

            bool operator==(const Description& other) const = default;
        };

        const Description& getDescription() const noexcept
        {
            return mDescription;
        }

        bool isSRGB() const noexcept
        {
            return mIsSRGB;
        }

        CompressionPreset getCompressionPreset() const noexcept
        {
            return mCompressionPreset;
        }

        CompressionFormatFamily getCompressionFormatFamily() const noexcept
        {
            return mCompressionFormatFamily;
        }

        bool getFlipGreenChannel() const noexcept
        {
            return mFlipGreenChannel;
        }

        void setSRGB(bool isSRGB) noexcept;

        void setCompressionPreset(CompressionPreset compressionPreset) noexcept;

        void setCompressionFormatFamily(CompressionFormatFamily compressionFormatFamily) noexcept;

        void setFlipGreenChannel(bool flipGreenChannel) noexcept;

        rhi::TextureFormat getRHITextureFormat() const noexcept;

        // 纯设置——只存数据、算内容哈希(供派生数据缓存查找用), 不触发任何 mip 生成
        void setSourcePixels(std::vector<uint8_t>&& sourcePixels) noexcept;

        // 调用方已经算好完整 mip chain 时直接设置, 不经过 ITextureUtility, 不碰 mSourcePixels,
        // 在 Shipping 构建里也能跑。目前唯一的调用方是 SkyLight 的镜面反射 IBL 捕获:
        // 那份数据是 GPU compute pass 按粗糙度分级卷积出来的, 不是"从 mip0 降采样"能生成的东西
        void setMips(std::vector<uint8_t> mipChainData) noexcept;

        // getOrCreateTextureProxy() 首次创建 proxy 时会自动调一次, 不需要在那之前手动调用；
        // 只有 setSourcePixels() 改了已经在用的纹理内容、需要把新数据重新传上去时才需要手动调
        void uploadToGpu();

        void ensureMips();

        void releaseMips();

        void setSampleState(const SampleState& sampleState) noexcept;

        const SampleState& getSampleState() const noexcept
        {
            return mSampleState;
        }

        TextureProxyHandle getOrCreateTextureProxy();

        void addListener(ITextureListener* listener);

        void removeListener(ITextureListener* listener);

        // 临时确保 mSourcePixels 常驻并返回解压后的原始像素字节, 调用前不是常驻状态的话用完
        // 照原样还原(同 bakeMips() 的 wasLoaded 模式), 不会让原本没打开的纹理一直占着内存
        std::vector<uint8_t> copySourcePixels();

        // 从 mSourcePixels 调 ITextureUtility 生成 mMips, 需要时会自行 load()/unload()。调用方:
        // 1. 编辑器创建新纹理资产、或者改了 Description 需要重新烘焙时(setSourcePixels()/
        //    setDescription() 之后紧接着调一次);
        // 2. ensureMips() 里查派生数据缓存 miss 的时候。
        // setSourcePixels()/setDescription() 都是纯设置, 不会自动触发这个
        void bakeMips();

        // 纯设置, 不触发 bakeMips()——改完调用方自己再调一次 bakeMips() 从已有的 mSourcePixels 重新烘焙
        void setDescription(const Description& description) noexcept;

        // ITextureUtility(压缩+mip 生成的具体实现)按 Description 反推目标 GPU 格式时也要用到,
        // 所以是 public, 不是只给子类用的 protected
        static rhi::TextureFormat convertToRHITextureFormat(
            PixelFormat format,
            CompressionPreset compressionPreset,
            CompressionFormatFamily compressionFormatFamily,
            bool isSRGB
        ) noexcept;

    protected:
        virtual void createTextureProxy(TextureProxyHandle handle) = 0;

        void destroyTextureProxy(TextureProxyHandle handle) noexcept;

        // requestedMipLevels == 0 表示完整 mip 链, 否则夹到完整链长度以内
        static uint32_t computeMipLevels(uint32_t maxExtent, uint32_t requestedMipLevels) noexcept;

        TextureProxyHandle getTextureProxyHandle() const noexcept
        {
            return mProxyHandle;
        }

    private:
        // mSourcePixelsHash + mDescription 逐字段 combine, 派生数据缓存的查找/存储 key
        uint128_t computeMipCacheKey() const noexcept;

        // 缓存文件里的组帧格式: [u32 mipCount][对每一级: u32 size, size 个字节]
        std::vector<uint8_t> packMips() const;

        void unpackMips(std::span<const uint8_t> blob);

        void invalidateDerivedData(bool recreateProxy) noexcept;

    private:
        Description mDescription;
        bool mIsSRGB = false;
        CompressionPreset mCompressionPreset = CompressionPreset::None;
        CompressionFormatFamily mCompressionFormatFamily = CompressionFormatFamily::BC;
        bool mFlipGreenChannel = false;
        SampleState mSampleState;

        BulkData mSourcePixels;
        uint128_t mSourcePixelsHash;
        struct Mip
        {
            uint32_t level;
            BulkData data;
        };
        std::vector<Mip> mMips;

        TextureProxyHandle mProxyHandle;
        std::vector<ITextureListener*> mListeners;
    };

    template <> Type* Reflection::createType<Texture::Description>();

    template <> Type* Reflection::createType<Texture::Mip>();

    template <> Type* Reflection::createType<Texture>();
}
