#pragma once
#include "Memory.h"

#include <Core/Common/HString.h>
#include <Core/Common/Refable.h>
#include <Core/Common/EnumBitwise.h>
#include <Core/Math/Vector.h>

namespace hyp::rhi
{
    enum class TextureFormat
    {
        Undefined,

        R8_UNORM,
        R8_SNORM,
        R8_UINT,
        R8_SINT,
        R8_SRGB,
        RG8_UNORM,
        RG8_SNORM,
        RG8_UINT,
        RG8_SINT,
        RG8_SRGB,
        // 没有 RGB8_*/BGR8_*(24bpp, 3 通道)——D3D12/WebGPU 没有对应的原生格式, Vulkan 虽然有
        // 但引擎的 Texture::convertToRHITextureFormat() 从不会真的产出这几个值(3 通道源数据
        // 会先在 bakeMips() 里 pad 成 4 通道再交给 RHI), 保留只是死代码, 已删除。3 通道的
        // *源像素数据* 描述仍然用 hyp::PixelFormat(Engine 层), 和这里是两回事, 不受影响
        RGBA8_UNORM,
        RGBA8_SNORM,
        RGBA8_UINT,
        RGBA8_SINT,
        RGBA8_SRGB,
        BGRA8_UNORM,
        BGRA8_SNORM,
        BGRA8_UINT,
        BGRA8_SINT,
        BGRA8_SRGB,

        A2RGB10_UNORM_PACK32,
        A2BGR10_UNORM_PACK32,

        B10G11R11_UFLOAT_PACK32,
        E5B9G9R9_UFLOAT_PACK32,

        R16_UNORM,
        R16_SNORM,
        R16_UINT,
        R16_SINT,
        R16_SFLOAT,
        RG16_UNORM,
        RG16_SNORM,
        RG16_UINT,
        RG16_SINT,
        RG16_SFLOAT,
        // 同上, 没有 RGB16_*(48bpp, 3 通道)
        RGBA16_UNORM,
        RGBA16_SNORM,
        RGBA16_UINT,
        RGBA16_SINT,
        RGBA16_SFLOAT,

        R32_UINT,
        R32_SINT,
        R32_SFLOAT,
        RG32_UINT,
        RG32_SINT,
        RG32_SFLOAT,
        // 同上, 没有 RGB32_*(96bpp, 3 通道)
        RGBA32_UINT,
        RGBA32_SINT,
        RGBA32_SFLOAT,

        D16_UNORM,
        D32_SFLOAT,
        S8_UINT,
        D16_UNORM_S8_UINT,
        D24_UNORM_S8_UINT,
        D32_SFLOAT_S8_UINT,

        BC1_RGB_UNORM,
        BC1_RGB_SRGB,
        BC1_RGBA_UNORM,
        BC1_RGBA_SRGB,
        BC2_UNORM,
        BC2_SRGB,
        BC3_UNORM,
        BC3_SRGB,
        BC4_UNORM,
        BC4_SNORM,
        BC5_UNORM,
        BC5_SNORM,
        BC6H_UFLOAT,
        BC6H_SFLOAT,
        BC7_UNORM,
        BC7_SRGB,
    };

    static constexpr uint32_t kWholeMipLevels = ~0u;
    static constexpr uint32_t kWholeArrayLayers = ~0u;

    enum class TextureViewType
    {
        Texture_View_2D,
        Texture_View_2D_Array,
        Texture_View_3D,
        Texture_View_Cube,
    };

    enum class TextureAspect
    {
        None = 0,
        Color = 1 << 0,
        Depth = 1 << 1,
        Stencil = 1 << 2,
    };
    ENABLE_ENUM_CLASS_BITWISE(TextureAspect)

    struct TextureRange
    {
        TextureAspect aspect;
        uint32_t baseMip;
        uint32_t mipCount;
        uint32_t baseLayer;
        uint32_t layerCount;
    };

    struct TextureSlice
    {
        TextureAspect aspect;
        uint32_t mipLevel;
        uint32_t baseLayer;
        uint32_t layerCount;
    };

    struct TextureViewDesc
    {
        TextureViewType type;
        TextureFormat format;
        TextureRange range;
    };

    class Texture;
    class TextureView : public Refable
    {
    public:
        TextureView(Texture& texture, const TextureViewDesc& desc) :
            mTexture(texture), mDesc(desc) {
        }

        virtual ~TextureView();

        Texture& getTexture() const noexcept
        {
            return mTexture;
        }

        const TextureViewDesc& getDesc() const noexcept
        {
            return mDesc;
        }

        virtual void setDebugName(HString name)
        {
            mDebugName = std::move(name);
        }

        HString getDebugName() const noexcept
        {
            return mDebugName;
        }

    protected:
        Texture& mTexture;
        TextureViewDesc mDesc;
        HString mDebugName;
    };

    enum class TextureType
    {
        Texture_2D,
        Texture_3D,
        Texture_Cube,
    };

    enum class TextureUsage
    {
        None = 0,
        Transfer_Src = 1 << 0,
        Transfer_Dst = 1 << 1,
        Sampled = 1 << 2,
        Storage = 1 << 3,
        Color_Attachment = 1 << 4,
        Depth_Stencil_Attachment = 1 << 5,
    };
    ENABLE_ENUM_CLASS_BITWISE(TextureUsage)

    struct TextureDesc
    {
        TextureType type = TextureType::Texture_2D;
        TextureFormat format = TextureFormat::Undefined;
        uint3 extent = { 0, 0, 0 };
        uint32_t mipCount = 1;
        uint32_t layerCount = 1;
        uint8_t samples = 1;
        TextureUsage usage = TextureUsage::None;
    };

    class Device;
    class Texture : public Refable
    {
        friend class TextureView;
    public:
        Texture(const Device& device, const TextureDesc& desc, MemoryType memoryType) :
            mDevice(device), mDesc(desc), mMemoryType(memoryType) {
        }

        virtual ~Texture() = default;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        const TextureDesc& getDesc() const noexcept
        {
            return mDesc;
        }

        MemoryType getMemoryType() const noexcept
        {
            return mMemoryType;
        }

        virtual TRef<TextureView> createView(const TextureViewDesc& desc) = 0;

        virtual void setDebugName(HString name)
        {
            mDebugName = std::move(name);
        }

        HString getDebugName() const noexcept
        {
            return mDebugName;
        }

        uint32_t getViewCount() const noexcept
        {
            return mViewCount;
        }

    protected:
        void incViewCount() noexcept
        {
            ++mViewCount;
        }

        void decViewCount() noexcept
        {
            --mViewCount;
        }

    protected:
        const Device& mDevice;
        TextureDesc mDesc;
        MemoryType mMemoryType;
        uint32_t mViewCount = 0;
        HString mDebugName;
    };

    inline bool isDepthStencilFormat(TextureFormat format)
    {
        return format == TextureFormat::D16_UNORM_S8_UINT ||
            format == TextureFormat::D24_UNORM_S8_UINT ||
            format == TextureFormat::D32_SFLOAT_S8_UINT;
    }

    inline bool isDepthFormat(TextureFormat format)
    {
        return format == TextureFormat::D16_UNORM ||
            format == TextureFormat::D32_SFLOAT;
    }

    inline bool isStencilFormat(TextureFormat format)
    {
        return format == TextureFormat::S8_UINT;
    }

    TextureAspect getFormatAspect(TextureFormat format);

    TextureViewDesc makeTextureDefaultViewDesc(const TextureDesc& textureDesc);

    struct TextureFormatBlockInfo
    {
        uint32_t blockWidth;
        uint32_t blockHeight;
        uint32_t blockSizeInBytes;
    };

    TextureFormatBlockInfo getTextureFormatBlockInfo(rhi::TextureFormat format) noexcept;
}
