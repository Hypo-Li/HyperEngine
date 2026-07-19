#pragma once
#include "DescriptorAllocator.h"

#include <RHI/BindGroup.h>

#include <volk.h>

#include <vector>

namespace hyp::vk
{
    class BindGroupLayout : public rhi::BindGroupLayout
    {
    public:
        BindGroupLayout(const Device& device, std::span<const rhi::BindGroupLayoutEntry> entries);

        ~BindGroupLayout() override;

        const Device& getVkDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        VkDescriptorSetLayout getHandle() const noexcept
        {
            return mSetLayout;
        }

        std::span<const VkDescriptorSetLayoutBinding> getVkBindings() const noexcept
        {
            return mBindings;
        }

        uint32_t getDynamicOffsetCount() const noexcept
        {
            return mDynamicOffsetCount;
        }

    private:
        VkDescriptorSetLayout mSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSetLayoutBinding> mBindings;
        std::vector<VkDescriptorBindingFlags> mBindingFlags;
        uint32_t mDynamicOffsetCount = 0;
    };

    class BindGroup : public rhi::BindGroup
    {
    public:
        BindGroup(const Device& device, const BindGroupLayout& layout);

        ~BindGroup() override;

        const Device& getVkDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        const BindGroupLayout& getVkLayout() const noexcept
        {
            return mLayout;
        }

        VkDescriptorSet getHandle() const noexcept
        {
            return mDescriptorSet;
        }

        void update(std::span<const rhi::BindGroupEntry> entries) override;

    private:
        const BindGroupLayout& mLayout;
        VkDescriptorSet mDescriptorSet = VK_NULL_HANDLE;
    };
}
