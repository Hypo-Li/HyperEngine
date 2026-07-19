#pragma once
#include "ShaderModule.h"
#include "Buffer.h"
#include "Texture.h"
#include "Sampler.h"

#include <Core/Common/Refable.h>

namespace hyp::rhi
{
    enum class BindingType
    {
        Uniform_Buffer,
        Storage_Buffer,
        Dynamic_Uniform_Buffer,
        Dynamic_Storage_Buffer,
        Sampled_Texture,
        Storage_Texture,
        Sampler,
    };

    enum class BindingAccess
    {
        Read_Only = 1 << 0,
        Write_Only = 1 << 1,
        Read_Write = Read_Only | Write_Only,
    };
    ENABLE_ENUM_CLASS_BITWISE(BindingAccess)

    enum class TextureSampleType
    {
        Float,
        Uint,
        Sint,
        Depth,
    };

    enum class SamplerBindingType
    {
        Filtering,
        Comparison,
    };

    struct BindGroupLayoutEntry
    {
        uint32_t binding = 0;
        BindingType type = BindingType::Uniform_Buffer;
        BindingAccess access = BindingAccess::Read_Write;
        uint32_t count = 0;

        TextureViewType textureViewType = TextureViewType::Texture_View_2D;
        TextureFormat storageTextureFormat = TextureFormat::Undefined;
        TextureSampleType textureSampleType = TextureSampleType::Float;
        bool multiSample = false;

        SamplerBindingType samplerBindingType = SamplerBindingType::Filtering;

        ShaderStage stageFlags = ShaderStage::None;
        bool bindless = false;

        bool operator==(const BindGroupLayoutEntry& other) const noexcept
        {
            return binding == other.binding &&
                type == other.type &&
                access == other.access &&
                count == other.count &&
                textureViewType == other.textureViewType &&
                storageTextureFormat == other.storageTextureFormat &&
                textureSampleType == other.textureSampleType &&
                multiSample == other.multiSample &&
                samplerBindingType == other.samplerBindingType &&
                stageFlags == other.stageFlags &&
                bindless == other.bindless;
        }
    };

    // 构建期 CompilePassShader.cmake 从 slangc -reflection-json 解析出来的单个 binding 描述,
    // 直接对应 .slang 源码里的 [[vk::binding(N, 2)]] 声明——生成头文件里以 constexpr 数组的
    // 形式写出一份 PassShaderBindingDesc[], 取代过去每个 Pass 手写 BindGroupLayoutEntry 数组
    // (两边手动保持同步、容易和 shader 实际声明错配)的做法。纯 POD, 只依赖 <cstdint>, 保证
    // 能在生成头文件里 constexpr 初始化(不能用 HString, 依赖太重), name 只用来做按名字查找,
    // 不参与运行时 BindGroupLayout 本身的创建。
    struct PassShaderBindingDesc
    {
        const char* name = nullptr;
        uint32_t binding = 0;
        BindingType type = BindingType::Uniform_Buffer;
        BindingAccess access = BindingAccess::Read_Write;
        uint32_t count = 1;
        TextureViewType textureViewType = TextureViewType::Texture_View_2D;
        TextureFormat storageTextureFormat = TextureFormat::Undefined;
        TextureSampleType textureSampleType = TextureSampleType::Float;
        SamplerBindingType samplerBindingType = SamplerBindingType::Filtering;
        ShaderStage stageFlags = ShaderStage::None;

        constexpr BindGroupLayoutEntry toBindGroupLayoutEntry() const noexcept
        {
            BindGroupLayoutEntry entry;
            entry.binding = binding;
            entry.type = type;
            entry.access = access;
            entry.count = count;
            entry.textureViewType = textureViewType;
            entry.storageTextureFormat = storageTextureFormat;
            entry.textureSampleType = textureSampleType;
            entry.samplerBindingType = samplerBindingType;
            entry.stageFlags = stageFlags;
            return entry;
        }
    };

    class BindGroupLayout : public Refable
    {
    public:
        BindGroupLayout(Device& device, std::span<const BindGroupLayoutEntry> entries)
            : mDevice(device), mEntries(entries.begin(), entries.end())
        {
        }

        virtual ~BindGroupLayout() = default;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        std::span<const BindGroupLayoutEntry> getEntries() const noexcept
        {
            return mEntries;
        }

    private:
        const Device& mDevice;
        std::vector<BindGroupLayoutEntry> mEntries;
    };

    struct BufferBindingInfo
    {
        const Buffer* buffer = nullptr;
        uint64_t offset = 0;
        uint64_t range = 0;
    };

    struct BindGroupEntry
    {
        uint32_t binding = 0;
        BindingType type = BindingType::Uniform_Buffer;
        uint32_t index = 0;
        std::span<const BufferBindingInfo> buffers;
        std::span<const TextureView* const> textures;
        std::span<const Sampler* const> samplers;
    };

    class BindGroup : public Refable
    {
    public:
        BindGroup(const Device& device, const BindGroupLayout& layout) :
            mDevice(device), mLayout(layout)
        {
        }

        virtual ~BindGroup() = default;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        const BindGroupLayout& getLayout() const noexcept
        {
            return mLayout;
        }

        virtual void update(std::span<const BindGroupEntry> entries) = 0;

    private:
        const Device& mDevice;
        const BindGroupLayout& mLayout;
    };
}
