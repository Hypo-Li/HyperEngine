#include "BindGroup.h"

#include "Buffer.h"
#include "Sampler.h"
#include "Texture.h"
#include "WgpuFormatUtil.h"

#include <Core/Common/LogSystem.h>

#include <cassert>
#include <limits>
#include <vector>

namespace hyp::wgpu
{
    static WGPUShaderStage convertShaderStage(rhi::ShaderStage stage) noexcept
    {
        WGPUShaderStage result = WGPUShaderStage_None;
        if (enumHasAnyFlags(stage, rhi::ShaderStage::Vertex))
            result |= WGPUShaderStage_Vertex;
        if (enumHasAnyFlags(stage, rhi::ShaderStage::Fragment))
            result |= WGPUShaderStage_Fragment;
        if (enumHasAnyFlags(stage, rhi::ShaderStage::Compute))
            result |= WGPUShaderStage_Compute;
        return result;
    }

    static WGPUTextureSampleType convertTextureSampleType(rhi::TextureSampleType sampleType) noexcept
    {
        switch (sampleType)
        {
        case rhi::TextureSampleType::Float:
            return WGPUTextureSampleType_Float;
        case rhi::TextureSampleType::Uint:
            return WGPUTextureSampleType_Uint;
        case rhi::TextureSampleType::Sint:
            return WGPUTextureSampleType_Sint;
        case rhi::TextureSampleType::Depth:
            return WGPUTextureSampleType_Depth;
        default:
            return WGPUTextureSampleType_Undefined;
        }
    }

    static WGPUTextureViewDimension convertTextureViewDimension(rhi::TextureViewType type) noexcept
    {
        switch (type)
        {
        case rhi::TextureViewType::Texture_View_2D:       return WGPUTextureViewDimension_2D;
        case rhi::TextureViewType::Texture_View_2D_Array: return WGPUTextureViewDimension_2DArray;
        case rhi::TextureViewType::Texture_View_3D:       return WGPUTextureViewDimension_3D;
        case rhi::TextureViewType::Texture_View_Cube:     return WGPUTextureViewDimension_Cube;
        default:                                          return WGPUTextureViewDimension_2D;
        }
    }

    static WGPUStorageTextureAccess convertStorageTextureAccess(rhi::BindingAccess access) noexcept
    {
        if (access == rhi::BindingAccess::Read_Only)
            return WGPUStorageTextureAccess_ReadOnly;
        else if (access == rhi::BindingAccess::Write_Only)
            return WGPUStorageTextureAccess_WriteOnly;
        else if (access == rhi::BindingAccess::Read_Write)
            return WGPUStorageTextureAccess_ReadWrite;
        return WGPUStorageTextureAccess_Undefined;
    }

    static WGPUSamplerBindingType convertSamplerBindingType(rhi::SamplerBindingType samplerBindingType) noexcept
    {
        switch (samplerBindingType)
        {
        case rhi::SamplerBindingType::Comparison:
            return WGPUSamplerBindingType_Comparison;
        case rhi::SamplerBindingType::Filtering:
            return WGPUSamplerBindingType_Filtering;
        default:
            return WGPUSamplerBindingType_Undefined;
        }
    }

    BindGroupLayout::BindGroupLayout(const Device& device, std::span<const rhi::BindGroupLayoutEntry> entries)
        : rhi::BindGroupLayout(const_cast<Device&>(device), entries)
    {
        std::vector<WGPUBindGroupLayoutEntry> wgpuEntries;
        wgpuEntries.reserve(entries.size());

        for (const auto& entry : entries)
        {
            WGPUBindGroupLayoutEntry wgpuEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            wgpuEntry.binding = entry.binding;
            wgpuEntry.visibility = convertShaderStage(entry.stageFlags);
            wgpuEntry.bindingArraySize = (entry.count > 1) ? entry.count : 0;
            assert(entry.bindless == false && "WebGPU not support Bindless");

            switch (entry.type)
            {
            case rhi::BindingType::Uniform_Buffer:
            case rhi::BindingType::Storage_Buffer:
            case rhi::BindingType::Dynamic_Uniform_Buffer:
            case rhi::BindingType::Dynamic_Storage_Buffer:
            {
                WGPUBufferBindingType bufferType = WGPUBufferBindingType_Undefined;
                if (entry.type == rhi::BindingType::Uniform_Buffer || entry.type == rhi::BindingType::Dynamic_Uniform_Buffer)
                {
                    bufferType = WGPUBufferBindingType_Uniform;
                }
                else if (entry.type == rhi::BindingType::Storage_Buffer || entry.type == rhi::BindingType::Dynamic_Storage_Buffer)
                {
                    if (entry.access == rhi::BindingAccess::Read_Only)
                    {
                        bufferType = WGPUBufferBindingType_ReadOnlyStorage;
                    }
                    else
                    {
                        bufferType = WGPUBufferBindingType_Storage;
                        wgpuEntry.visibility &= ~(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
                    }
                }

                wgpuEntry.buffer.type = bufferType;
                wgpuEntry.buffer.hasDynamicOffset = (entry.type == rhi::BindingType::Dynamic_Uniform_Buffer ||
                                                     entry.type == rhi::BindingType::Dynamic_Storage_Buffer) ? WGPU_TRUE : WGPU_FALSE;
                wgpuEntry.buffer.minBindingSize = 0;

                if (entry.type == rhi::BindingType::Dynamic_Uniform_Buffer ||
                    entry.type == rhi::BindingType::Dynamic_Storage_Buffer)
                {
                    ++mDynamicOffsetCount;
                }
                break;
            }
            case rhi::BindingType::Sampled_Texture:
            {
                wgpuEntry.texture.sampleType = convertTextureSampleType(entry.textureSampleType);
                wgpuEntry.texture.viewDimension = convertTextureViewType(entry.textureViewType);
                wgpuEntry.texture.multisampled = entry.multiSample ? WGPU_TRUE : WGPU_FALSE;
                break;
            }
            case rhi::BindingType::Storage_Texture:
            {
                wgpuEntry.storageTexture.access = convertStorageTextureAccess(entry.access);
                if (wgpuEntry.storageTexture.access == WGPUStorageTextureAccess_ReadWrite ||
                    wgpuEntry.storageTexture.access == WGPUStorageTextureAccess_WriteOnly)
                {
                    // wgpu not allow read-write/write storage in vertex/fragment stage
                    wgpuEntry.visibility &= ~(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
                }
                wgpuEntry.storageTexture.format = convertTextureFormat(entry.storageTextureFormat);
                wgpuEntry.storageTexture.viewDimension = convertTextureViewType(entry.textureViewType);
                break;
            }
            case rhi::BindingType::Sampler:
            {
                wgpuEntry.sampler.type = convertSamplerBindingType(entry.samplerBindingType);
                break;
            }
            default:
                assert(false && "Unsupported binding type");
                break;
            }

            wgpuEntries.push_back(wgpuEntry);
        }

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.nextInChain = nullptr;
        layoutDesc.label = WGPUStringView{ "Unnamed BindGroupLayout", WGPU_STRLEN };
        layoutDesc.entryCount = static_cast<uint32_t>(wgpuEntries.size());
        layoutDesc.entries = wgpuEntries.data();
        mLayout = wgpuDeviceCreateBindGroupLayout(device.getHandle(), &layoutDesc);
    }

    BindGroupLayout::~BindGroupLayout()
    {
        if (mLayout)
        {
            wgpuBindGroupLayoutRelease(mLayout);
            mLayout = nullptr;
        }
    }

    BindGroup::BindGroup(const Device& device, const BindGroupLayout& layout)
        : rhi::BindGroup(device, layout),
          mWgpuLayout(layout)
    {
    }

    BindGroup::~BindGroup()
    {
        if (mBindGroup)
        {
            wgpuBindGroupRelease(mBindGroup);
            mBindGroup = nullptr;
        }
    }

    void BindGroup::update(std::span<const rhi::BindGroupEntry> entries)
    {
        assert(mBindGroup == nullptr && "WebGPU BindGroup can only be updated once");
        createWgpuBindGroup(entries);
    }

    void BindGroup::createWgpuBindGroup(std::span<const rhi::BindGroupEntry> entries)
    {
        WGPUDevice device = getWgpuDevice().getHandle();
        WGPUBindGroupLayout layout = mWgpuLayout.getHandle();

        std::vector<WGPUBindGroupEntry> wgpuEntries;

        for (const auto& entry : entries)
        {
            const rhi::BindGroupLayoutEntry* layoutEntry = nullptr;
            for (const auto& le : mWgpuLayout.getEntries())
            {
                if (le.binding == entry.binding)
                {
                    layoutEntry = &le;
                    break;
                }
            }

            if (!layoutEntry)
            {
                LOG_ERROR("BindGroupEntry binding %u not found in layout", entry.binding);
                continue;
            }

            uint32_t arraySize = layoutEntry->count;

            switch (entry.type)
            {
            case rhi::BindingType::Uniform_Buffer:
            case rhi::BindingType::Storage_Buffer:
            case rhi::BindingType::Dynamic_Uniform_Buffer:
            case rhi::BindingType::Dynamic_Storage_Buffer:
            {
                for (size_t i = 0; i < entry.buffers.size(); ++i)
                {
                    const auto& bufferInfo = entry.buffers[i];
                    WGPUBindGroupEntry wgpuEntry = WGPU_BIND_GROUP_ENTRY_INIT;
                    wgpuEntry.binding = entry.binding;
                    wgpuEntry.buffer = bufferInfo.buffer ? static_cast<const Buffer*>(bufferInfo.buffer)->getHandle() : nullptr;
                    wgpuEntry.offset = bufferInfo.offset;
                    wgpuEntry.size = bufferInfo.range == rhi::kWholeBuffer ? WGPU_WHOLE_SIZE : bufferInfo.range;
                    wgpuEntries.push_back(wgpuEntry);
                }
                break;
            }
            case rhi::BindingType::Sampled_Texture:
            case rhi::BindingType::Storage_Texture:
            {
                for (size_t i = 0; i < entry.textures.size(); ++i)
                {
                    const rhi::TextureView* textureView = entry.textures[i];
                    WGPUBindGroupEntry wgpuEntry = WGPU_BIND_GROUP_ENTRY_INIT;
                    wgpuEntry.binding = entry.binding;
                    wgpuEntry.textureView = textureView
                        ? static_cast<const TextureView*>(textureView)->getHandle()
                        : nullptr;
                    wgpuEntries.push_back(wgpuEntry);
                }
                break;
            }
            case rhi::BindingType::Sampler:
            {
                for (size_t i = 0; i < entry.samplers.size(); ++i)
                {
                    const rhi::Sampler* sampler = entry.samplers[i];
                    WGPUBindGroupEntry wgpuEntry = WGPU_BIND_GROUP_ENTRY_INIT;
                    wgpuEntry.binding = entry.binding;
                    wgpuEntry.sampler = sampler ? static_cast<const Sampler*>(sampler)->getHandle() : nullptr;
                    wgpuEntries.push_back(wgpuEntry);
                }
                break;
            }
            default:
                assert(false && "Unsupported binding type");
                break;
            }
        }

        WGPUBindGroupDescriptor groupDesc = {};
        groupDesc.nextInChain = nullptr;
        groupDesc.label = WGPUStringView{ "UnnamedBindGroup", WGPU_STRLEN };
        groupDesc.layout = layout;
        groupDesc.entryCount = static_cast<uint32_t>(wgpuEntries.size());
        groupDesc.entries = wgpuEntries.data();
        mBindGroup = wgpuDeviceCreateBindGroup(device, &groupDesc);
    }
}
