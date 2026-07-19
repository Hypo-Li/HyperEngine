#include "Texture.h"
#include "../Context.h"
#include "Mipmap.h"

#include <RenderBase/RenderSystem.h>
#include <RenderBase/TextureProxy.h>

#include <ispc_texcomp.h>

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

    static bool isCompressionFormat(rhi::TextureFormat format) noexcept
    {
        switch (format)
        {
        case rhi::TextureFormat::BC1_RGB_UNORM:
        case rhi::TextureFormat::BC1_RGB_SRGB:
        case rhi::TextureFormat::BC1_RGBA_UNORM:
        case rhi::TextureFormat::BC1_RGBA_SRGB:
        case rhi::TextureFormat::BC2_UNORM:
        case rhi::TextureFormat::BC2_SRGB:
        case rhi::TextureFormat::BC3_UNORM:
        case rhi::TextureFormat::BC3_SRGB:
        case rhi::TextureFormat::BC4_UNORM:
        case rhi::TextureFormat::BC4_SNORM:
        case rhi::TextureFormat::BC5_UNORM:
        case rhi::TextureFormat::BC5_SNORM:
        case rhi::TextureFormat::BC6H_UFLOAT:
        case rhi::TextureFormat::BC6H_SFLOAT:
        case rhi::TextureFormat::BC7_UNORM:
        case rhi::TextureFormat::BC7_SRGB:
            return true;
        default:
            return false;
        }
    }

    static uint32_t getBlockSizeByVkFormat(rhi::TextureFormat format) noexcept
    {
        switch (format)
        {
        case rhi::TextureFormat::BC1_RGB_UNORM:
        case rhi::TextureFormat::BC1_RGB_SRGB:
        case rhi::TextureFormat::BC1_RGBA_UNORM:
        case rhi::TextureFormat::BC1_RGBA_SRGB:
        case rhi::TextureFormat::BC4_UNORM:
        case rhi::TextureFormat::BC4_SNORM:
            return 8;
        case rhi::TextureFormat::BC2_UNORM:
        case rhi::TextureFormat::BC2_SRGB:
        case rhi::TextureFormat::BC3_UNORM:
        case rhi::TextureFormat::BC3_SRGB:
        case rhi::TextureFormat::BC5_UNORM:
        case rhi::TextureFormat::BC5_SNORM:
        case rhi::TextureFormat::BC6H_UFLOAT:
        case rhi::TextureFormat::BC6H_SFLOAT:
        case rhi::TextureFormat::BC7_UNORM:
        case rhi::TextureFormat::BC7_SRGB:
            return 16;
        default:
            return 0;
        }
    }

    static uint32_t getPixelSizeByFormat(PixelFormat format) noexcept
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

    static uint32_t getPixelFormatChannels(PixelFormat format) noexcept
    {
        switch (format)
        {
        case PixelFormat::R8_UNORM:
        case PixelFormat::R16_UNORM:
        case PixelFormat::R32_SFLOAT:
            return 1;
        case PixelFormat::RG8_UNORM:
        case PixelFormat::RG16_UNORM:
        case PixelFormat::RG16_SFLOAT:
        case PixelFormat::RG32_SFLOAT:
            return 2;
        case PixelFormat::RGB8_UNORM:
        case PixelFormat::RGB16_UNORM:
        case PixelFormat::RGB16_SFLOAT:
        case PixelFormat::RGB32_SFLOAT:
            return 3;
        case PixelFormat::RGBA8_UNORM:
        case PixelFormat::RGBA16_UNORM:
        case PixelFormat::RGBA16_SFLOAT:
        case PixelFormat::RGBA32_SFLOAT:
            return 4;
        default:
            return 0;
        }
    }

    static bool pixelFormatHasAlpha(PixelFormat format) noexcept
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

    // BC1/BC3/BC6H/BC7 吃标准 4 通道输入(LDR 32bpp/HDR 64bpp), 但 BC4/BC5 是例外——
    // ispc_texcomp 的文档(ispc_texcomp.h)明确写了 BC4 要求紧凑打包的 8bpp R8(1 通道),
    // BC5 要求紧凑打包的 16bpp RG8(2 通道), 不是"从 4 通道源数据里跳着只读 R/G"。
    // generatePaddingMipData() 按这里返回的目标通道数重新整理数据, compressTextureData()
    // 只管拿整理好的数据转发调用, 不做任何形状变换——和现有"数据整形在 bakeMips() 这一层
    // 通过 generatePaddingMipData() 完成, compressTextureData() 只是纯转发"的架构保持一致
    static uint32_t getCompressionInputChannels(rhi::TextureFormat format) noexcept
    {
        switch (format)
        {
        case rhi::TextureFormat::BC4_UNORM:
        case rhi::TextureFormat::BC4_SNORM:
            return 1;
        case rhi::TextureFormat::BC5_UNORM:
        case rhi::TextureFormat::BC5_SNORM:
            return 2;
        default:
            return 4;
        }
    }

    // 把源数据重新整理成宽高对齐到 4 的倍数、且通道数恰好是 dstChannels 的紧凑排列——
    // dstChannels 可能比源通道数多(比如 3 通道 RGB 要 pad 到 4 通道给 BC1/BC3/BC6H/BC7 用,
    // 多出来的通道补 0), 也可能比源通道数少(比如 4 通道 RGBA 要砍到 1/2 通道给 BC4/BC5 用,
    // 多余的通道直接丢弃)。边缘(宽高不是 4 的倍数的部分)按 clamp 补, 复制最后一行/列
    static std::vector<uint8_t> generatePaddingMipData(
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint32_t layers,
        uint32_t pixelSize, // 原始单个像素的总字节数
        uint32_t channels,  // 原始通道数
        uint32_t dstChannels, // 目标通道数, 由调用方按压缩格式实际需要的通道数决定
        std::span<const uint8_t> srcData)
    {
        uint32_t channelSize = pixelSize / channels;
        uint32_t dstPixelSize = dstChannels * channelSize;
        // 目标通道数比源通道数少时(BC4/BC5 的情形)只能拷贝 dstChannels 份, 多了会越界写
        uint32_t copySize = std::min(channels, dstChannels) * channelSize;

        uint32_t paddedWidth = (width + 3) & ~3;
        uint32_t paddedHeight = (height + 3) & ~3;

        // 分配内存并初始化为 0 (自动满足新通道填充 0 的要求)
        size_t paddedSliceSize = static_cast<size_t>(paddedWidth) * paddedHeight * dstPixelSize;
        std::vector<uint8_t> paddingPixels(paddedSliceSize * depth * layers, 0);

        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            for (uint32_t z = 0; z < depth; ++z)
            {
                uint8_t* dstSlice = paddingPixels.data() + (layer * depth + z) * paddedSliceSize;
                const uint8_t* srcSlice = srcData.data() + (layer * depth + z) * width * height * pixelSize;

                for (uint32_t y = 0; y < paddedHeight; ++y)
                {
                    // 垂直方向边缘处理：取原图最后一行
                    uint32_t srcY = std::min(y, height - 1);
                    const uint8_t* srcRow = srcSlice + srcY * width * pixelSize;
                    uint8_t* dstRow = dstSlice + y * paddedWidth * dstPixelSize;

                    for (uint32_t x = 0; x < paddedWidth; ++x)
                    {
                        // 水平方向边缘处理：取原图最后的一列像素
                        uint32_t srcX = std::min(x, width - 1);
                        const uint8_t* srcPixel = srcRow + srcX * pixelSize;
                        uint8_t* dstPixel = dstRow + x * dstPixelSize;

                        std::memcpy(dstPixel, srcPixel, copySize);
                    }
                }
            }
        }

        return paddingPixels;
    }

    static void compressTextureData(
        uint32_t width,
        uint32_t height,
        std::span<const uint8_t> srcData,
        std::span<uint8_t> dstData,
        rhi::TextureFormat compressionFormat,
        bool hasAlpha
    )
    {
        // srcData 已经由调用方(bakeMips(), 经 getCompressionInputChannels()/
        // generatePaddingMipData() 整理)按这个格式实际需要的通道数紧凑排列好了, 这里只管
        // 按格式转发调用, 不做任何数据整形
        rgba_surface surface;
        surface.width = width;
        surface.height = height;
        surface.ptr = const_cast<uint8_t*>(srcData.data());
        surface.stride = static_cast<int32_t>(srcData.size() / height);

        switch (compressionFormat)
        {
        case rhi::TextureFormat::BC1_RGB_UNORM:
        case rhi::TextureFormat::BC1_RGB_SRGB:
        case rhi::TextureFormat::BC1_RGBA_UNORM:
        case rhi::TextureFormat::BC1_RGBA_SRGB:
        {
            CompressBlocksBC1(&surface, dstData.data());
            break;
        }
        case rhi::TextureFormat::BC3_UNORM:
        case rhi::TextureFormat::BC3_SRGB:
        {
            CompressBlocksBC3(&surface, dstData.data());
            break;
        }
        case rhi::TextureFormat::BC4_UNORM:
        case rhi::TextureFormat::BC4_SNORM:
        {
            CompressBlocksBC4(&surface, dstData.data());
            break;
        }
        case rhi::TextureFormat::BC5_UNORM:
        case rhi::TextureFormat::BC5_SNORM:
        {
            CompressBlocksBC5(&surface, dstData.data());
            break;
        }
        case rhi::TextureFormat::BC6H_UFLOAT:
        case rhi::TextureFormat::BC6H_SFLOAT:
        {
            bc6h_enc_settings settings;
            GetProfile_bc6h_veryslow(&settings);
            CompressBlocksBC6H(&surface, dstData.data(), &settings);
            break;
        }
        case rhi::TextureFormat::BC7_UNORM:
        case rhi::TextureFormat::BC7_SRGB:
        {
            bc7_enc_settings settings;
            if (hasAlpha)
            {
                GetProfile_alpha_slow(&settings);
            }
            else
            {
                GetProfile_slow(&settings);
            }
            CompressBlocksBC7(&surface, dstData.data(), &settings);
            break;
        }
        default:
            break;
        }
    }

    void Texture::setSourcePixels(std::vector<uint8_t>&& sourcePixels) noexcept
    {
        uint32_t pixelSize = getPixelSizeByFormat(mDescription.format);
        uint32_t arrayLayers = mDescription.arrayLayers;
        size_t expectedSize = mDescription.width *
            mDescription.height *
            mDescription.depth *
            pixelSize *
            arrayLayers;

        size_t mipmapSize = 0;
        {
            uint32_t mipWidth = mDescription.width;
            uint32_t mipHeight = mDescription.height;
            uint32_t mipDepth = mDescription.depth;
            for (uint32_t i = 1; i < mDescription.mipLevels; ++i)
            {
                mipWidth = std::max(mipWidth >> 1, 1u);
                mipHeight = std::max(mipHeight >> 1, 1u);
                mipDepth = std::max(mipDepth >> 1, 1u);

                mipmapSize += mipWidth * mipHeight * mipDepth * pixelSize * arrayLayers;
            }
        }

        if (!mDescription.generateMips)
        {
            expectedSize += mipmapSize;
        }

        assert(sourcePixels.size() == expectedSize);

        mSourcePixels.setData(std::move(sourcePixels));

        bakeMips();

        // 烘焙完 mMips 之后 mSourcePixels 就不需要常驻内存了
        mSourcePixels.unload();
    }

    void Texture::setSourcePixels(std::span<const uint8_t> sourcePixels) noexcept
    {
        setSourcePixels(std::vector<uint8_t>(sourcePixels.begin(), sourcePixels.end()));
    }

    void Texture::bakeMips()
    {
        assert(mSourcePixels.isLoaded());
        std::span<const uint8_t> sourcePixels = mSourcePixels.getData();

        uint32_t pixelSize = getPixelSizeByFormat(mDescription.format);
        uint32_t arrayLayers = mDescription.arrayLayers;

        std::vector<uint8_t> mipmapPixels;
        if (mDescription.generateMips)
        {
            mipmapPixels = generateMipmaps(sourcePixels, mDescription);
        }

        mMips.clear();
        mMips.reserve(mDescription.mipLevels);
        uint32_t mipWidth = mDescription.width;
        uint32_t mipHeight = mDescription.height;
        uint32_t mipDepth = mDescription.depth;

        rhi::TextureFormat format = convertToRHITextureFormat(mDescription.format, mDescription.compressionPreset, mDescription.compressionFormatFamily, mDescription.isSRGB);
        bool compFormat = isCompressionFormat(format);
        uint32_t blockSize = getBlockSizeByVkFormat(format);
        uint32_t pixelChannels = getPixelFormatChannels(mDescription.format);
        bool hasAlpha = pixelFormatHasAlpha(mDescription.format);

        size_t srcMipOffset = 0;
        for (uint32_t i = 0; i < mDescription.mipLevels; ++i)
        {
            size_t srcMipSize = mipWidth * mipHeight * mipDepth * pixelSize * arrayLayers;
            size_t mipDataSize = 0;

            std::span<const uint8_t> srcMipData;
            if (i == 0)
            {
                srcMipData = std::span<const uint8_t>(sourcePixels.data(), srcMipSize);
                if (!mDescription.generateMips)
                    srcMipOffset += srcMipSize;
            }
            else
            {
                if (mDescription.generateMips)
                    srcMipData = std::span<const uint8_t>(mipmapPixels.data() + srcMipOffset, srcMipSize);
                else
                    srcMipData = std::span<const uint8_t>(sourcePixels.data() + srcMipOffset, srcMipSize);
                srcMipOffset += srcMipSize;
            }

            if (compFormat)
            {
                uint32_t paddedWidth = (mipWidth + 3) & ~3;
                uint32_t paddedHeight = (mipHeight + 3) & ~3;
                mipDataSize = (paddedWidth * paddedHeight) / 16 * blockSize * mipDepth * arrayLayers;
            }
            else
            {
                if (pixelChannels == 3)
                    mipDataSize = srcMipSize / 3 * 4;
                else
                    mipDataSize = srcMipSize;
            }

            std::vector<uint8_t> mipBytes(mipDataSize);

            if (compFormat)
            {
                uint32_t paddedWidth = (mipWidth + 3) & ~3;
                uint32_t paddedHeight = (mipHeight + 3) & ~3;
                uint32_t compressionChannels = getCompressionInputChannels(format);

                if (mipWidth != paddedWidth || mipHeight != paddedHeight || pixelChannels != compressionChannels)
                {
                    std::vector<uint8_t> paddingPixels = generatePaddingMipData(
                        mipWidth,
                        mipHeight,
                        mipDepth,
                        arrayLayers,
                        pixelSize,
                        pixelChannels,
                        compressionChannels,
                        srcMipData
                    );

                    size_t srcSliceSize = paddedWidth * paddedHeight * (pixelSize / pixelChannels) * compressionChannels;
                    size_t dstSliceSize = (paddedWidth * paddedHeight) / 16 * blockSize;
                    for (uint32_t layer = 0; layer < arrayLayers; ++layer)
                    {
                        for (uint32_t z = 0; z < mipDepth; ++z)
                        {
                            std::span<const uint8_t> srcSlice = { paddingPixels.data() + (layer * mipDepth + z) * srcSliceSize, srcSliceSize };
                            std::span<uint8_t> dstSlice = { mipBytes.data() + (layer * mipDepth + z) * dstSliceSize, dstSliceSize};

                            compressTextureData(
                                paddedWidth,
                                paddedHeight,
                                srcSlice,
                                dstSlice,
                                format,
                                hasAlpha
                            );
                        }
                    }
                }
                else
                {
                    size_t srcSliceSize = mipWidth * mipHeight * pixelSize;
                    size_t dstSliceSize = (mipWidth * mipHeight) / 16 * blockSize;
                    for (uint32_t layer = 0; layer < arrayLayers; ++layer)
                    {
                        for (uint32_t z = 0; z < mipDepth; ++z)
                        {
                            std::span<const uint8_t> srcSlice = { srcMipData.data() + (layer * mipDepth + z) * srcSliceSize, srcSliceSize };
                            std::span<uint8_t> dstSlice = { mipBytes.data() + (layer * mipDepth + z) * dstSliceSize, dstSliceSize };

                            compressTextureData(
                                mipWidth,
                                mipHeight,
                                srcSlice,
                                dstSlice,
                                format,
                                hasAlpha
                            );
                        }
                    }
                }
            }
            else
            {
                // 当原数据通道数为3时, 补齐到4通道
                if (pixelChannels == 3)
                {
                    size_t pixelCount = srcMipData.size() / pixelSize;
                    uint32_t dstPixelSize = pixelSize / 3 * 4;
                    for (size_t i = 0; i < pixelCount; ++i)
                    {
                        uint8_t* pixel = mipBytes.data() + i * dstPixelSize;
                        std::memcpy(pixel, srcMipData.data() + i * pixelSize, pixelSize);
                        uint8_t* alpha = pixel + pixelSize / 3;
                    }

                    switch (mDescription.format)
                    {
                    case PixelFormat::RGB8_UNORM:
                    {
                        TVector4<uint8_t>* pixel = reinterpret_cast<TVector4<uint8_t>*>(mipBytes.data());
                        for (size_t i = 0; i < pixelCount; ++i)
                            pixel[i].w = 255;
                        break;
                    }
                    case PixelFormat::RGB16_UNORM:
                    {
                        TVector4<uint16_t>* pixel = reinterpret_cast<TVector4<uint16_t>*>(mipBytes.data());
                        for (size_t i = 0; i < pixelCount; ++i)
                            pixel[i].w = 65535;
                        break;
                    }
                    case PixelFormat::RGB16_SFLOAT:
                    {
                        TVector4<uint16_t>* pixel = reinterpret_cast<TVector4<uint16_t>*>(mipBytes.data());
                        for (size_t i = 0; i < pixelCount; ++i)
                            pixel[i].w = 0x3c00;
                        break;
                    }
                    case PixelFormat::RGB32_SFLOAT:
                    {
                        float4* pixel = reinterpret_cast<float4*>(mipBytes.data());
                        for (size_t i = 0; i < pixelCount; ++i)
                            pixel[i].w = 1.0f;
                        break;
                    }
                    }
                }
                else
                {
                    std::memcpy(mipBytes.data(), srcMipData.data(), srcMipData.size());
                }
            }

            Mip& mip = mMips.emplace_back();
            mip.level = i;
            mip.data.setData(std::move(mipBytes));

            mipWidth = std::max(mipWidth >> 1, 1u);
            mipHeight = std::max(mipHeight >> 1, 1u);
            mipDepth = std::max(mipDepth >> 1, 1u);
        }
    }

    void Texture::setDescription(const Description& description) noexcept
    {
        // 只有"不改变 mSourcePixels 期望字节数"的字段变化才能安全地从已有源像素重新烘焙 mip;
        // 宽高/深度/层数/PixelFormat/mipLevels/isCube 变了意味着 mSourcePixels 的字节布局
        // 也变了,旧的 mSourcePixels(哪怕还在)已经对不上新尺寸——这种情况只清空 mMips,
        // 强制调用方重新 setSourcePixels(),它内部的 expectedSize 断言会在数据依然对不上时
        // 立刻报出来,不会静默产生错的贴图
        bool sourceLayoutChanged = mDescription.format != description.format
            || mDescription.width != description.width
            || mDescription.height != description.height
            || mDescription.depth != description.depth
            || mDescription.mipLevels != description.mipLevels
            || mDescription.arrayLayers != description.arrayLayers
            || mDescription.isCube != description.isCube;

        // 只影响"怎么编码"、不影响 mSourcePixels 字节数的字段——可以直接从已有源像素重新烘焙
        bool bakeConfigChanged = mDescription.isSRGB != description.isSRGB
            || mDescription.compressionPreset != description.compressionPreset
            || mDescription.compressionFormatFamily != description.compressionFormatFamily
            || mDescription.generateMips != description.generateMips;

        mDescription = description;

        if (sourceLayoutChanged)
        {
            mMips.clear();
        }
        else if (bakeConfigChanged && mSourcePixels.getSize() > 0)
        {
            bool wasSourcePixelsLoaded = mSourcePixels.isLoaded();
            if (!wasSourcePixelsLoaded)
                mSourcePixels.load();

            bakeMips();

            if (!wasSourcePixelsLoaded)
                mSourcePixels.unload();
        }
    }

    void Texture::uploadToGpu()
    {
        uploadToGpuInternal(getOrCreateTextureProxy());
    }

    void Texture::uploadToGpuInternal(TextureProxyHandle proxyHandle)
    {
        auto& renderFrontend = Context::get().getRenderFrontend();

        // 上传 GPU 只需要这一份拷贝,拷完就不再需要 CPU 侧常驻——主动 unload 掉,
        // 不用等渲染线程真正消费完这份拷贝(它已经是独立的一份数据了)
        struct UploadMip
        {
            uint32_t level;
            std::vector<uint8_t> data;
        };
        std::vector<UploadMip> uploadMips;
        uploadMips.reserve(mMips.size());
        for (Mip& mip : mMips)
        {
            mip.data.load();
            std::span<const uint8_t> data = mip.data.getData();
            uploadMips.push_back(UploadMip{ mip.level, std::vector<uint8_t>(data.begin(), data.end()) });
            mip.data.unload();
        }

        renderFrontend.enqueueLambda(
            [proxyHandle,
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
            // createTextureProxy() 只建 GPU 纹理对象、不带像素数据——proxy 一旦存在就意味着
            // 材质马上会拿它去采样(getOrCreateTextureProxy() 正是材质纹理参数解析成 proxy 的
            // 必经之路), 这里顺手把已经烘焙好的 mMips 传上去, 不然采样到的是空纹理(渲染出来是黑的),
            // 调用方也不用再记得单独调一次 uploadToGpu()
            uploadToGpuInternal(mProxyHandle);
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

#define INVOKE_GENERATE(FORMAT, IS_SRGB) \
    invokeGenerate<PixelFormat::FORMAT, IS_SRGB>(srcWidth, srcHeight, srcDepth, srcData, dstMips, mipCount, filter)

#define CASE_FORMAT(FORMAT) \
    case PixelFormat::FORMAT: \
        INVOKE_GENERATE(FORMAT, false); \
        break;

#define CASE_FORMAT_SRGB(FORMAT) \
    case PixelFormat::FORMAT: \
        if (isSRGB) INVOKE_GENERATE(FORMAT, true); \
        else INVOKE_GENERATE(FORMAT, false); \
        break;

    void dispatchGenerate(
        PixelFormat format,
        bool isSRGB,
        uint32_t srcWidth,
        uint32_t srcHeight,
        uint32_t srcDepth,
        std::span<const uint8_t> srcData,
        std::span<uint8_t> dstMips,
        uint32_t mipCount,
        MipmapFilter filter
    )
    {
        switch (format)
        {
        CASE_FORMAT_SRGB(R8_UNORM)
        CASE_FORMAT_SRGB(RG8_UNORM)
        CASE_FORMAT_SRGB(RGB8_UNORM)
        CASE_FORMAT_SRGB(RGBA8_UNORM)
        CASE_FORMAT(R16_UNORM)
        CASE_FORMAT(RG16_UNORM)
        CASE_FORMAT(RGB16_UNORM)
        CASE_FORMAT(RGBA16_UNORM)
        CASE_FORMAT(R16_SFLOAT)
        CASE_FORMAT(RG16_SFLOAT)
        CASE_FORMAT(RGB16_SFLOAT)
        CASE_FORMAT(RGBA16_SFLOAT)
        CASE_FORMAT(R32_SFLOAT)
        CASE_FORMAT(RG32_SFLOAT)
        CASE_FORMAT(RGB32_SFLOAT)
        CASE_FORMAT(RGBA32_SFLOAT)
        default: break;
        }
    }

    template <PixelFormat F, bool isSRGB>
    void invokeGenerateCube(
        uint32_t srcWidth,
        uint32_t srcHeight,
        std::span<const uint8_t> src,
        std::span<uint8_t> dstMips,
        uint32_t mipCount,
        MipmapFilter filter
    )
    {
        using Traits = PixelFormatTraits<F, isSRGB>;
        using T = typename Traits::Type;

        auto typedSrc = std::span<const T>(reinterpret_cast<const T*>(src.data()), src.size() / sizeof(T));
        auto typedDst = std::span<T>(reinterpret_cast<T*>(dstMips.data()), dstMips.size() / sizeof(T));

        generateMipmapCube<T>(
            srcWidth, srcHeight,
            typedSrc, typedDst, mipCount,
            filter,
            Traits::getDecode(),
            Traits::getEncode()
        );
    }

#define INVOKE_GENERATE_CUBE(FORMAT, IS_SRGB) \
    invokeGenerateCube<PixelFormat::FORMAT, IS_SRGB>(srcWidth, srcHeight, srcData, dstMips, mipCount, filter)

#define CASE_FORMAT_CUBE(FORMAT) \
    case PixelFormat::FORMAT: INVOKE_GENERATE_CUBE(FORMAT, false); break;

#define CASE_FORMAT_CUBE_SRGB(FORMAT) \
    case PixelFormat::FORMAT: \
        if (isSRGB) INVOKE_GENERATE_CUBE(FORMAT, true); \
        else INVOKE_GENERATE_CUBE(FORMAT, false); \
        break;

    void dispatchGenerateCube(
        PixelFormat format,
        bool isSRGB,
        uint32_t srcWidth,
        uint32_t srcHeight,
        std::span<const uint8_t> srcData,
        std::span<uint8_t> dstMips,
        uint32_t mipCount,
        MipmapFilter filter
    )
    {
        switch (format)
        {
        CASE_FORMAT_CUBE_SRGB(R8_UNORM)
        CASE_FORMAT_CUBE_SRGB(RG8_UNORM)
        CASE_FORMAT_CUBE_SRGB(RGB8_UNORM)
        CASE_FORMAT_CUBE_SRGB(RGBA8_UNORM)
        CASE_FORMAT_CUBE(R16_UNORM)
        CASE_FORMAT_CUBE(RG16_UNORM)
        CASE_FORMAT_CUBE(RGB16_UNORM)
        CASE_FORMAT_CUBE(RGBA16_UNORM)
        CASE_FORMAT_CUBE(R16_SFLOAT)
        CASE_FORMAT_CUBE(RG16_SFLOAT)
        CASE_FORMAT_CUBE(RGB16_SFLOAT)
        CASE_FORMAT_CUBE(RGBA16_SFLOAT)
        CASE_FORMAT_CUBE(R32_SFLOAT)
        CASE_FORMAT_CUBE(RG32_SFLOAT)
        CASE_FORMAT_CUBE(RGB32_SFLOAT)
        CASE_FORMAT_CUBE(RGBA32_SFLOAT)
        default: break;
        }
    }

#undef INVOKE_GENERATE_CUBE
#undef CASE_FORMAT_CUBE
#undef CASE_FORMAT_CUBE_SRGB

    std::vector<uint8_t> Texture::generateMipmaps(std::span<const uint8_t> mip0Data, const Description& description)
    {
        std::vector<uint8_t> mipmaps;

        uint32_t pixelSize = getPixelSizeByFormat(description.format);
        uint32_t arrayLayers = description.arrayLayers;
        size_t mipmapSize = 0;
        {
            uint32_t mipWidth = description.width;
            uint32_t mipHeight = description.height;
            uint32_t mipDepth = description.depth;
            for (uint32_t i = 1; i < description.mipLevels; ++i)
            {
                mipWidth = std::max(mipWidth >> 1, 1u);
                mipHeight = std::max(mipHeight >> 1, 1u);
                mipDepth = std::max(mipDepth >> 1, 1u);

                mipmapSize += mipWidth * mipHeight * mipDepth * pixelSize * arrayLayers;
            }
        }
        mipmaps.resize(mipmapSize);

        uint32_t mipCount = description.mipLevels > 0 ? description.mipLevels - 1 : 0;
        MipmapFilter filter = MipmapFilter::Box;

        if (description.isCube)
        {
            size_t face0Size = static_cast<size_t>(description.width) * description.height * pixelSize;
            size_t cubeMipsSize = mipmapSize;
            assert(mipmapSize % 6 == 0);

            std::span<const uint8_t> srcCube = { mip0Data.data(), face0Size * 6 };
            std::span<uint8_t> dstCube = { mipmaps.data(), cubeMipsSize };
            dispatchGenerateCube(
                description.format,
                description.isSRGB,
                description.width,
                description.height,
                srcCube,
                dstCube,
                mipCount,
                filter
            );

            return mipmaps;
        }

        size_t layer0Size = static_cast<size_t>(description.width) * description.height * description.depth * pixelSize;
        size_t layerMipsSize = mipmapSize / arrayLayers;
        for (uint32_t layer = 0; layer < arrayLayers; ++layer)
        {
            std::span<const uint8_t> srcLayer = { mip0Data.data() + layer * layer0Size, layer0Size };
            std::span<uint8_t> dstLayer = { mipmaps.data() + layer * layerMipsSize, layerMipsSize };
            dispatchGenerate(
                description.format,
                description.isSRGB,
                description.width,
                description.height,
                description.depth,
                srcLayer,
                dstLayer,
                mipCount,
                filter
            );
        }

        return mipmaps;
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
        s->addProperty("Is SRGB", &Texture::Description::isSRGB);
        s->addProperty("Compression Preset", &Texture::Description::compressionPreset);
        s->addProperty("Compression Format Family", &Texture::Description::compressionFormatFamily);
        s->addProperty("Generate Mips", &Texture::Description::generateMips);
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
        c->addProperty("Sample State", &Texture::mSampleState);
        c->addProperty("Source Pixels", &Texture::mSourcePixels);
        c->addProperty("Mips", &Texture::mMips);
        return c;
    }
}
