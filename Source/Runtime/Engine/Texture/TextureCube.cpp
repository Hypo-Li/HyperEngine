#include "TextureCube.h"

#include "../Context.h"
#include <Core/Math/Math.h>
#include <RenderBase/RenderSystem.h>
#include <RenderBase/TextureProxy.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <execution>
#include <span>

namespace hyp
{
    REGISTER_CLASS(TextureCube)

    TextureCube::TextureCube()
    {
    }

    TextureCube::~TextureCube() noexcept
    {
    }

    void TextureCube::initialize(
        uint32_t width,
        PixelFormat format,
        bool isSRGB,
        CompressionPreset compressionPreset,
        bool generateMips,
        uint32_t mipLevels
    )
    {
        Description description;
        description.format = format;
        description.isSRGB = isSRGB;
        description.compressionPreset = compressionPreset;
        description.generateMips = generateMips;
        description.width = width;
        description.height = width;
        description.depth = 1;
        uint32_t fullMipCount = static_cast<uint32_t>(std::floor(std::log2(width))) + 1;
        description.mipLevels = mipLevels == 0 ? fullMipCount : std::min(mipLevels, fullMipCount);
        description.arrayLayers = 6;
        description.isCube = true;
        setDescription(description);
    }

    void TextureCube::createTextureProxy(TextureProxyHandle handle)
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [handle,
            desc = getDescription()](FrameContext& context)
            {
                rhi::TextureDesc textureDesc;
                textureDesc.type = rhi::TextureType::Texture_Cube;
                textureDesc.format = convertToRHITextureFormat(desc.format, desc.compressionPreset, desc.compressionFormatFamily, desc.isSRGB);
                textureDesc.extent = { desc.width, desc.height, 1 };
                textureDesc.mipCount = desc.mipLevels;
                textureDesc.layerCount = desc.arrayLayers;
                textureDesc.usage = rhi::TextureUsage::Transfer_Src | rhi::TextureUsage::Transfer_Dst | rhi::TextureUsage::Sampled;

                context.getProxyManager().createProxy(handle, textureDesc);
            }
        );
    }

    template <> Type* Reflection::createType<TextureCube>()
    {
        auto c = new TClass<TextureCube, Texture>("TextureCube");
        return c;
    }

    float3 facePixelToDir(uint32_t face, uint32_t x, uint32_t y, uint32_t cubemapSize)
    {
        float2 st = (float2(float(x), float(y)) + 0.5f) / float(cubemapSize);
        float2 uv = 2.0f * st - 1.0f;

        float3 dir;
        switch (face)
        {
        case 0: dir = float3(1.0f, -uv.y, -uv.x); break; // +X
        case 1: dir = float3(-1.0f, -uv.y, uv.x); break; // -X
        case 2: dir = float3(uv.x, 1.0f, uv.y); break; // +Y
        case 3: dir = float3(uv.x, -1.0f, -uv.y); break; // -Y
        case 4: dir = float3(uv.x, -uv.y, 1.0f); break; // +Z
        case 5: dir = float3(-uv.x, -uv.y, -1.0f); break; // -Z
        default: assert(false); dir = float3(0.0f);
        }

        return Math::normalize(dir);
    }

    float2 dirToEquirectUV(const float3& dir)
    {
        // z-up to y-up
        float3 dirYUp = float3(dir.x, dir.z, -dir.y);
        float phi = std::atan2(-dirYUp.z, dirYUp.x);
        float theta = std::acos(std::clamp(dirYUp.y, -1.0f, 1.0f));

        float u = (phi + Math::PI) / (2.0f * Math::PI);
        float v = theta / Math::PI;

        return float2(u, v);
    }

    float3 sampleEquirectBilinear(
        std::span<const float3> equirectMapPixels,
        uint32_t width, uint32_t height,
        const float2& uv
    )
    {
        float px = uv.x * float(width);
        float py = uv.y * float(height);

        float x0f = std::floor(px - 0.5f);
        float y0f = std::floor(py - 0.5f);

        float tx = (px - 0.5f) - x0f;
        float ty = (py - 0.5f) - y0f;

        int x0 = int(x0f);
        int y0 = int(y0f);

        auto wrapX = [width](int x) { return ((x % int(width)) + int(width)) % int(width); };
        auto clampY = [height](int y) { return std::clamp(y, 0, int(height) - 1); };

        auto fetch = [&](int x, int y) -> const float3& {
            uint32_t idx = uint32_t(clampY(y)) * width + uint32_t(wrapX(x));
            return equirectMapPixels[idx];
            };

        const float3& c00 = fetch(x0, y0);
        const float3& c10 = fetch(x0 + 1, y0);
        const float3& c01 = fetch(x0, y0 + 1);
        const float3& c11 = fetch(x0 + 1, y0 + 1);

        float3 c0 = c00 * (1.0f - tx) + c10 * tx;
        float3 c1 = c01 * (1.0f - tx) + c11 * tx;

        return c0 * (1.0f - ty) + c1 * ty;
    }

    std::vector<float3> equirectMapToCubemap(
        std::span<const float3> equirectMapPixels,
        uint32_t width, uint32_t height,
        uint32_t cubemapSize,
        float maxLuminance
    )
    {
        assert(width == height * 2);
        assert(height > 0);
        assert(cubemapSize > 0);
        assert(equirectMapPixels.size() >= static_cast<size_t>(width) * height);

        const size_t totalPixels = static_cast<size_t>(6) * cubemapSize * cubemapSize;
        std::vector<float3> cubemapPixels(totalPixels);

        std::vector<uint32_t> indices(totalPixels);
        std::iota(indices.begin(), indices.end(), 0u);

        std::for_each(std::execution::par, indices.begin(), indices.end(),
            [&cubemapPixels, &equirectMapPixels,
            width, height, cubemapSize, maxLuminance](uint32_t index)
            {
                uint32_t face = index / (cubemapSize * cubemapSize);
                uint32_t rem = index % (cubemapSize * cubemapSize);
                uint32_t y = rem / cubemapSize;
                uint32_t x = rem % cubemapSize;

                float3 dir = facePixelToDir(face, x, y, cubemapSize);
                float2 uv = dirToEquirectUV(dir);
                cubemapPixels[index] = sampleEquirectBilinear(
                    equirectMapPixels, width, height, uv
                );
                cubemapPixels[index].r = std::min(cubemapPixels[index].r, maxLuminance);
                cubemapPixels[index].g = std::min(cubemapPixels[index].g, maxLuminance);
                cubemapPixels[index].b = std::min(cubemapPixels[index].b, maxLuminance);
            }
        );

        return cubemapPixels;
    }
}
