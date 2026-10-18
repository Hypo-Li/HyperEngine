#pragma once

#include "MipmapFilter.h"

#include <Engine/Texture/Texture.h>

#include <Core/Common/Float16.h>
#include <Core/Math/Vector.h>

#include <algorithm>
#include <cassert>
#include <span>
#include <vector>
#include <execution>
#include <ranges>

namespace hyp
{
    template <typename T>
    void resample1D(
        const T* src,
        uint32_t srcSize,
        T* dst,
        uint32_t dstSize,
        const FilterKernel& kernel,
        uint32_t stride)
    {
        if (dstSize == 0 || srcSize == 0) return;

        // 尺寸不变时直接拷贝
        if (dstSize == srcSize)
        {
            for (uint32_t i = 0; i < dstSize; ++i)
            {
                dst[i * stride] = src[i * stride];
            }
            return;
        }

        // 1. 计算缩放参数
        //    - fnsource: 源尺寸浮点值
        //    - minifying: 是否缩小（dstSize < srcSize）
        //    - domainScale: 用于把源/目标空间统一到一个归一化域
        //      放大时用源尺寸，缩小时用目标尺寸（Filament 做法）
        //    - filterBounds: filter kernel 在归一化域中的半宽
        const float fnsource = static_cast<float>(srcSize);
        const bool minifying = static_cast<float>(dstSize) < fnsource;
        const float domainScale = minifying ? static_cast<float>(dstSize) : fnsource;
        const float filterBounds = kernel.radius / domainScale;

        // 2. 遍历每个目标像素
        //    xtarget 表示目标像素中心在 [0,1] 域中的位置
        const float dtarget = 1.0f / dstSize;
        float xtarget = dtarget * 0.5f;

        for (uint32_t itarget = 0; itarget < dstSize; ++itarget, xtarget += dtarget)
        {
            // 3. 确定需要扫描的源像素范围
            const int32_t isourceLower = static_cast<int32_t>((xtarget - filterBounds) * srcSize);
            const int32_t isourceUpper = static_cast<int32_t>(std::ceil((xtarget + filterBounds) * srcSize));

            T valueSum = T(0);
            float weightSum = 0.0f;

            // 4. 在源像素窗口内累加加权样本
            for (int32_t isource = isourceLower; isource <= isourceUpper; ++isource)
            {
                const float xsource = (static_cast<float>(isource) + 0.5f) / static_cast<float>(srcSize);
                const float t = domainScale * std::abs(xsource - xtarget);
                const float weight = kernel.weight(t);
                if (weight == 0.0f) continue;

                const int32_t clampedIndex = std::clamp(isource, 0, static_cast<int32_t>(srcSize) - 1);
                valueSum = valueSum + src[clampedIndex * stride] * weight;
                weightSum += weight;
            }

            // 5. 归一化并写出结果
            if (weightSum > 0.0f)
            {
                dst[itarget * stride] = valueSum * (1.0f / weightSum);
            }
            else
            {
                const int32_t nearestIndex = std::clamp(
                    static_cast<int32_t>(xtarget * srcSize),
                    0, static_cast<int32_t>(srcSize) - 1
                );
                dst[itarget * stride] = src[nearestIndex * stride];
            }
        }
    }

    template <typename It, typename Fn>
    void forEach(bool parallel, It first, It last, Fn&& fn)
    {
        // par_unseq: 每次迭代只读共享的 kernel/src、只写各自独立的 dst 下标, 满足 unsequenced 的
        // 无副作用要求, 给向量化留出空间
        if (parallel)
            std::for_each(std::execution::par_unseq, first, last, std::forward<Fn>(fn));
        else
            std::for_each(std::execution::seq, first, last, std::forward<Fn>(fn));
    }

    template <typename T>
    void resampleImage2D(
        std::span<const T> srcData,
        uint32_t srcWidth,
        uint32_t srcHeight,
        std::span<T> dstData,
        uint32_t dstWidth,
        uint32_t dstHeight,
        MipmapFilter filter
    )
    {
        const uint64_t srcExpected = static_cast<uint64_t>(srcWidth) * srcHeight;
        const uint64_t dstExpected = static_cast<uint64_t>(dstWidth) * dstHeight;
        assert(srcData.size() == srcExpected);
        assert(dstData.size() == dstExpected);

        if (srcWidth == 0 || srcHeight == 0 || dstWidth == 0 || dstHeight == 0)
        {
            return;
        }

        const FilterKernel kernel = getFilterKernel(filter);
        std::vector<T> temp(static_cast<size_t>(dstWidth) * srcHeight);

        auto srcHeightRange = std::views::iota(0u, srcHeight);
        forEach(
            srcHeight > 64,
            srcHeightRange.begin(),
            srcHeightRange.end(),
            [&](uint32_t y) {
                resample1D(
                    srcData.data() + y * srcWidth,
                    srcWidth,
                    temp.data() + y * dstWidth,
                    dstWidth,
                    kernel,
                    1
                );
            }
        );

        auto dstWidthRange = std::views::iota(0u, dstWidth);
        forEach(
            dstWidth > 64,
            dstWidthRange.begin(),
            dstWidthRange.end(),
            [&](uint32_t x) {
                resample1D(
                    temp.data() + x,
                    srcHeight,
                    dstData.data() + x,
                    dstHeight,
                    kernel,
                    dstWidth
                );
            }
        );
    }

    template <typename T>
    void resampleImage3D(
        std::span<const T> srcData,
        uint32_t srcWidth,
        uint32_t srcHeight,
        uint32_t srcDepth,
        std::span<T> dstData,
        uint32_t dstWidth,
        uint32_t dstHeight,
        uint32_t dstDepth,
        MipmapFilter filter
    )
    {
        const uint64_t srcExpected = static_cast<uint64_t>(srcWidth) * srcHeight * srcDepth;
        const uint64_t dstExpected = static_cast<uint64_t>(dstWidth) * dstHeight * dstDepth;
        assert(srcData.size() == srcExpected);
        assert(dstData.size() == dstExpected);

        if (srcWidth == 0 || srcHeight == 0 || srcDepth == 0 || dstWidth == 0 || dstHeight == 0 || dstDepth == 0)
        {
            return;
        }

        const FilterKernel kernel = getFilterKernel(filter);

        const size_t srcSliceSize = static_cast<size_t>(srcWidth) * srcHeight;
        const size_t dstSliceSize = static_cast<size_t>(dstWidth) * dstHeight;

        std::vector<T> tempX(static_cast<size_t>(dstWidth) * srcHeight * srcDepth);
        auto srcDepthRange = std::views::iota(0u, srcDepth);
        forEach(
            srcDepth > 64,
            srcDepthRange.begin(),
            srcDepthRange.end(),
            [&](uint32_t z) {
                for (uint32_t y = 0; y < srcHeight; ++y)
                {
                    resample1D(
                        srcData.data() + z * srcSliceSize + y * srcWidth,
                        srcWidth,
                        tempX.data() + z * (dstWidth * srcHeight) + y * dstWidth,
                        dstWidth,
                        kernel,
                        1
                    );
                }
            }
        );

        std::vector<T> tempY(static_cast<size_t>(dstWidth) * dstHeight * srcDepth);
        forEach(
            srcDepth > 64,
            srcDepthRange.begin(),
            srcDepthRange.end(),
            [&](uint32_t z) {
                for (uint32_t x = 0; x < dstWidth; ++x)
                {
                    resample1D(
                        tempX.data() + z * (dstWidth * srcHeight) + x,
                        srcHeight,
                        tempY.data() + z * dstSliceSize + x,
                        dstHeight,
                        kernel,
                        dstWidth
                    );
                }
            }
        );

        auto dstHeightRange = std::views::iota(0u, dstHeight);
        forEach(
            dstHeight > 64,
            dstHeightRange.begin(),
            dstHeightRange.end(),
            [&](uint32_t y) {
                for (uint32_t x = 0; x < dstWidth; ++x)
                {
                    resample1D(
                        tempY.data() + y * dstWidth + x,
                        srcDepth,
                        dstData.data() + y * dstWidth + x,
                        dstDepth,
                        kernel,
                        static_cast<uint32_t>(dstSliceSize)
                    );
                }
            }
        );
    }

    template <typename PixelType, typename DecodeFunc, typename EncodeFunc>
    void generateMipmap(
        uint32_t srcWidth,
        uint32_t srcHeight,
        uint32_t srcDepth,
        std::span<const PixelType> srcData,
        std::span<PixelType> dstMips,
        uint32_t mipCount,
        MipmapFilter filter,
        DecodeFunc&& decode,
        EncodeFunc&& encode
    )
    {
        using DecodedType = std::invoke_result_t<DecodeFunc, PixelType>;

        const uint64_t srcExpected = static_cast<uint64_t>(srcWidth) * srcHeight * srcDepth;
        assert(srcData.size() == srcExpected);

        if (srcWidth == 0 || srcHeight == 0 || srcDepth == 0 || mipCount == 0)
        {
            return;
        }

        std::vector<DecodedType> curData(srcData.size());
        for (size_t i = 0; i < srcData.size(); ++i)
        {
            curData[i] = decode(srcData[i]);
        }
        uint32_t curWidth = srcWidth;
        uint32_t curHeight = srcHeight;
        uint32_t curDepth = srcDepth;

        uint32_t mipWidth = srcWidth;
        uint32_t mipHeight = srcHeight;
        uint32_t mipDepth = srcDepth;
        size_t dstOffset = 0;

        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            mipWidth = std::max(mipWidth >> 1u, 1u);
            mipHeight = std::max(mipHeight >> 1u, 1u);
            mipDepth = std::max(mipDepth >> 1u, 1u);

            const size_t mipSize = static_cast<size_t>(mipWidth) * mipHeight * mipDepth;
            assert(dstOffset + mipSize <= dstMips.size());

            std::span<PixelType> dstMip(dstMips.data() + dstOffset, mipSize);
            std::vector<DecodedType> linearDst(mipSize);

            if (curDepth == 1)
            {
                resampleImage2D(
                    std::span<const DecodedType>(curData.data(), curData.size()),
                    curWidth, curHeight,
                    std::span<DecodedType>(linearDst.data(), linearDst.size()),
                    mipWidth, mipHeight,
                    filter
                );
            }
            else
            {
                resampleImage3D(
                    std::span<const DecodedType>(curData.data(), curData.size()),
                    curWidth, curHeight, curDepth,
                    std::span<DecodedType>(linearDst.data(), linearDst.size()),
                    mipWidth, mipHeight, mipDepth,
                    filter
                );
            }

            for (size_t i = 0; i < mipSize; ++i)
            {
                dstMip[i] = encode(linearDst[i]);
            }

            dstOffset += mipSize;
            curData = std::move(linearDst);
            curWidth = mipWidth;
            curHeight = mipHeight;
            curDepth = mipDepth;
        }
    }

    enum class CubeFace : uint8_t
    {
        PX = 0, // +X
        NX,     // -X
        PY,     // +Y
        NY,     // -Y
        PZ,     // +Z
        NZ      // -Z
    };

    struct CubeAddress
    {
        CubeFace face;
        float s = 0.0f; // [0, 1]
        float t = 0.0f; // [0, 1]
    };

    float3 cubeGetDirection(CubeFace face, float x, float y, uint32_t dim);

    CubeAddress cubeGetAddressFor(const float3& r);

    template <typename T>
    T sampleCubemap(
        const std::array<std::span<const T>, 6>& faces,
        uint32_t srcDim,
        const float3& direction
    )
    {
        CubeAddress addr = cubeGetAddressFor(direction);
        const std::span<const T>& face = faces[static_cast<size_t>(addr.face)];

        // 1. 标准映射：将 [0, 1] 映射到 [-0.5, srcDim - 0.5]
        float x = addr.s * static_cast<float>(srcDim) - 0.5f;
        float y = addr.t * static_cast<float>(srcDim) - 0.5f;

        // 2. 取左上角像素索引
        int32_t x0 = static_cast<int32_t>(std::floor(x));
        int32_t y0 = static_cast<int32_t>(std::floor(y));

        // 3. 计算插值权重
        float u = x - static_cast<float>(x0);
        float v = y - static_cast<float>(y0);

        // 4. 边界处理：如果不做跨面采样，则 Clamp 到边缘
        auto clampCoord = [&](int32_t c) {
            return static_cast<uint32_t>(std::max(0, std::min(c, static_cast<int32_t>(srcDim) - 1)));
            };

        uint32_t ix0 = clampCoord(x0);
        uint32_t ix1 = clampCoord(x0 + 1);
        uint32_t iy0 = clampCoord(y0);
        uint32_t iy1 = clampCoord(y0 + 1);

        // 5. 采样
        const T& c00 = face[iy0 * srcDim + ix0];
        const T& c10 = face[iy0 * srcDim + ix1];
        const T& c01 = face[iy1 * srcDim + ix0];
        const T& c11 = face[iy1 * srcDim + ix1];

        return c00 * ((1.0f - u) * (1.0f - v)) +
            c10 * (u * (1.0f - v)) +
            c01 * ((1.0f - u) * v) +
            c11 * (u * v);
    }

    template <typename T>
    T sampleCubemapFiltered(
        const std::array<std::span<const T>, 6>& faces,
        uint32_t srcDim,
        uint32_t dstDim,
        const float3& dir,
        const FilterKernel& kernel)
    {
        float3 up = std::abs(dir.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
        float3 right = glm::normalize(glm::cross(up, dir));
        float3 tangent = glm::cross(dir, right);

        // 目标一个像素对应源多少个像素的角度
        const float texelAngle = 2.0f / static_cast<float>(srcDim);

        T valueSum = T(0);
        float weightSum = 0.0f;

        const int sampleRadius = static_cast<int>(std::ceil(kernel.radius));

        for (int j = -sampleRadius; j <= sampleRadius; ++j)
        {
            for (int i = -sampleRadius; i <= sampleRadius; ++i)
            {
                float offsetX = i * texelAngle;
                float offsetY = j * texelAngle;

                float3 sampleDir = glm::normalize(dir + right * offsetX + tangent * offsetY);

                float t = std::sqrt(offsetX * offsetX + offsetY * offsetY);
                float weight = kernel.weight(t);
                if (weight == 0.0f) continue;

                valueSum = valueSum + sampleCubemap(faces, srcDim, sampleDir) * weight;
                weightSum += weight;
            }
        }

        if (weightSum > 0.0f)
        {
            return valueSum * (1.0f / weightSum);
        }

        return sampleCubemap(faces, srcDim, dir);
    }

    template <typename PixelType, typename DecodeFunc, typename EncodeFunc>
    void generateMipmapCube(
        uint32_t srcWidth,
        uint32_t srcHeight,
        std::span<const PixelType> srcData,
        std::span<PixelType> dstMips,
        uint32_t mipCount,
        MipmapFilter filter,
        DecodeFunc&& decode,
        EncodeFunc&& encode
    )
    {
        using DecodedType = std::invoke_result_t<DecodeFunc, PixelType>;

        const uint32_t faceSize = srcWidth * srcHeight;
        assert(srcData.size() == static_cast<size_t>(faceSize) * 6);
        assert(srcWidth == srcHeight);

        // 1. 解码6个face
        std::vector<DecodedType> prevFaces[6];
        for (uint32_t f = 0; f < 6; ++f)
        {
            prevFaces[f].resize(faceSize);
            const PixelType* faceSrc = srcData.data() + f * faceSize;
            for (uint32_t i = 0; i < faceSize; ++i)
            {
                prevFaces[f][i] = decode(faceSrc[i]);
            }
        }

        const FilterKernel kernel = getFilterKernel(filter);

        uint32_t mipWidth = srcWidth;
        uint32_t mipHeight = srcHeight;
        size_t dstOffset = 0;

        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            mipWidth = std::max(mipWidth >> 1u, 1u);
            mipHeight = std::max(mipHeight >> 1u, 1u);
            const uint32_t mipFaceSize = mipWidth * mipHeight;

            std::vector<DecodedType> currentFaces[6];
            for (uint32_t f = 0; f < 6; ++f)
            {
                currentFaces[f].resize(mipFaceSize);
            }

            for (uint32_t f = 0; f < 6; ++f)
            {
                std::array<std::span<const DecodedType>, 6> faceSpans;
                for (uint32_t i = 0; i < 6; ++i)
                {
                    faceSpans[i] = std::span<const DecodedType>(prevFaces[i].data(), prevFaces[i].size());
                }

                auto mipHeightRange = std::views::iota(0u, mipHeight);
                forEach(
                    mipHeight > 32,
                    mipHeightRange.begin(),
                    mipHeightRange.end(),
                    [&](uint32_t y)
                    {
                        for (uint32_t x = 0; x < mipWidth; ++x)
                        {
                            float3 dir = cubeGetDirection(static_cast<CubeFace>(f), x + 0.5f, y + 0.5f, mipWidth);
                            uint32_t prevWidth = std::max(srcWidth >> mip, 1u);
                            currentFaces[f][y * mipWidth + x] = sampleCubemapFiltered(faceSpans, prevWidth, mipWidth, dir, kernel);
                        }
                    }
                );
            }

            // 编码并写入dstMips
            for (uint32_t f = 0; f < 6; ++f)
            {
                assert(dstOffset + mipFaceSize <= dstMips.size());
                std::span<PixelType> dstFace(dstMips.data() + dstOffset, mipFaceSize);
                for (uint32_t i = 0; i < mipFaceSize; ++i)
                {
                    dstFace[i] = encode(currentFaces[f][i]);
                }
                dstOffset += mipFaceSize;
            }

            // 链式：当前层作为下一层源
            for (uint32_t f = 0; f < 6; ++f)
            {
                prevFaces[f] = std::move(currentFaces[f]);
            }
        }
    }

    class ColorSpace
    {
    public:
        static float srgbToLinear(uint8_t v);

        static uint8_t linearToSrgb(float v);
    };

    template <PixelFormat F, bool isSRGB = false> struct PixelFormatTraits;

    template <> struct PixelFormatTraits<PixelFormat::R8_UNORM, true>
    {
        using Type = uint8_t;

        static auto getDecode()
        {
            return [](uint8_t v) -> float { return ColorSpace::srgbToLinear(v); };
        }

        static auto getEncode()
        {
            return [](float v) -> uint8_t { return ColorSpace::linearToSrgb(v); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::R8_UNORM, false>
    {
        using Type = uint8_t;

        static auto getDecode()
        {
            return [](uint8_t v) -> float { return v / 255.0f; };
        }

        static auto getEncode()
        {
            return [](float v) -> uint8_t { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RG8_UNORM, true>
    {
        using Type = TVector2<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float2 { return float2(ColorSpace::srgbToLinear(v.x), ColorSpace::srgbToLinear(v.y)); };
        }

        static auto getEncode()
        {
            return [](float2 v) -> Type { return Type(ColorSpace::linearToSrgb(v.x), ColorSpace::linearToSrgb(v.y)); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RG8_UNORM, false>
    {
        using Type = TVector2<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float2 { return float2(v.x / 255.0f, v.y / 255.0f); };
        }

        static auto getEncode()
        {
            return [](float2 v) -> Type {
                return Type(
                    static_cast<uint8_t>(std::clamp(v.x, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.y, 0.0f, 1.0f) * 255.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGB8_UNORM, true>
    {
        using Type = TVector3<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float3 { return float3(ColorSpace::srgbToLinear(v.x), ColorSpace::srgbToLinear(v.y), ColorSpace::srgbToLinear(v.z)); };
        }

        static auto getEncode()
        {
            return [](float3 v) -> Type { return Type(ColorSpace::linearToSrgb(v.x), ColorSpace::linearToSrgb(v.y), ColorSpace::linearToSrgb(v.z)); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGB8_UNORM, false>
    {
        using Type = TVector3<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float3 { return float3(v.x / 255.0f, v.y / 255.0f, v.z / 255.0f); };
        }

        static auto getEncode()
        {
            return [](float3 v) -> Type {
                return Type(
                    static_cast<uint8_t>(std::clamp(v.x, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.y, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.z, 0.0f, 1.0f) * 255.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGBA8_UNORM, true>
    {
        using Type = TVector4<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float4 {
                return float4(
                    ColorSpace::srgbToLinear(v.x),
                    ColorSpace::srgbToLinear(v.y),
                    ColorSpace::srgbToLinear(v.z),
                    v.w / 255.0f
                );
            };
        }

        static auto getEncode()
        {
            return [](float4 v) -> Type {
                return Type(
                    ColorSpace::linearToSrgb(v.x),
                    ColorSpace::linearToSrgb(v.y),
                    ColorSpace::linearToSrgb(v.z),
                    static_cast<uint8_t>(std::clamp(v.w, 0.0f, 1.0f) * 255.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGBA8_UNORM, false>
    {
        using Type = TVector4<uint8_t>;

        static auto getDecode()
        {
            return [](Type v) -> float4 { return float4(v.x / 255.0f, v.y / 255.0f, v.z / 255.0f, v.w / 255.0f); };
        }

        static auto getEncode()
        {
            return [](float4 v) -> Type {
                return Type(
                    static_cast<uint8_t>(std::clamp(v.x, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.y, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.z, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<uint8_t>(std::clamp(v.w, 0.0f, 1.0f) * 255.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::R16_UNORM, false>
    {
        using Type = uint16_t;

        static auto getDecode()
        {
            return [](uint16_t v) -> float { return v / 65535.0f; };
        }

        static auto getEncode()
        {
            return [](float v) -> uint16_t { return static_cast<uint16_t>(std::clamp(v, 0.0f, 1.0f) * 65535.0f + 0.5f); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RG16_UNORM, false>
    {
        using Type = TVector2<uint16_t>;

        static auto getDecode()
        {
            return [](Type v) -> float2 { return float2(v.x / 65535.0f, v.y / 65535.0f); };
        }

        static auto getEncode()
        {
            return [](float2 v) -> Type {
                return Type(
                    static_cast<uint16_t>(std::clamp(v.x, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint16_t>(std::clamp(v.y, 0.0f, 1.0f) * 65535.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGB16_UNORM, false>
    {
        using Type = TVector3<uint16_t>;

        static auto getDecode()
        {
            return [](Type v) -> float3 { return float3(v.x / 65535.0f, v.y / 65535.0f, v.z / 65535.0f); };
        }

        static auto getEncode()
        {
            return [](float3 v) -> Type {
                return Type(
                    static_cast<uint16_t>(std::clamp(v.x, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint16_t>(std::clamp(v.y, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint16_t>(std::clamp(v.z, 0.0f, 1.0f) * 65535.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGBA16_UNORM, false>
    {
        using Type = TVector4<uint16_t>;

        static auto getDecode()
        {
            return [](Type v) -> float4 { return float4(v.x / 65535.0f, v.y / 65535.0f, v.z / 65535.0f, v.w / 65535.0f); };
        }

        static auto getEncode()
        {
            return [](float4 v) -> Type {
                return Type(
                    static_cast<uint16_t>(std::clamp(v.x, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint16_t>(std::clamp(v.y, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint16_t>(std::clamp(v.z, 0.0f, 1.0f) * 65535.0f + 0.5f),
                    static_cast<uint16_t>(std::clamp(v.w, 0.0f, 1.0f) * 65535.0f + 0.5f)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::R16_SFLOAT, false>
    {
        using Type = float16;

        static auto getDecode()
        {
            return [](float16 v) -> float { return float16ToFloat32(v); };
        }

        static auto getEncode()
        {
            return [](float v) -> float16 { return float32ToFloat16(v); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RG16_SFLOAT, false>
    {
        using Type = TVector2<float16>;

        static auto getDecode()
        {
            return [](Type v) -> float2 { return float2(float16ToFloat32(v.x), float16ToFloat32(v.y)); };
        }

        static auto getEncode()
        {
            return [](float2 v) -> Type { return Type(float32ToFloat16(v.x), float32ToFloat16(v.y)); };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGB16_SFLOAT, false>
    {
        using Type = TVector3<float16>;

        static auto getDecode()
        {
            return [](Type v) -> float3 {
                return float3(
                    float16ToFloat32(v.x),
                    float16ToFloat32(v.y),
                    float16ToFloat32(v.z)
                );
            };
        }

        static auto getEncode()
        {
            return [](float3 v) -> Type {
                return Type(
                    float32ToFloat16(v.x),
                    float32ToFloat16(v.y),
                    float32ToFloat16(v.z)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGBA16_SFLOAT, false>
    {
        using Type = TVector4<float16>;

        static auto getDecode()
        {
            return [](Type v) -> float4 {
                return float4(
                    float16ToFloat32(v.x),
                    float16ToFloat32(v.y),
                    float16ToFloat32(v.z),
                    float16ToFloat32(v.w)
                );
            };
        }

        static auto getEncode()
        {
            return [](float4 v) -> Type {
                return Type(
                    float32ToFloat16(v.x),
                    float32ToFloat16(v.y),
                    float32ToFloat16(v.z),
                    float32ToFloat16(v.w)
                );
            };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::R32_SFLOAT, false>
    {
        using Type = float;

        static auto getDecode()
        {
            return [](float v) -> float { return v; };
        }

        static auto getEncode()
        {
            return [](float v) -> float { return v; };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RG32_SFLOAT, false>
    {
        using Type = float2;

        static auto getDecode()
        {
            return [](float2 v) -> float2 { return v; };
        }

        static auto getEncode()
        {
            return [](float2 v) -> float2 { return v; };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGB32_SFLOAT, false>
    {
        using Type = float3;

        static auto getDecode()
        {
            return [](float3 v) -> float3 { return v; };
        }

        static auto getEncode()
        {
            return [](float3 v) -> float3 { return v; };
        }
    };

    template <> struct PixelFormatTraits<PixelFormat::RGBA32_SFLOAT, false>
    {
        using Type = float4;

        static auto getDecode()
        {
            return [](float4 v) -> float4 { return v; };
        }

        static auto getEncode()
        {
            return [](float4 v) -> float4 { return v; };
        }
    };

    template <PixelFormat F, bool isSRGB>
    void invokeGenerate(
        uint32_t srcWidth,
        uint32_t srcHeight,
        uint32_t srcDepth,
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

        generateMipmap<T>(
            srcWidth, srcHeight, srcDepth,
            typedSrc, typedDst, mipCount,
            filter,
            Traits::getDecode(),
            Traits::getEncode()
        );
    }
}
