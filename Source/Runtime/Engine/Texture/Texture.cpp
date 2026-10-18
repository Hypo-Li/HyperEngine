#include "Texture.h"
#include "../Context.h"
#include "TextureUtility.h"

#include <RenderBase/RenderFrontend.h>
#include <RenderBase/TextureProxy.h>

#include <Core/Common/Compressor.h>
#include <Core/Common/DerivedDataCache.h>
#include <Core/Common/Hash.h>
#include <Core/Common/ModuleServiceRegistry.h>
#include <Platform/Platform.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hyp
{
    template <> Type* Reflection::createType<PixelFormat>()
    {
        auto e = new TEnumeration<PixelFormat>("PixelFormat");
        e->addValue("Undefined", PixelFormat::Undefined);

        e->addValue("R8_UNORM", PixelFormat::R8_UNORM);
        e->addValue("RG8_UNORM", PixelFormat::RG8_UNORM);
        e->addValue("RGB8_UNORM", PixelFormat::RGB8_UNORM);
        e->addValue("RGBA8_UNORM", PixelFormat::RGBA8_UNORM);

        e->addValue("R16_UNORM", PixelFormat::R16_UNORM);
        e->addValue("RG16_UNORM", PixelFormat::RG16_UNORM);
        e->addValue("RGB16_UNORM", PixelFormat::RGB16_UNORM);
        e->addValue("RGBA16_UNORM", PixelFormat::RGBA16_UNORM);

        e->addValue("R16_SFLOAT", PixelFormat::R16_SFLOAT);
        e->addValue("RG16_SFLOAT", PixelFormat::RG16_SFLOAT);
        e->addValue("RGB16_SFLOAT", PixelFormat::RGB16_SFLOAT);
        e->addValue("RGBA16_SFLOAT", PixelFormat::RGBA16_SFLOAT);

        e->addValue("R32_SFLOAT", PixelFormat::R32_SFLOAT);
        e->addValue("RG32_SFLOAT", PixelFormat::RG32_SFLOAT);
        e->addValue("RGB32_SFLOAT", PixelFormat::RGB32_SFLOAT);
        e->addValue("RGBA32_SFLOAT", PixelFormat::RGBA32_SFLOAT);

        return e;
    }

    template <> Type* Reflection::createType<CompressionPreset>()
    {
        auto e = new TEnumeration<CompressionPreset>("CompressionPreset");
        e->addValue("None", CompressionPreset::None);
        e->addValue("Default", CompressionPreset::Default);
        e->addValue("High Quality", CompressionPreset::High_Quality);
        e->addValue("Normal Map", CompressionPreset::Normal_Map);
        e->addValue("Grayscale", CompressionPreset::Grayscale);
        e->addValue("HDR", CompressionPreset::HDR);
        return e;
    }

    template <> Type* Reflection::createType<CompressionFormatFamily>()
    {
        auto e = new TEnumeration<CompressionFormatFamily>("CompressionFormatFamily");
        e->addValue("None", CompressionFormatFamily::None);
        e->addValue("BC", CompressionFormatFamily::BC);
        return e;
    }

    uint32_t getPixelSizeByFormat(PixelFormat format) noexcept
    {
        switch (format)
        {
        case PixelFormat::R8_UNORM: return 1;
        case PixelFormat::RG8_UNORM: return 2;
        case PixelFormat::RGB8_UNORM: return 3;
        case PixelFormat::RGBA8_UNORM: return 4;
        case PixelFormat::R16_UNORM: return 2;
        case PixelFormat::RG16_UNORM: return 4;
        case PixelFormat::RGB16_UNORM: return 6;
        case PixelFormat::RGBA16_UNORM: return 8;
        case PixelFormat::R16_SFLOAT: return 2;
        case PixelFormat::RG16_SFLOAT: return 4;
        case PixelFormat::RGB16_SFLOAT: return 6;
        case PixelFormat::RGBA16_SFLOAT: return 8;
        case PixelFormat::R32_SFLOAT: return 4;
        case PixelFormat::RG32_SFLOAT: return 8;
        case PixelFormat::RGB32_SFLOAT: return 12;
        case PixelFormat::RGBA32_SFLOAT: return 16;
        default: return 0;
        }
    }

    bool pixelFormatHasAlpha(PixelFormat format) noexcept
    {
        if (format == PixelFormat::RGBA8_UNORM ||
            format == PixelFormat::RGBA16_UNORM ||
            format == PixelFormat::RGBA16_SFLOAT ||
            format == PixelFormat::RGBA32_SFLOAT)
        {
            return true;
        }
        return false;
    }

    REGISTER_CLASS(Texture)

    Texture::Texture() noexcept
    {

    }

    Texture::~Texture() noexcept
    {
        if (mProxyHandle.isValid())
        {
            destroyTextureProxy(mProxyHandle);
            Context::get().getRenderFrontend().freeProxyHandle(mProxyHandle);
            mProxyHandle.reset();
        }
    }

    void Texture::ensureMips()
    {
        if (!mMips.empty())
        {
            // cooked 资产: Mips 带着真实的磁盘来源反序列化出来了, 直接读回
            for (Mip& mip : mMips)
                mip.data.load();
            return;
        }

        if (mSourcePixels.getSize() == 0)
            return; // 没有源数据(比如资产还没导入过), 没法生成

        assert(mSourcePixelsHash != uint128_t{} && "有源像素就一定有哈希, 见 setSourcePixels()");

        DerivedDataCache mipCache(Platform::getExecutableDir() / "Cache", "Textures");
        std::optional<std::vector<uint8_t>> cached = mipCache.tryLoad(computeMipCacheKey());

        if (cached)
        {
            unpackMips(*cached);
        }
        else
        {
            bakeMips(); // 缓存未命中时才真的重新烘焙——写回缓存的步骤在 bakeMips() 内部完成
        }
    }

    void Texture::releaseMips()
    {
        // 有源像素才能靠 DDC/重新烘焙把 mips 拿回来, 否则只能 unload, 保住 cooked 资产里那份磁盘来源
        if (mSourcePixels.getSize() != 0)
        {
            mMips.clear();
        }
        else
        {
            for (Mip& mip : mMips)
                mip.data.unload();
        }
    }

    void Texture::setSourcePixels(std::vector<uint8_t>&& sourcePixels) noexcept
    {
        // Source Pixels 只放 mip0 原始像素——其余各级 mip 一律由 ITextureUtility 生成,
        // 不再支持调用方自己拼好完整 mip chain 塞进来(那种场景改用 setMips())。
        // 纯设置: 只存数据、算内容哈希, 不触发生成——生成由上层显式触发
        size_t expectedSize = static_cast<size_t>(mDescription.width) *
            mDescription.height *
            mDescription.depth *
            getPixelSizeByFormat(mDescription.format) *
            mDescription.arrayLayers;

        assert(sourcePixels.size() == expectedSize);

        // 哈希算的是原始像素、缓存 key 才能跟压缩算法无关;实际存进 mSourcePixels 的是压缩后的数据,
        // 换来的磁盘/内存占用收益值得多付一次导入时的压缩开销(解压在 bakeMips() 里做,足够快)
        mSourcePixelsHash = Hash::compute(sourcePixels.data(), sourcePixels.size(), Hash::Algorithm128::xxHash);
        mSourcePixels.setData(Compressor::compress(sourcePixels));
    }

    void Texture::setMips(std::vector<uint8_t> mipChainData) noexcept
    {
        mMips.clear();
        mMips.reserve(mDescription.mipLevels);

        uint32_t pixelSize = getPixelSizeByFormat(mDescription.format);
        uint32_t mipWidth = mDescription.width;
        uint32_t mipHeight = mDescription.height;
        uint32_t mipDepth = mDescription.depth;

        size_t offset = 0;
        for (uint32_t i = 0; i < mDescription.mipLevels; ++i)
        {
            size_t mipSize = static_cast<size_t>(mipWidth) * mipHeight * mipDepth * pixelSize * mDescription.arrayLayers;
            assert(offset + mipSize <= mipChainData.size());

            Mip& mip = mMips.emplace_back();
            mip.level = i;
            mip.data.setData(std::vector<uint8_t>(mipChainData.begin() + offset, mipChainData.begin() + offset + mipSize));
            offset += mipSize;

            mipWidth = std::max(mipWidth >> 1, 1u);
            mipHeight = std::max(mipHeight >> 1, 1u);
            mipDepth = std::max(mipDepth >> 1, 1u);
        }
    }

    std::vector<uint8_t> Texture::copySourcePixels()
    {
        bool wasLoaded = mSourcePixels.isLoaded();
        if (!wasLoaded)
            mSourcePixels.load();

        std::vector<uint8_t> pixels = Compressor::decompress(mSourcePixels.getData());

        if (!wasLoaded)
            mSourcePixels.unload();

        return pixels;
    }

    void Texture::bakeMips()
    {
        bool wasLoaded = mSourcePixels.isLoaded();
        if (!wasLoaded)
            mSourcePixels.load();

        std::vector<uint8_t> sourcePixels = Compressor::decompress(mSourcePixels.getData());

        mMips.clear();

        ITextureUtility* textureUtility = ModuleServiceRegistry::get().getService<ITextureUtility>();
        if (textureUtility)
        {
            std::vector<ITextureUtility::BakedMip> bakedMips = textureUtility->bakeMips(sourcePixels, *this);
            mMips.reserve(bakedMips.size());
            for (auto& bakedMip : bakedMips)
            {
                Mip& mip = mMips.emplace_back();
                mip.level = bakedMip.level;
                mip.data.setData(std::move(bakedMip.data));
            }

            // mMips 是 Cook_Only 属性, 编辑器资产从不落盘——每次调用方(导入/postLoad() 缓存 miss)
            // 都要重新烘焙, 这里写回一份供下次 computeMipCacheKey() 命中, 不然每次都要重新压缩
            if (!mMips.empty())
            {
                DerivedDataCache mipCache(Platform::getExecutableDir() / "Cache", "Textures");
                mipCache.save(computeMipCacheKey(), packMips());
            }
        }

        if (!wasLoaded)
            mSourcePixels.unload();
    }

    uint128_t Texture::computeMipCacheKey() const noexcept
    {
        uint128_t key = mSourcePixelsHash;
        Hash::combine(key, mDescription.format);
        Hash::combine(key, mIsSRGB);
        Hash::combine(key, mCompressionPreset);
        Hash::combine(key, mCompressionFormatFamily);
        Hash::combine(key, mFlipGreenChannel);
        Hash::combine(key, mDescription.width);
        Hash::combine(key, mDescription.height);
        Hash::combine(key, mDescription.depth);
        Hash::combine(key, mDescription.mipLevels);
        Hash::combine(key, mDescription.arrayLayers);
        Hash::combine(key, mDescription.isCube);
        return key;
    }

    std::vector<uint8_t> Texture::packMips() const
    {
        std::vector<uint8_t> blob;

        uint32_t mipCount = static_cast<uint32_t>(mMips.size());
        const uint8_t* mipCountBytes = reinterpret_cast<const uint8_t*>(&mipCount);
        blob.insert(blob.end(), mipCountBytes, mipCountBytes + sizeof(mipCount));

        for (const Mip& mip : mMips)
        {
            assert(mip.data.isLoaded());
            std::span<const uint8_t> data = mip.data.getData();

            uint32_t size = static_cast<uint32_t>(data.size());
            const uint8_t* sizeBytes = reinterpret_cast<const uint8_t*>(&size);
            blob.insert(blob.end(), sizeBytes, sizeBytes + sizeof(size));
            blob.insert(blob.end(), data.begin(), data.end());
        }

        return blob;
    }

    void Texture::unpackMips(std::span<const uint8_t> blob)
    {
        mMips.clear();

        size_t offset = 0;
        if (offset + sizeof(uint32_t) > blob.size())
            return;
        uint32_t mipCount;
        std::memcpy(&mipCount, blob.data() + offset, sizeof(mipCount));
        offset += sizeof(mipCount);

        mMips.reserve(mipCount);
        for (uint32_t i = 0; i < mipCount; ++i)
        {
            if (offset + sizeof(uint32_t) > blob.size())
            {
                mMips.clear();
                return;
            }
            uint32_t size;
            std::memcpy(&size, blob.data() + offset, sizeof(size));
            offset += sizeof(size);

            if (offset + size > blob.size())
            {
                mMips.clear();
                return;
            }

            Mip& mip = mMips.emplace_back();
            mip.level = i;
            mip.data.setData(std::vector<uint8_t>(blob.begin() + offset, blob.begin() + offset + size));
            offset += size;
        }
    }

    void Texture::setDescription(const Description& description) noexcept
    {
        // 任何字段变化都会让已有 mMips 的字节布局或格式解读对不上新 Description, 无条件清空——
        // 不清空的话, mDescription 和 mMips 会有一段互不匹配的窗口期, uploadToGpu() 一旦
        // 被调就会拿新格式去传旧字节。清空之后同样场景下靠 uploadToGpu() 的
        // `if (mMips.empty()) return;` 短路, 不会传错东西——重新生成 mMips 由上层显式触发, 这里不做
        bool changed = mDescription != description;

        mDescription = description;

        if (changed)
            invalidateDerivedData(true);
    }

    void Texture::setSRGB(bool isSRGB) noexcept
    {
        if (mIsSRGB == isSRGB)
            return;
        mIsSRGB = isSRGB;
        invalidateDerivedData(true);
    }

    void Texture::setCompressionPreset(CompressionPreset compressionPreset) noexcept
    {
        if (mCompressionPreset == compressionPreset)
            return;
        mCompressionPreset = compressionPreset;
        invalidateDerivedData(true);
    }

    void Texture::setCompressionFormatFamily(CompressionFormatFamily compressionFormatFamily) noexcept
    {
        if (mCompressionFormatFamily == compressionFormatFamily)
            return;
        mCompressionFormatFamily = compressionFormatFamily;
        invalidateDerivedData(true);
    }

    void Texture::setFlipGreenChannel(bool flipGreenChannel) noexcept
    {
        if (mFlipGreenChannel == flipGreenChannel)
            return;
        mFlipGreenChannel = flipGreenChannel;
        invalidateDerivedData(false);
    }

    rhi::TextureFormat Texture::getRHITextureFormat() const noexcept
    {
        return convertToRHITextureFormat(mDescription.format, mCompressionPreset, mCompressionFormatFamily, mIsSRGB);
    }

    uint32_t Texture::computeMipLevels(uint32_t maxExtent, uint32_t requestedMipLevels) noexcept
    {
        uint32_t fullMipLevels = static_cast<uint32_t>(std::floor(std::log2(maxExtent))) + 1;
        return requestedMipLevels == 0 ? fullMipLevels : std::min(requestedMipLevels, fullMipLevels);
    }

    void Texture::invalidateDerivedData(bool recreateProxy) noexcept
    {
        mMips.clear();

        // createTextureProxy() 建出的 GPU 纹理对象格式/尺寸是创建时定死的, 不会随 mDescription 变化自动更新
        if (recreateProxy && mProxyHandle.isValid())
        {
            destroyTextureProxy(mProxyHandle);
            createTextureProxy(mProxyHandle);
            for (ITextureListener* listener : mListeners)
                listener->onTextureChanged();
        }
    }

    void Texture::addListener(ITextureListener* listener)
    {
        assert(std::find(mListeners.begin(), mListeners.end(), listener) == mListeners.end());
        mListeners.push_back(listener);
    }

    void Texture::removeListener(ITextureListener* listener)
    {
        auto it = std::find(mListeners.begin(), mListeners.end(), listener);
        assert(it != mListeners.end());
        mListeners.erase(it);
    }

    void Texture::uploadToGpu()
    {
        if (!mProxyHandle.isValid())
            return;

        ensureMips();

        struct UploadMip
        {
            uint32_t level;
            std::vector<uint8_t> data;
        };
        std::vector<UploadMip> uploadMips;
        uploadMips.reserve(mMips.size());
        for (Mip& mip : mMips)
        {
            std::vector<uint8_t> data = mip.data.takeData();
            if (!data.empty())
                uploadMips.push_back(UploadMip{ mip.level, std::move(data) });
        }

        releaseMips();

        if (uploadMips.empty())
            return;

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle,
            mips = std::move(uploadMips),
            layerCount = mDescription.arrayLayers](FrameContext& context) mutable
            {
                TextureProxy* proxy = context.getProxyManager().getProxy<ProxyType::Texture>(proxyHandle);
                for (auto& mip : mips)
                {
                    proxy->stageUpload(std::move(mip.data), mip.level, 0, layerCount);
                }
            }
        );
    }

    void Texture::setSampleState(const SampleState& sampleState) noexcept
    {
        mSampleState = sampleState;
    }

    TextureProxyHandle Texture::getOrCreateTextureProxy()
    {
        if (!mProxyHandle.isValid())
        {
            auto& renderFrontend = Context::get().getRenderFrontend();
            mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Texture>();
            createTextureProxy(mProxyHandle);
            // createTextureProxy() 只建 GPU 纹理对象、不写入像素数据
            uploadToGpu();
        }
        return mProxyHandle;
    }

    void Texture::destroyTextureProxy(TextureProxyHandle handle) noexcept
    {
        assert(handle.isValid());

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [handle](FrameContext& context)
            {
                context.getProxyManager().destroyProxy(handle);
            }
        );
    }

    rhi::TextureFormat Texture::convertToRHITextureFormat(
        PixelFormat format,
        CompressionPreset compressionPreset,
        CompressionFormatFamily compressionFormatFamily,
        bool isSRGB
    ) noexcept
    {
        // 目前只实现了 BC 这一个压缩格式族;以后要支持 ASTC/ETC2 之类移动端格式族时,
        // 在下面每个"确实要压缩"的分支里按 compressionFormatFamily 再拆一层即可
        assert(compressionFormatFamily == CompressionFormatFamily::BC);

        const bool is8BitUnorm = format == PixelFormat::R8_UNORM ||
            format == PixelFormat::RG8_UNORM ||
            format == PixelFormat::RGB8_UNORM ||
            format == PixelFormat::RGBA8_UNORM;
        const bool is16BitSfloat = format == PixelFormat::R16_SFLOAT ||
            format == PixelFormat::RG16_SFLOAT ||
            format == PixelFormat::RGB16_SFLOAT ||
            format == PixelFormat::RGBA16_SFLOAT;
        if (compressionPreset == CompressionPreset::Default && is8BitUnorm)
        {
            return pixelFormatHasAlpha(format) ?
                (isSRGB ? rhi::TextureFormat::BC3_SRGB : rhi::TextureFormat::BC3_UNORM) :
                (isSRGB ? rhi::TextureFormat::BC1_RGB_SRGB : rhi::TextureFormat::BC1_RGB_UNORM);
        }
        else if (compressionPreset == CompressionPreset::High_Quality && is8BitUnorm)
        {
            return isSRGB ? rhi::TextureFormat::BC7_SRGB : rhi::TextureFormat::BC7_UNORM;
        }
        else if (compressionPreset == CompressionPreset::Normal_Map && is8BitUnorm)
        {
            return rhi::TextureFormat::BC5_UNORM;
        }
        else if (compressionPreset == CompressionPreset::Grayscale && is8BitUnorm)
        {
            return rhi::TextureFormat::BC4_UNORM;
        }
        else if (compressionPreset == CompressionPreset::HDR && is16BitSfloat)
        {
            return rhi::TextureFormat::BC6H_UFLOAT;
        }
        else
        {
            switch (format)
            {
            case PixelFormat::R8_UNORM: return isSRGB ? rhi::TextureFormat::R8_SRGB : rhi::TextureFormat::R8_UNORM;
            case PixelFormat::RG8_UNORM: return isSRGB ? rhi::TextureFormat::RG8_SRGB : rhi::TextureFormat::RG8_UNORM;
            case PixelFormat::RGB8_UNORM: return isSRGB ? rhi::TextureFormat::RGBA8_SRGB : rhi::TextureFormat::RGBA8_UNORM;
            case PixelFormat::RGBA8_UNORM: return isSRGB ? rhi::TextureFormat::RGBA8_SRGB : rhi::TextureFormat::RGBA8_UNORM;
            case PixelFormat::R16_UNORM: return rhi::TextureFormat::R16_UNORM;
            case PixelFormat::RG16_UNORM: return rhi::TextureFormat::RG16_UNORM;
            case PixelFormat::RGB16_UNORM: return rhi::TextureFormat::RGBA16_UNORM;
            case PixelFormat::RGBA16_UNORM: return rhi::TextureFormat::RGBA16_UNORM;
            case PixelFormat::R16_SFLOAT: return rhi::TextureFormat::R16_SFLOAT;
            case PixelFormat::RG16_SFLOAT: return rhi::TextureFormat::RG16_SFLOAT;
            case PixelFormat::RGB16_SFLOAT: return rhi::TextureFormat::RGBA16_SFLOAT;
            case PixelFormat::RGBA16_SFLOAT: return rhi::TextureFormat::RGBA16_SFLOAT;
            case PixelFormat::R32_SFLOAT: return rhi::TextureFormat::R32_SFLOAT;
            case PixelFormat::RG32_SFLOAT: return rhi::TextureFormat::RG32_SFLOAT;
            case PixelFormat::RGB32_SFLOAT: return rhi::TextureFormat::RGBA32_SFLOAT;
            case PixelFormat::RGBA32_SFLOAT: return rhi::TextureFormat::RGBA32_SFLOAT;
            default: return rhi::TextureFormat::Undefined;
            }
        }
    }

    template <> Type* Reflection::createType<Texture::Description>()
    {
        auto s = new TStructure<Texture::Description>("Texture.Description");
        s->addProperty("Format", &Texture::Description::format);
        s->addProperty("Width", &Texture::Description::width);
        s->addProperty("Height", &Texture::Description::height);
        s->addProperty("Depth", &Texture::Description::depth);
        s->addProperty("Mip Levels", &Texture::Description::mipLevels);
        s->addProperty("Array Layers", &Texture::Description::arrayLayers);
        s->addProperty("Is Cube", &Texture::Description::isCube);
        return s;
    }

    template <> Type* Reflection::createType<Texture::Mip>()
    {
        auto s = new TStructure<Texture::Mip>("Texture.Mip");
        s->addProperty("Level", &Texture::Mip::level);
        s->addProperty("Data", &Texture::Mip::data);
        return s;
    }

    template <> Type* Reflection::createType<Texture>()
    {
        auto c = new TClass<Texture, Object>("Texture");
        c->addProperty("Description", &Texture::mDescription);
        c->addProperty("Is SRGB", &Texture::mIsSRGB);
        c->addProperty("Compression Preset", &Texture::mCompressionPreset);
        c->addProperty("Compression Format Family", &Texture::mCompressionFormatFamily);
        c->addProperty("Flip Green Channel", &Texture::mFlipGreenChannel);
        c->addProperty("Sample State", &Texture::mSampleState);
        c->addProperty("Source Pixels", &Texture::mSourcePixels, CopyPolicy::Shallow, PropertyFlags::Editor_Only);
        c->addProperty("Source Pixels Hash", &Texture::mSourcePixelsHash, CopyPolicy::Shallow, PropertyFlags::Editor_Only);
        c->addProperty("Mips", &Texture::mMips, CopyPolicy::Shallow, PropertyFlags::Cook_Only);
        return c;
    }
}
