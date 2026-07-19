#include "BindGroup.h"

#include "Buffer.h"
#include "Sampler.h"
#include "Texture.h"

#include <Core/Common/LogSystem.h>

#include <cassert>
#include <cstring>
#include <limits>

namespace hyp::vk
{
    static VkDescriptorType convertBindingType(rhi::BindingType type) noexcept
    {
        switch (type)
        {
        case rhi::BindingType::Uniform_Buffer:         return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case rhi::BindingType::Storage_Buffer:         return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case rhi::BindingType::Dynamic_Uniform_Buffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case rhi::BindingType::Dynamic_Storage_Buffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case rhi::BindingType::Sampled_Texture:        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case rhi::BindingType::Storage_Texture:        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case rhi::BindingType::Sampler:                return VK_DESCRIPTOR_TYPE_SAMPLER;
        default:
            assert(false && "Unsupported binding type");
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }
    }

    static VkShaderStageFlags convertShaderStage(rhi::ShaderStage stage) noexcept
    {
        VkShaderStageFlags flags = 0;
        if (enumHasAnyFlags(stage, rhi::ShaderStage::Vertex))
            flags |= VK_SHADER_STAGE_VERTEX_BIT;
        if (enumHasAnyFlags(stage, rhi::ShaderStage::Fragment))
            flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if (enumHasAnyFlags(stage, rhi::ShaderStage::Compute))
            flags |= VK_SHADER_STAGE_COMPUTE_BIT;
        return flags;
    }

    BindGroupLayout::BindGroupLayout(const Device& device, std::span<const rhi::BindGroupLayoutEntry> entries)
        : rhi::BindGroupLayout(const_cast<Device&>(device), entries)
    {
        mBindings.reserve(entries.size());
        mBindingFlags.resize(entries.size(), 0);

        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries[i];

            VkDescriptorSetLayoutBinding binding = {};
            binding.binding = entry.binding;
            binding.descriptorType = convertBindingType(entry.type);
            binding.descriptorCount = entry.count;
            binding.stageFlags = convertShaderStage(entry.stageFlags);
            binding.pImmutableSamplers = nullptr;
            mBindings.push_back(binding);

            if (entry.bindless)
            {
                mBindingFlags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                    | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            }

            if (entry.type == rhi::BindingType::Dynamic_Uniform_Buffer ||
                entry.type == rhi::BindingType::Dynamic_Storage_Buffer)
            {
                ++mDynamicOffsetCount;
            }
        }

        mSetLayout = device.getDescriptorAllocator().getSetLayout(mBindings, mBindingFlags);
    }

    BindGroupLayout::~BindGroupLayout()
    {
    }

    BindGroup::BindGroup(const Device& device, const BindGroupLayout& layout)
        : rhi::BindGroup(device, layout),
        mLayout(layout)
    {
        mDescriptorSet = device.getDescriptorAllocator().allocateSet(layout.getHandle());
    }

    BindGroup::~BindGroup()
    {
        if (mDescriptorSet != VK_NULL_HANDLE)
        {
            getVkDevice().getDescriptorAllocator().freeSet(mLayout.getHandle(), mDescriptorSet);
            mDescriptorSet = VK_NULL_HANDLE;
        }
    }

    void BindGroup::update(std::span<const rhi::BindGroupEntry> entries)
    {
        VkDevice device = getVkDevice().getHandle();

        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkDescriptorImageInfo> imageInfos;

        // Reserve enough space to avoid reallocation invalidating pointers.
        size_t totalBufferBindings = 0;
        size_t totalImageBindings = 0;
        for (const auto& entry : entries)
        {
            totalBufferBindings += entry.buffers.size();
            totalImageBindings += entry.textures.size() + entry.samplers.size();
        }
        bufferInfos.reserve(totalBufferBindings);
        imageInfos.reserve(totalImageBindings);

        for (const auto& entry : entries)
        {
            const auto* layoutBindings = mLayout.getVkBindings().data();
            uint32_t layoutBindingCount = static_cast<uint32_t>(mLayout.getVkBindings().size());

            VkDescriptorSetLayoutBinding vkBinding = {};
            bool found = false;
            for (uint32_t i = 0; i < layoutBindingCount; ++i)
            {
                if (layoutBindings[i].binding == entry.binding)
                {
                    vkBinding = layoutBindings[i];
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                LOG_ERROR("BindGroupEntry binding %u not found in layout", entry.binding);
                continue;
            }

            if (entry.buffers.size() > 0)
            {
                uint32_t firstBufferInfoIndex = static_cast<uint32_t>(bufferInfos.size());
                for (const auto& bufferInfo : entry.buffers)
                {
                    VkDescriptorBufferInfo info = {};
                    info.buffer = bufferInfo.buffer ? static_cast<const Buffer*>(bufferInfo.buffer)->getHandle() : VK_NULL_HANDLE;
                    info.offset = bufferInfo.offset;
                    info.range = bufferInfo.range;
                    bufferInfos.push_back(info);
                }

                VkWriteDescriptorSet write = {};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = mDescriptorSet;
                write.dstBinding = entry.binding;
                write.dstArrayElement = entry.index;
                write.descriptorType = vkBinding.descriptorType;
                write.descriptorCount = static_cast<uint32_t>(entry.buffers.size());
                write.pBufferInfo = bufferInfos.data() + firstBufferInfoIndex;
                writes.push_back(write);
            }
            else if (entry.textures.size() > 0)
            {
                uint32_t firstImageInfoIndex = static_cast<uint32_t>(imageInfos.size());
                VkImageLayout imageLayout = entry.type == rhi::BindingType::Storage_Texture ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                for (const rhi::TextureView* textureView : entry.textures)
                {
                    VkDescriptorImageInfo info = {};
                    info.imageLayout = imageLayout;
                    info.imageView = textureView ? static_cast<const TextureView*>(textureView)->getHandle() : VK_NULL_HANDLE;
                    info.sampler = VK_NULL_HANDLE;
                    imageInfos.push_back(info);
                }

                VkWriteDescriptorSet write = {};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = mDescriptorSet;
                write.dstBinding = entry.binding;
                write.dstArrayElement = entry.index;
                write.descriptorType = vkBinding.descriptorType;
                write.descriptorCount = static_cast<uint32_t>(entry.textures.size());
                write.pImageInfo = imageInfos.data() + firstImageInfoIndex;
                writes.push_back(write);
            }
            else if (entry.samplers.size() > 0)
            {
                uint32_t firstImageInfoIndex = static_cast<uint32_t>(imageInfos.size());
                for (const rhi::Sampler* sampler : entry.samplers)
                {
                    VkDescriptorImageInfo info = {};
                    info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    info.imageView = VK_NULL_HANDLE;
                    info.sampler = sampler ? static_cast<const Sampler*>(sampler)->getHandle() : VK_NULL_HANDLE;
                    imageInfos.push_back(info);
                }

                VkWriteDescriptorSet write = {};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = mDescriptorSet;
                write.dstBinding = entry.binding;
                write.dstArrayElement = entry.index;
                write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                write.descriptorCount = static_cast<uint32_t>(entry.samplers.size());
                write.pImageInfo = imageInfos.data() + firstImageInfoIndex;
                writes.push_back(write);
            }
        }

        if (!writes.empty())
        {
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }
}
