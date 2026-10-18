#include "ThumbnailProvider.h"

#include <Core/Asset/Asset.h>
#include <Core/Common/Hash.h>
#include <Core/Math/Math.h>
#include <Engine/Config.h>
#include <Engine/Context.h>
#include <Engine/Texture/Texture2D.h>
#include <Engine/Texture/TextureCube.h>

#include <RenderBase/FrameContext.h>
#include <RenderBase/ProxyManager.h>
#include <RenderBase/RenderFrontend.h>
#include <RenderBase/TextureProxy.h>

#include <TextureUtility/Mipmap.h>

#include <Platform/Platform.h>

#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace hyp::ed
{
    namespace
    {
        constexpr uint32_t kMaxThumbnailEdge = 128;
        constexpr uint32_t kCubeThumbnailWidth = 128;
        constexpr uint32_t kCubeThumbnailHeight = 64;
        constexpr int kMaxThumbnailsPerFrame = 4;

        constexpr uint32_t log2Floor(uint32_t v) { uint32_t r = 0; while (v >>= 1) ++r; return r; }
        constexpr uint32_t kThumbnailMipLevels = log2Floor(kMaxThumbnailEdge) + 1;
        // ImGui 后端的 Vulkan 描述符池现在是 1024(见 ImGuiBackend.cpp), 这里留出一半余量
        // 给 SceneView 视口/TextureEditor 预览这些别的 ImGui 纹理注册。
        constexpr size_t kMaxGpuThumbnails = 512;

        // 缩略图是给人看的展示图, 不是给 shader 采样的数据贴图——不管源纹理自己是不是 sRGB
        // (法线贴图/粗糙度这类"数据贴图"通常不是), 输出恒定按 sRGB 曲线编码, 因为
        // uploadThumbnail() 建 GPU 纹理时也恒定按 isSRGB=true 处理。源纹理的 isSRGB 只影响
        // 上面解码那一步(必须按真实编码方式解读原始字节, 重采样才能在正确的线性空间做)。
        template <typename DecodedType>
        void encodeLinearToRGBA8(const DecodedType& v, uint8_t* out)
        {
            auto encodeColorChannel = [](float c) -> uint8_t
            {
                return ColorSpace::linearToSrgb(c);
            };
            auto encodeLinearChannel = [](float c) -> uint8_t
            {
                return static_cast<uint8_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
            };

            if constexpr (std::is_same_v<DecodedType, float>)
            {
                uint8_t gray = encodeColorChannel(v);
                out[0] = gray; out[1] = gray; out[2] = gray; out[3] = 255;
            }
            else if constexpr (std::is_same_v<DecodedType, float2>)
            {
                out[0] = encodeColorChannel(v.x); out[1] = encodeColorChannel(v.y); out[2] = 0; out[3] = 255;
            }
            else if constexpr (std::is_same_v<DecodedType, float3>)
            {
                out[0] = encodeColorChannel(v.x); out[1] = encodeColorChannel(v.y); out[2] = encodeColorChannel(v.z); out[3] = 255;
            }
            else
            {
                out[0] = encodeColorChannel(v.x); out[1] = encodeColorChannel(v.y); out[2] = encodeColorChannel(v.z);
                out[3] = encodeLinearChannel(v.w);
            }
        }

        // 复用 ITextureUtility 生成 mip 链时用的同一套 resampleImage2D(线性空间滤波、sRGB
        // 感知), 只是目标尺寸是任意值(等比缩放到最长边 kMaxThumbnailEdge), 不是逐级减半
        template <PixelFormat F, bool IsSRGB>
        void resizeToRGBA8Impl(std::span<const uint8_t> srcBytes, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight, std::vector<uint8_t>& outRgba8)
        {
            using Traits = PixelFormatTraits<F, IsSRGB>;
            using T = typename Traits::Type;
            auto decode = Traits::getDecode();
            using DecodedType = decltype(decode(std::declval<T>()));

            std::span<const T> typedSrc(reinterpret_cast<const T*>(srcBytes.data()), srcBytes.size() / sizeof(T));

            std::vector<DecodedType> decoded(typedSrc.size());
            for (size_t i = 0; i < typedSrc.size(); ++i)
                decoded[i] = decode(typedSrc[i]);

            std::vector<DecodedType> resized(static_cast<size_t>(dstWidth) * dstHeight);
            resampleImage2D<DecodedType>(
                std::span<const DecodedType>(decoded.data(), decoded.size()), srcWidth, srcHeight,
                std::span<DecodedType>(resized.data(), resized.size()), dstWidth, dstHeight,
                MipmapFilter::Box
            );

            outRgba8.resize(static_cast<size_t>(dstWidth) * dstHeight * 4);
            for (size_t i = 0; i < resized.size(); ++i)
                encodeLinearToRGBA8(resized[i], outRgba8.data() + i * 4);
        }

        // 只支持 8-bit UNORM 格式——16-bit/float(HDR)源需要额外的色调映射决策, 暂不支持,
        // 调用方回退默认占位图
        bool resizeSourcePixelsToRGBA8(PixelFormat format, bool isSRGB, std::span<const uint8_t> src, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight, std::vector<uint8_t>& outRgba8)
        {
            switch (format)
            {
            case PixelFormat::R8_UNORM:
                isSRGB ? resizeToRGBA8Impl<PixelFormat::R8_UNORM, true>(src, srcWidth, srcHeight, dstWidth, dstHeight, outRgba8)
                       : resizeToRGBA8Impl<PixelFormat::R8_UNORM, false>(src, srcWidth, srcHeight, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RG8_UNORM:
                isSRGB ? resizeToRGBA8Impl<PixelFormat::RG8_UNORM, true>(src, srcWidth, srcHeight, dstWidth, dstHeight, outRgba8)
                       : resizeToRGBA8Impl<PixelFormat::RG8_UNORM, false>(src, srcWidth, srcHeight, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RGB8_UNORM:
                isSRGB ? resizeToRGBA8Impl<PixelFormat::RGB8_UNORM, true>(src, srcWidth, srcHeight, dstWidth, dstHeight, outRgba8)
                       : resizeToRGBA8Impl<PixelFormat::RGB8_UNORM, false>(src, srcWidth, srcHeight, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RGBA8_UNORM:
                isSRGB ? resizeToRGBA8Impl<PixelFormat::RGBA8_UNORM, true>(src, srcWidth, srcHeight, dstWidth, dstHeight, outRgba8)
                       : resizeToRGBA8Impl<PixelFormat::RGBA8_UNORM, false>(src, srcWidth, srcHeight, dstWidth, dstHeight, outRgba8);
                return true;
            default:
                return false;
            }
        }

        // dirToEquirectUV()(TextureCube.cpp)的精确代数逆——phi/theta 的标准球坐标定义,
        // 代入验证过 dirToEquirectUV(equirectUVToDirection(u,v)) == (u,v)。用来把等距柱状图
        // 目标像素的 uv 转回方向, 再交给 cubeGetAddressFor() 找立方体上对应的面+uv。
        float3 equirectUVToDirection(float u, float v)
        {
            float phi = u * 2.0f * Math::PI - Math::PI;
            float theta = v * Math::PI;
            float sinTheta = std::sin(theta);
            return float3(sinTheta * std::cos(phi), sinTheta * std::sin(phi), std::cos(theta));
        }

        template <PixelFormat F, bool IsSRGB>
        void resizeCubeToEquirectRGBA8(std::span<const uint8_t> srcBytes, uint32_t faceEdge, uint32_t dstWidth, uint32_t dstHeight, std::vector<uint8_t>& outRgba8)
        {
            using Traits = PixelFormatTraits<F, IsSRGB>;
            using T = typename Traits::Type;
            auto decode = Traits::getDecode();
            using DecodedType = decltype(decode(std::declval<T>()));

            std::span<const T> typedSrc(reinterpret_cast<const T*>(srcBytes.data()), srcBytes.size() / sizeof(T));

            std::vector<DecodedType> decoded(typedSrc.size());
            for (size_t i = 0; i < typedSrc.size(); ++i)
                decoded[i] = decode(typedSrc[i]);

            size_t faceSize = static_cast<size_t>(faceEdge) * faceEdge;
            std::array<std::span<const DecodedType>, 6> faceSpans;
            for (size_t face = 0; face < 6; ++face)
                faceSpans[face] = std::span<const DecodedType>(decoded.data() + face * faceSize, faceSize);

            outRgba8.resize(static_cast<size_t>(dstWidth) * dstHeight * 4);
            for (uint32_t y = 0; y < dstHeight; ++y)
            {
                for (uint32_t x = 0; x < dstWidth; ++x)
                {
                    float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(dstWidth);
                    float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(dstHeight);
                    float3 dir = equirectUVToDirection(u, v);
                    DecodedType sample = sampleCubemap<DecodedType>(faceSpans, faceEdge, dir);
                    // float 格式(HDR 数据)解码结果可能超过 1.0——encodeLinearToRGBA8() 内部的
                    // ColorSpace::linearToSrgb() 会先 clamp 到 [0,1] 再编码, 超出部分直接裁到
                    // 纯白, 缩略图不需要额外做 tonemap。
                    encodeLinearToRGBA8(sample, outRgba8.data() + (static_cast<size_t>(y) * dstWidth + x) * 4);
                }
            }
        }

        // TextureCube 是否是 HDR 数据完全由 PixelFormat 决定, 不是所有 TextureCube 都是——
        // 这里同时支持 8-bit UNORM 和 float 格式(这个项目里 TextureCubeImportDialog.cpp
        // 目前用的是 RGB16_SFLOAT)。float 格式在 PixelFormatTraits 里只有 isSRGB=false
        // 这一个特化, 不需要(也不能)按 isSRGB 分支。
        bool resizeCubeSourcePixelsToEquirectRGBA8(PixelFormat format, bool isSRGB, std::span<const uint8_t> src, uint32_t faceEdge, uint32_t dstWidth, uint32_t dstHeight, std::vector<uint8_t>& outRgba8)
        {
            switch (format)
            {
            case PixelFormat::R8_UNORM:
                isSRGB ? resizeCubeToEquirectRGBA8<PixelFormat::R8_UNORM, true>(src, faceEdge, dstWidth, dstHeight, outRgba8)
                       : resizeCubeToEquirectRGBA8<PixelFormat::R8_UNORM, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RG8_UNORM:
                isSRGB ? resizeCubeToEquirectRGBA8<PixelFormat::RG8_UNORM, true>(src, faceEdge, dstWidth, dstHeight, outRgba8)
                       : resizeCubeToEquirectRGBA8<PixelFormat::RG8_UNORM, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RGB8_UNORM:
                isSRGB ? resizeCubeToEquirectRGBA8<PixelFormat::RGB8_UNORM, true>(src, faceEdge, dstWidth, dstHeight, outRgba8)
                       : resizeCubeToEquirectRGBA8<PixelFormat::RGB8_UNORM, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RGBA8_UNORM:
                isSRGB ? resizeCubeToEquirectRGBA8<PixelFormat::RGBA8_UNORM, true>(src, faceEdge, dstWidth, dstHeight, outRgba8)
                       : resizeCubeToEquirectRGBA8<PixelFormat::RGBA8_UNORM, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::R16_SFLOAT:
                resizeCubeToEquirectRGBA8<PixelFormat::R16_SFLOAT, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RG16_SFLOAT:
                resizeCubeToEquirectRGBA8<PixelFormat::RG16_SFLOAT, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RGB16_SFLOAT:
                resizeCubeToEquirectRGBA8<PixelFormat::RGB16_SFLOAT, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RGBA16_SFLOAT:
                resizeCubeToEquirectRGBA8<PixelFormat::RGBA16_SFLOAT, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::R32_SFLOAT:
                resizeCubeToEquirectRGBA8<PixelFormat::R32_SFLOAT, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RG32_SFLOAT:
                resizeCubeToEquirectRGBA8<PixelFormat::RG32_SFLOAT, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RGB32_SFLOAT:
                resizeCubeToEquirectRGBA8<PixelFormat::RGB32_SFLOAT, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            case PixelFormat::RGBA32_SFLOAT:
                resizeCubeToEquirectRGBA8<PixelFormat::RGBA32_SFLOAT, false>(src, faceEdge, dstWidth, dstHeight, outRgba8);
                return true;
            default:
                return false;
            }
        }

        // 要求 srcWidth/srcHeight 不超过 canvasEdge, 否则 offsetX/offsetY 的减法会 uint32_t 下溢。
        void padToSquareRGBA8(std::span<const uint8_t> src, uint32_t srcWidth, uint32_t srcHeight, uint32_t canvasEdge, std::vector<uint8_t>& outRgba8)
        {
            outRgba8.assign(static_cast<size_t>(canvasEdge) * canvasEdge * 4, 0);
            uint32_t offsetX = (canvasEdge - srcWidth) / 2;
            uint32_t offsetY = (canvasEdge - srcHeight) / 2;
            for (uint32_t y = 0; y < srcHeight; ++y)
            {
                const uint8_t* srcRow = src.data() + static_cast<size_t>(y) * srcWidth * 4;
                uint8_t* dstRow = outRgba8.data() + (static_cast<size_t>(y + offsetY) * canvasEdge + offsetX) * 4;
                std::memcpy(dstRow, srcRow, static_cast<size_t>(srcWidth) * 4);
            }
        }

        // GPU 端常驻的缩略图/图标纹理都不是资产, 不需要 Texture2D 那套压缩存储/离线烘焙语义——
        // 直接用 TextureUtility 生成 mip 链用的同一套 invokeGenerate(线性空间 box filter、
        // sRGB 感知)在这里现算, 只产出 mip1 及以后的字节(mip0 就是传进来的 mip0Rgba8, 调用方
        // 已经有了)。IsSRGB 由调用方按内容决定: 缩略图固定按 sRGB 编码(见 encodeLinearToRGBA8()
        // 的注释), 图标(loadIconTexture())保留 stb_image 读出来的"直接显示"字节, 传 false。
        template <bool IsSRGB>
        std::vector<uint8_t> generateThumbnailMipChain(std::span<const uint8_t> mip0Rgba8, uint32_t width, uint32_t height, uint32_t mipLevels)
        {
            if (mipLevels <= 1)
                return {};

            size_t chainSize = 0;
            {
                uint32_t mipWidth = width, mipHeight = height;
                for (uint32_t i = 1; i < mipLevels; ++i)
                {
                    mipWidth = std::max(mipWidth >> 1, 1u);
                    mipHeight = std::max(mipHeight >> 1, 1u);
                    chainSize += static_cast<size_t>(mipWidth) * mipHeight * 4;
                }
            }

            std::vector<uint8_t> mipChain(chainSize);
            invokeGenerate<PixelFormat::RGBA8_UNORM, IsSRGB>(
                width, height, 1,
                mip0Rgba8, std::span<uint8_t>(mipChain.data(), mipChain.size()),
                mipLevels - 1, MipmapFilter::Box
            );
            return mipChain;
        }
    }

    ThumbnailProvider::ThumbnailProvider() :
        mThumbnailDiskCache(Platform::getExecutableDir() / "Cache", "Thumbnails")
    {
        const std::filesystem::path& resourceDir = Context::get().getConfig().getResourceDir();
        loadIconTexture(resourceDir / "folder.png", mFolderIconHandle, mFolderIconProxyHandle);
        loadIconTexture(resourceDir / "asset.png", mAssetIconHandle, mAssetIconProxyHandle);
    }

    void ThumbnailProvider::loadIconTexture(const std::filesystem::path& pngPath, ImGuiTextureHandle& outHandle, TextureProxyHandle& outProxyHandle)
    {
        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load(pngPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels)
            return;

        size_t pixelDataSize = static_cast<size_t>(width) * height * 4;
        std::vector<uint8_t> mip0Rgba8(pixels, pixels + pixelDataSize);
        stbi_image_free(pixels);

        // 直接建 TextureProxy, 不经过 Texture2D——理由同 uploadThumbnail(): 这两张图标只
        // 上传一次、常驻显示, 不需要 Texture2D 为"可编辑资产"提供的那套压缩存储 source
        // pixels/DDC 缓存语义。
        //
        // isSRGB 必须是 false: 这里画的时候统一走原始 drawList->AddImage(), 不经过
        // SRGBImage() 那套会在采样后把硬件自动解码出的线性值重新编码回 sRGB 的自定义
        // pipeline(见 ImGuiShadedImage.h 的说明)——纹理标成 isSRGB=true 会让 GPU 采样时
        // 自动做一次 sRGB->线性解码, 但没有任何环节把它编回去, 显示出来的就是没做 gamma
        // 校正的线性值, 看起来发灰发平。stb_image 读出来的 PNG 字节本来就是"直接显示"
        // 语义, 不需要 GPU 再解码一次, mip 链生成同理要按 IsSRGB=false 解码/编码。
        uint32_t texWidth = static_cast<uint32_t>(width);
        uint32_t texHeight = static_cast<uint32_t>(height);
        uint32_t mipLevels = log2Floor(std::max(texWidth, texHeight)) + 1;
        std::vector<uint8_t> extraMips = generateThumbnailMipChain<false>(mip0Rgba8, texWidth, texHeight, mipLevels);

        struct UploadMip
        {
            uint32_t level;
            std::vector<uint8_t> data;
        };
        std::vector<UploadMip> uploadMips;
        uploadMips.reserve(mipLevels);
        uploadMips.push_back(UploadMip{ 0, std::move(mip0Rgba8) });
        {
            uint32_t mipWidth = texWidth, mipHeight = texHeight;
            size_t offset = 0;
            for (uint32_t level = 1; level < mipLevels; ++level)
            {
                mipWidth = std::max(mipWidth >> 1, 1u);
                mipHeight = std::max(mipHeight >> 1, 1u);
                size_t levelSize = static_cast<size_t>(mipWidth) * mipHeight * 4;
                uploadMips.push_back(UploadMip{ level, std::vector<uint8_t>(extraMips.begin() + offset, extraMips.begin() + offset + levelSize) });
                offset += levelSize;
            }
        }

        outProxyHandle = Context::get().getRenderFrontend().allocateProxyHandle<ProxyType::Texture>();
        TextureProxyHandle proxyHandle = outProxyHandle;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle, mips = std::move(uploadMips), texWidth, texHeight, mipLevels, textureHandle = &outHandle](FrameContext& context) mutable
            {
                rhi::TextureDesc desc;
                desc.type = rhi::TextureType::Texture_2D;
                desc.format = rhi::TextureFormat::RGBA8_UNORM;
                desc.extent = { texWidth, texHeight, 1 };
                desc.mipCount = mipLevels;
                desc.layerCount = 1;
                desc.usage = rhi::TextureUsage::Transfer_Dst | rhi::TextureUsage::Sampled;

                context.getProxyManager().createProxy(proxyHandle, desc);
                TextureProxy* proxy = context.getProxyManager().getProxy<ProxyType::Texture>(proxyHandle);
                for (auto& mip : mips)
                    proxy->stageUpload(std::move(mip.data), mip.level, 0, 1);
                textureHandle->update(proxy->getTextureView());
            }
        );
    }

    void ThumbnailProvider::shutdown()
    {
        mFolderIconHandle.reset();
        mAssetIconHandle.reset();
        destroyThumbnailProxy(mFolderIconProxyHandle);
        destroyThumbnailProxy(mAssetIconProxyHandle);
        mFolderIconProxyHandle.reset();
        mAssetIconProxyHandle.reset();

        for (auto& [guid, thumbnail] : mGpuThumbnails)
        {
            thumbnail.handle.reset();
            destroyThumbnailProxy(thumbnail.proxyHandle);
            thumbnail.proxyHandle.reset();
        }
        mGpuThumbnails.clear();
    }

    void ThumbnailProvider::collectPrepareTextures(std::vector<TextureProxyHandle>& out)
    {
        if (mFolderIconProxyHandle.isValid())
            out.push_back(mFolderIconProxyHandle);
        if (mAssetIconProxyHandle.isValid())
            out.push_back(mAssetIconProxyHandle);
        for (auto& [guid, thumbnail] : mGpuThumbnails)
        {
            if (thumbnail.proxyHandle.isValid())
                out.push_back(thumbnail.proxyHandle);
        }
    }

    void ThumbnailProvider::resetThumbnailBudget() noexcept
    {
        ++mCurrentFrame;
        mThumbnailBudgetThisFrame = kMaxThumbnailsPerFrame;
        evictLeastRecentlyUsedThumbnails();
    }

    ImTextureID ThumbnailProvider::getThumbnail(Asset* asset)
    {
        if (!asset || !asset->getRootClass())
            return 0;

        Class* rootClass = asset->getRootClass();
        bool isTexture2D = rootClass->isDerivedFrom(Texture2D::staticClass());
        bool isTextureCube = !isTexture2D && rootClass->isDerivedFrom(TextureCube::staticClass());
        if (!isTexture2D && !isTextureCube)
            return 0;

        Guid guid = asset->getRootGuid();

        if (auto it = mGpuThumbnails.find(guid); it != mGpuThumbnails.end())
        {
            it->second.lastUsedFrame = mCurrentFrame;
            return it->second.handle.get();
        }

        const std::filesystem::path& physicalPath = asset->getPhysicalPath();
        uint128_t currentKey = computeThumbnailKey(guid, physicalPath);

        // 之前在这个 mtime 下生成失败过(比如格式暂不支持)就不再重试——不然每一帧只要
        // 预算够就会重新触发一次完整生成(现查 Asset、解压 BulkData), 对一个永远不会成功
        // 的资产来说等于每帧都在做无用功、直接把帧率拖垮。资产改动后 mtime 变了, 这里自然
        // 不再匹配, 会正常重新尝试一次。
        if (auto it = mFailedGenerationKeys.find(guid); it != mFailedGenerationKeys.end() && it->second == currentKey)
            return 0;

        if (std::optional<std::vector<uint8_t>> diskPixels = tryLoadThumbnailPixels(guid, physicalPath))
        {
            uploadThumbnail(guid, *diskPixels);
            return mGpuThumbnails[guid].handle.get();
        }

        if (mThumbnailBudgetThisFrame <= 0)
            return 0;
        --mThumbnailBudgetThisFrame;

        std::vector<uint8_t> generatedPixels;
        bool generated = isTexture2D
            ? generateTextureThumbnail(*asset, generatedPixels)
            : generateCubeThumbnail(*asset, generatedPixels);
        if (!generated)
        {
            mFailedGenerationKeys[guid] = currentKey;
            return 0;
        }
        mFailedGenerationKeys.erase(guid);

        saveThumbnailPixels(guid, physicalPath, generatedPixels);
        uploadThumbnail(guid, generatedPixels);
        return mGpuThumbnails[guid].handle.get();
    }

    void ThumbnailProvider::evictLeastRecentlyUsedThumbnails()
    {
        if (mGpuThumbnails.size() <= kMaxGpuThumbnails)
            return;

        std::vector<Guid> byAge;
        byAge.reserve(mGpuThumbnails.size());
        for (auto& [guid, thumbnail] : mGpuThumbnails)
            byAge.push_back(guid);
        std::sort(byAge.begin(), byAge.end(), [this](Guid a, Guid b)
        {
            return mGpuThumbnails.at(a).lastUsedFrame < mGpuThumbnails.at(b).lastUsedFrame;
        });

        size_t evictCount = mGpuThumbnails.size() - kMaxGpuThumbnails;
        for (size_t i = 0; i < evictCount; ++i)
        {
            auto it = mGpuThumbnails.find(byAge[i]);
            it->second.handle.reset();
            destroyThumbnailProxy(it->second.proxyHandle);
            mGpuThumbnails.erase(it);
        }
    }

    uint128_t ThumbnailProvider::computeThumbnailKey(Guid guid, const std::filesystem::path& physicalPath) const
    {
        uint128_t key{};
        Hash::combine(key, guid);
        std::error_code ec;
        auto mtime = std::filesystem::last_write_time(physicalPath, ec);
        int64_t mtimeCount = ec ? 0 : mtime.time_since_epoch().count();
        Hash::combine(key, mtimeCount);
        return key;
    }

    std::optional<std::vector<uint8_t>> ThumbnailProvider::tryLoadThumbnailPixels(Guid guid, const std::filesystem::path& physicalPath) const
    {
        std::optional<std::vector<uint8_t>> pngBytes = mThumbnailDiskCache.tryLoad(computeThumbnailKey(guid, physicalPath));
        if (!pngBytes)
            return std::nullopt;

        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(pngBytes->data(), static_cast<int>(pngBytes->size()), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels)
            return std::nullopt;

        // 尺寸跟 kMaxThumbnailEdge 对不上说明是旧格式的缓存(改分辨率/加方形填充之前生成的), 当作未命中处理。
        if (width != static_cast<int>(kMaxThumbnailEdge) || height != static_cast<int>(kMaxThumbnailEdge))
        {
            stbi_image_free(pixels);
            return std::nullopt;
        }

        std::vector<uint8_t> rgba8(pixels, pixels + static_cast<size_t>(width) * height * 4);
        stbi_image_free(pixels);
        return rgba8;
    }

    void ThumbnailProvider::saveThumbnailPixels(Guid guid, const std::filesystem::path& physicalPath, std::span<const uint8_t> rgba8) const
    {
        int pngSize = 0;
        unsigned char* pngData = stbi_write_png_to_mem(rgba8.data(), static_cast<int>(kMaxThumbnailEdge) * 4, static_cast<int>(kMaxThumbnailEdge), static_cast<int>(kMaxThumbnailEdge), 4, &pngSize);
        if (!pngData)
            return;

        mThumbnailDiskCache.save(computeThumbnailKey(guid, physicalPath), std::span<const uint8_t>(pngData, static_cast<size_t>(pngSize)));
        free(pngData);
    }

    bool ThumbnailProvider::generateTextureThumbnail(Asset& asset, std::vector<uint8_t>& outRgba8) const
    {
        bool wasLoaded = asset.getState() != AssetState::Unloaded;
        if (!wasLoaded)
            asset.load();

        bool generated = false;
        if (TRef<Texture2D> texture = asset.getRootObject<Texture2D>())
        {
            const Texture::Description& desc = texture->getDescription();
            if (desc.width > 0 && desc.height > 0)
            {
                uint32_t fitWidth, fitHeight;
                if (desc.width >= desc.height)
                {
                    fitWidth = kMaxThumbnailEdge;
                    fitHeight = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(desc.height) * kMaxThumbnailEdge / desc.width)));
                }
                else
                {
                    fitHeight = kMaxThumbnailEdge;
                    fitWidth = std::max(1u, static_cast<uint32_t>(std::lround(static_cast<double>(desc.width) * kMaxThumbnailEdge / desc.height)));
                }

                std::vector<uint8_t> sourcePixels = texture->copySourcePixels();
                std::vector<uint8_t> fitPixels;
                if (resizeSourcePixelsToRGBA8(desc.format, texture->isSRGB(), sourcePixels, desc.width, desc.height, fitWidth, fitHeight, fitPixels))
                {
                    padToSquareRGBA8(fitPixels, fitWidth, fitHeight, kMaxThumbnailEdge, outRgba8);
                    generated = true;
                }
            }
        }

        if (!wasLoaded)
            asset.tryUnload();

        return generated;
    }

    bool ThumbnailProvider::generateCubeThumbnail(Asset& asset, std::vector<uint8_t>& outRgba8) const
    {
        bool wasLoaded = asset.getState() != AssetState::Unloaded;
        if (!wasLoaded)
            asset.load();

        bool generated = false;
        if (TRef<TextureCube> cube = asset.getRootObject<TextureCube>())
        {
            const Texture::Description& desc = cube->getDescription();
            if (desc.width > 0)
            {
                std::vector<uint8_t> sourcePixels = cube->copySourcePixels();
                std::vector<uint8_t> equirectPixels;
                if (resizeCubeSourcePixelsToEquirectRGBA8(desc.format, cube->isSRGB(), sourcePixels, desc.width, kCubeThumbnailWidth, kCubeThumbnailHeight, equirectPixels))
                {
                    padToSquareRGBA8(equirectPixels, kCubeThumbnailWidth, kCubeThumbnailHeight, kMaxThumbnailEdge, outRgba8);
                    generated = true;
                }
            }
        }

        if (!wasLoaded)
            asset.tryUnload();

        return generated;
    }

    void ThumbnailProvider::uploadThumbnail(Guid guid, std::span<const uint8_t> rgba8)
    {
        GpuThumbnail& entry = mGpuThumbnails[guid];
        entry.lastUsedFrame = mCurrentFrame;

        // 直接建 TextureProxy, 不经过 Texture2D——这里管理的纹理只上传一次、显示、
        // evict 时丢弃, 不需要 Texture2D 为"可编辑资产"提供的那套压缩存储 source pixels/
        // 离线烘焙质量 mip 生成/DDC 缓存语义, 那套语义本身才是之前 2~3 秒卡顿的根源。
        std::vector<uint8_t> extraMips = generateThumbnailMipChain<true>(rgba8, kMaxThumbnailEdge, kMaxThumbnailEdge, kThumbnailMipLevels);

        struct UploadMip
        {
            uint32_t level;
            std::vector<uint8_t> data;
        };
        std::vector<UploadMip> uploadMips;
        uploadMips.reserve(kThumbnailMipLevels);
        uploadMips.push_back(UploadMip{ 0, std::vector<uint8_t>(rgba8.begin(), rgba8.end()) });
        {
            uint32_t mipEdge = kMaxThumbnailEdge;
            size_t offset = 0;
            for (uint32_t level = 1; level < kThumbnailMipLevels; ++level)
            {
                mipEdge = std::max(mipEdge >> 1, 1u);
                size_t levelSize = static_cast<size_t>(mipEdge) * mipEdge * 4;
                uploadMips.push_back(UploadMip{ level, std::vector<uint8_t>(extraMips.begin() + offset, extraMips.begin() + offset + levelSize) });
                offset += levelSize;
            }
        }

        entry.proxyHandle = Context::get().getRenderFrontend().allocateProxyHandle<ProxyType::Texture>();
        TextureProxyHandle proxyHandle = entry.proxyHandle;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle, mips = std::move(uploadMips), handlePtr = &entry.handle](FrameContext& context) mutable
            {
                rhi::TextureDesc desc;
                desc.type = rhi::TextureType::Texture_2D;
                // encodeLinearToRGBA8() 已经把像素编码成直接显示语义的字节, 理由同 loadIconTexture()
                // 的注释——这里不需要、也不能再让 GPU 采样时做一次 sRGB 解码。
                desc.format = rhi::TextureFormat::RGBA8_UNORM;
                desc.extent = { kMaxThumbnailEdge, kMaxThumbnailEdge, 1 };
                desc.mipCount = kThumbnailMipLevels;
                desc.layerCount = 1;
                desc.usage = rhi::TextureUsage::Transfer_Dst | rhi::TextureUsage::Sampled;

                context.getProxyManager().createProxy(proxyHandle, desc);
                TextureProxy* proxy = context.getProxyManager().getProxy<ProxyType::Texture>(proxyHandle);
                for (auto& mip : mips)
                    proxy->stageUpload(std::move(mip.data), mip.level, 0, 1);
                handlePtr->update(proxy->getTextureView());
            }
        );
    }

    void ThumbnailProvider::destroyThumbnailProxy(TextureProxyHandle handle) noexcept
    {
        if (!handle.isValid())
            return;

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [handle](FrameContext& context)
            {
                context.getProxyManager().destroyProxy(handle);
            }
        );
        renderFrontend.freeProxyHandle(handle);
    }
}
