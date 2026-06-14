#include "Texture.h"
#include "../Context.h"
#include "Mipmap.h"

#include <RenderBase/RenderSystem.h>
#include <RenderBase/TextureProxy.h>

#include <ispc_texcomp.h>

namespace hyp
{
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

    static bool isCompressionFormat(VkFormat format) noexcept
    {
        switch (format)
        {
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
            return true;
        default:
            return false;
        }
    }

    static uint32_t getBlockSizeByVkFormat(VkFormat format) noexcept
    {
        switch (format)
        {
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
            return 8;
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
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

    std::vector<uint8_t> generatePaddingMipData(
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint32_t layers,
        uint32_t pixelSize, // 原始单个像素的总字节数
        uint32_t channels,  // 原始通道数
        std::span<const uint8_t> srcData)
    {
        // 1. 计算单个通道的字节大小 (例如：pixelSize=8, channels=2 -> channelSize=4)
        uint32_t channelSize = pixelSize / channels;

        // 2. 计算目标通道数和目标像素大小
        // 如果通道少于4，补齐到4；否则保持原样
        uint32_t dstChannels = std::max(channels, 4u);
        uint32_t dstPixelSize = dstChannels * channelSize;

        // 3. 计算对齐后的尺寸
        uint32_t paddedWidth = (width + 3) & ~3;
        uint32_t paddedHeight = (height + 3) & ~3;

        // 4. 分配内存并初始化为 0 (自动满足新通道填充 0 的要求)
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

                        // 只拷贝原始存在的通道数据
                        // 如果原通道是 3，则拷贝 3 * channelSize 的数据
                        // 剩下的 (dstChannels - channels) * channelSize 字节由于初始化已为 0
                        std::memcpy(dstPixel, srcPixel, pixelSize);
                    }
                }
            }
        }

        return paddingPixels;
    }

    void compressTextureData(
        uint32_t width,
        uint32_t height,
        std::span<const uint8_t> srcData,
        std::span<uint8_t> dstData,
        VkFormat compressionFormat,
        bool hasAlpha
    )
    {
        rgba_surface surface;
        surface.width = width;
        surface.height = height;
        surface.ptr = const_cast<uint8_t*>(srcData.data());
        surface.stride = static_cast<int32_t>(srcData.size() / height);

        switch (compressionFormat)
        {
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        {
            CompressBlocksBC1(&surface, dstData.data());
            break;
        }
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        {
            CompressBlocksBC3(&surface, dstData.data());
            break;
        }
        case VK_FORMAT_BC4_UNORM_BLOCK:
        {
            CompressBlocksBC4(&surface, dstData.data());
            break;
        }
        case VK_FORMAT_BC5_UNORM_BLOCK:
        {
            CompressBlocksBC5(&surface, dstData.data());
            break;
        }
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        {
            bc6h_enc_settings settings;
            GetProfile_bc6h_veryslow(&settings);
            CompressBlocksBC6H(&surface, dstData.data(), &settings);
            break;
        }
        case VK_FORMAT_BC7_SRGB_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
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

        mSourcePixels = std::move(sourcePixels);

        std::vector<uint8_t> mipmapPixels;
        if (mDescription.generateMips)
        {
            mipmapPixels = generateMipmaps(mSourcePixels, mDescription);
        }

        mMips.reserve(mDescription.mipLevels);
        uint32_t mipWidth = mDescription.width;
        uint32_t mipHeight = mDescription.height;
        uint32_t mipDepth = mDescription.depth;

        VkFormat vkFormat = static_cast<VkFormat>(convertToVkFormat(mDescription.format, mDescription.compressionPreset, mDescription.isSRGB));
        bool compFormat = isCompressionFormat(vkFormat);
        uint32_t blockSize = getBlockSizeByVkFormat(vkFormat);
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
                srcMipData = std::span<const uint8_t>(mSourcePixels.data(), srcMipSize);
                if (!mDescription.generateMips)
                    srcMipOffset += srcMipSize;
            }
            else
            {
                if (mDescription.generateMips)
                    srcMipData = std::span<const uint8_t>(mipmapPixels.data() + srcMipOffset, srcMipSize);
                else
                    srcMipData = std::span<const uint8_t>(mSourcePixels.data() + srcMipOffset, srcMipSize);
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

            auto& mip = mMips.emplace_back(i, std::vector<uint8_t>(mipDataSize));

            if (compFormat)
            {
                uint32_t paddedWidth = (mipWidth + 3) & ~3;
                uint32_t paddedHeight = (mipHeight + 3) & ~3;

                if (mipWidth != paddedWidth || mipHeight != paddedHeight || pixelChannels != 4)
                {
                    std::vector<uint8_t> paddingPixels = generatePaddingMipData(
                        mipWidth,
                        mipHeight,
                        mipDepth,
                        arrayLayers,
                        pixelSize,
                        pixelChannels,
                        srcMipData
                    );

                    size_t srcSliceSize = paddedWidth * paddedHeight * (pixelSize / pixelChannels) * 4;
                    size_t dstSliceSize = (paddedWidth * paddedHeight) / 16 * blockSize;
                    for (uint32_t layer = 0; layer < arrayLayers; ++layer)
                    {
                        for (uint32_t z = 0; z < mipDepth; ++z)
                        {
                            std::span<const uint8_t> srcSlice = { paddingPixels.data() + (layer * mipDepth + z) * srcSliceSize, srcSliceSize };
                            std::span<uint8_t> dstSlice = { mip.data.data() + (layer * mipDepth + z) * dstSliceSize, dstSliceSize};

                            compressTextureData(
                                paddedWidth,
                                paddedHeight,
                                srcSlice,
                                dstSlice,
                                vkFormat,
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
                            std::span<uint8_t> dstSlice = { mip.data.data() + (layer * mipDepth + z) * dstSliceSize, dstSliceSize };

                            compressTextureData(
                                mipWidth,
                                mipHeight,
                                srcSlice,
                                dstSlice,
                                vkFormat,
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
                        uint8_t* pixel = mip.data.data() + i * dstPixelSize;
                        std::memcpy(pixel, srcMipData.data() + i * pixelSize, pixelSize);
                        uint8_t* alpha = pixel + pixelSize / 3;
                    }

                    switch (mDescription.format)
                    {
                    case PixelFormat::RGB8_UNORM:
                    {
                        TVector4<uint8_t>* pixel = reinterpret_cast<TVector4<uint8_t>*>(mip.data.data());
                        for (size_t i = 0; i < pixelCount; ++i)
                            pixel[i].w = 255;
                        break;
                    }
                    case PixelFormat::RGB16_UNORM:
                    {
                        TVector4<uint16_t>* pixel = reinterpret_cast<TVector4<uint16_t>*>(mip.data.data());
                        for (size_t i = 0; i < pixelCount; ++i)
                            pixel[i].w = 65535;
                        break;
                    }
                    case PixelFormat::RGB16_SFLOAT:
                    {
                        TVector4<uint16_t>* pixel = reinterpret_cast<TVector4<uint16_t>*>(mip.data.data());
                        for (size_t i = 0; i < pixelCount; ++i)
                            pixel[i].w = 0x3c00;
                        break;
                    }
                    case PixelFormat::RGB32_SFLOAT:
                    {
                        float4* pixel = reinterpret_cast<float4*>(mip.data.data());
                        for (size_t i = 0; i < pixelCount; ++i)
                            pixel[i].w = 1.0f;
                        break;
                    }
                    }
                }
                else
                {
                    std::memcpy(mip.data.data(), srcMipData.data(), srcMipData.size());
                }
            }

            mipWidth = std::max(mipWidth >> 1, 1u);
            mipHeight = std::max(mipHeight >> 1, 1u);
            mipDepth = std::max(mipDepth >> 1, 1u);
        }
    }

    void Texture::setDescription(const Description& description) noexcept
    {
        mDescription = description;
    }

    void Texture::uploadToGpu()
    {
        TextureProxyHandle proxyHandle = getOrCreateTextureProxy();
        auto& renderFrontend = Context::get().getRenderFrontend();

        auto& mips = mMips;

        renderFrontend.enqueueLambda(
            [proxyHandle,
            mips](RenderFrameContext& context) mutable
            {
                TextureProxy* proxy = context.getProxyManager().getProxy<ProxyType::Texture>(proxyHandle);
                for (auto& mip : mips)
                {
                    proxy->stageUpload(std::move(mip.data), mip.level);
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
        }
        return mProxyHandle;
    }

    void Texture::destroyTextureProxy(TextureProxyHandle handle) noexcept
    {
        assert(handle.isValid());

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [handle](RenderFrameContext& context)
            {
                context.getProxyManager().destroyProxy(handle);
            }
        );
    }

#define INVOKE_GENERATE(PIXEL_FORMAT, IS_SRGB) invokeGenerate<PIXEL_FORMAT, IS_SRGB>(srcWidth, srcHeight, srcDepth, srcData, dstData)

    void dispatchGenerate(
        PixelFormat format,
        bool isSRGB,
        uint32_t srcWidth,
        uint32_t srcHeight,
        uint32_t srcDepth,
        std::span<const uint8_t> srcData,
        std::span<uint8_t> dstData
    )
    {
        switch (format)
        {
        case PixelFormat::R8_UNORM:
        {
            if (isSRGB)
                INVOKE_GENERATE(PixelFormat::R8_UNORM, true);
            else
                INVOKE_GENERATE(PixelFormat::R8_UNORM, false);
            break;
        }
        case PixelFormat::RG8_UNORM:
        {
            if (isSRGB)
                INVOKE_GENERATE(PixelFormat::RG8_UNORM, true);
            else
                INVOKE_GENERATE(PixelFormat::RG8_UNORM, false);
            break;
        }
        case PixelFormat::RGB8_UNORM:
        {
            if (isSRGB)
                INVOKE_GENERATE(PixelFormat::RGB8_UNORM, true);
            else
                INVOKE_GENERATE(PixelFormat::RGB8_UNORM, false);
            break;
        }
        case PixelFormat::RGBA8_UNORM:
        {
            if (isSRGB)
                INVOKE_GENERATE(PixelFormat::RGBA8_UNORM, true);
            else
                INVOKE_GENERATE(PixelFormat::RGBA8_UNORM, false);
            break;
        }
        case PixelFormat::R16_UNORM: INVOKE_GENERATE(PixelFormat::R16_UNORM, false); break;
        case PixelFormat::RG16_UNORM: INVOKE_GENERATE(PixelFormat::RG16_UNORM, false); break;
        case PixelFormat::RGB16_UNORM: INVOKE_GENERATE(PixelFormat::RGB16_UNORM, false); break;
        case PixelFormat::RGBA16_UNORM: INVOKE_GENERATE(PixelFormat::RGBA16_UNORM, false); break;
        case PixelFormat::R16_SFLOAT: INVOKE_GENERATE(PixelFormat::R16_SFLOAT, false); break;
        case PixelFormat::RG16_SFLOAT: INVOKE_GENERATE(PixelFormat::RG16_SFLOAT, false); break;
        case PixelFormat::RGB16_SFLOAT: INVOKE_GENERATE(PixelFormat::RGB16_SFLOAT, false); break;
        case PixelFormat::RGBA16_SFLOAT: INVOKE_GENERATE(PixelFormat::RGBA16_SFLOAT, false); break;
        case PixelFormat::R32_SFLOAT: INVOKE_GENERATE(PixelFormat::R32_SFLOAT, false); break;
        case PixelFormat::RG32_SFLOAT: INVOKE_GENERATE(PixelFormat::RG32_SFLOAT, false); break;
        case PixelFormat::RGB32_SFLOAT: INVOKE_GENERATE(PixelFormat::RGB32_SFLOAT, false); break;
        case PixelFormat::RGBA32_SFLOAT: INVOKE_GENERATE(PixelFormat::RGBA32_SFLOAT, false); break;
        default: break;
        }
    }

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

        size_t srcDataOffset = 0;
        size_t dstDataOffset = 0;
        uint32_t prevMipWidth = description.width;
        uint32_t prevMipHeight = description.height;
        uint32_t prevMipDepth = description.depth;
        size_t prevMipSize = description.width * description.height * description.depth * pixelSize * arrayLayers;
        for (uint32_t mip = 1; mip < description.mipLevels; ++mip)
        {
            uint32_t mipWidth = std::max(prevMipWidth >> 1, 1u);
            uint32_t mipHeight = std::max(prevMipHeight >> 1, 1u);
            uint32_t mipDepth = std::max(prevMipDepth >> 1, 1u);

            std::span<const uint8_t> srcData;
            if (mip == 1)
            {
                srcData = mip0Data;
            }
            else
            {
                srcData = { mipmaps.data() + srcDataOffset, prevMipSize };
                srcDataOffset += prevMipSize;
            }
            size_t mipSize = mipWidth * mipHeight * mipDepth * pixelSize * arrayLayers;
            std::span<uint8_t> dstData = { mipmaps.data() + dstDataOffset, mipSize };
            dstDataOffset += mipSize;

            assert(prevMipSize % arrayLayers == 0 && mipSize % arrayLayers == 0);
            size_t prevLayerSize = prevMipSize / arrayLayers;
            size_t layerSize = mipSize / arrayLayers;
            for (uint32_t i = 0; i < arrayLayers; ++i)
            {
                std::span<const uint8_t> srcLayer = { srcData.data() + i * prevLayerSize, prevLayerSize };
                std::span<uint8_t> dstLayer = { dstData.data() + i * layerSize, layerSize };
                dispatchGenerate(description.format, description.isSRGB, prevMipWidth, prevMipHeight, prevMipDepth, srcLayer, dstLayer);
            }

            prevMipWidth = mipWidth;
            prevMipHeight = mipHeight;
            prevMipDepth = mipDepth;
            prevMipSize = mipSize;
        }

        return mipmaps;
    }

    uint32_t Texture::convertToVkFormat(PixelFormat format, CompressionPreset compressionPreset, bool isSRGB) noexcept
    {
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
                (isSRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK) :
                (isSRGB ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK);
        }
        else if (compressionPreset == CompressionPreset::High_Quality && is8BitUnorm)
        {
            return isSRGB ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
        }
        else if (compressionPreset == CompressionPreset::Normal_Map && is8BitUnorm)
        {
            return VK_FORMAT_BC5_UNORM_BLOCK;
        }
        else if (compressionPreset == CompressionPreset::Grayscale && is8BitUnorm)
        {
            return VK_FORMAT_BC4_UNORM_BLOCK;
        }
        else if (compressionPreset == CompressionPreset::HDR && is16BitSfloat)
        {
            return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        }
        else
        {
            switch (format)
            {
            case PixelFormat::R8_UNORM: return isSRGB ? VK_FORMAT_R8_SRGB : VK_FORMAT_R8_UNORM;
            case PixelFormat::RG8_UNORM: return isSRGB ? VK_FORMAT_R8G8_SRGB : VK_FORMAT_R8G8_UNORM;
            case PixelFormat::RGB8_UNORM: return isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            case PixelFormat::RGBA8_UNORM: return isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            case PixelFormat::R16_UNORM: return VK_FORMAT_R16_UNORM;
            case PixelFormat::RG16_UNORM: return VK_FORMAT_R16G16_UNORM;
            case PixelFormat::RGB16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
            case PixelFormat::RGBA16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
            case PixelFormat::R16_SFLOAT: return VK_FORMAT_R16_SFLOAT;
            case PixelFormat::RG16_SFLOAT: return VK_FORMAT_R16G16_SFLOAT;
            case PixelFormat::RGB16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
            case PixelFormat::RGBA16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
            case PixelFormat::R32_SFLOAT: return VK_FORMAT_R32_SFLOAT;
            case PixelFormat::RG32_SFLOAT: return VK_FORMAT_R32G32_SFLOAT;
            case PixelFormat::RGB32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case PixelFormat::RGBA32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
            default: return VK_FORMAT_UNDEFINED;
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
        s->addProperty("Array Layers", &Texture::Description::arrayLayers);
        return s;
    }

    template <> Type* Reflection::createType<Texture>()
    {
        auto c = new TClass<Texture, Object>("Texture");
        c->addProperty("Description", &Texture::mDescription);
        c->addProperty("Sample State", &Texture::mSampleState);
        return c;
    }
}
