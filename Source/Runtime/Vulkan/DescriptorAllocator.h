#pragma once
#include "Device.h"

namespace hyp::vk
{
    class DescriptorAllocator
    {
    public:
        DescriptorAllocator(const Device& device);

        ~DescriptorAllocator();

        VkDescriptorSetLayout getSetLayout(
            std::span<const VkDescriptorSetLayoutBinding> bindings,
            std::span<const VkDescriptorBindingFlags> flags
        );

        VkDescriptorSet allocateSet(VkDescriptorSetLayout layout);

        void freeSet(VkDescriptorSetLayout layout, VkDescriptorSet set);

    private:
        struct PoolInfo
        {
            VkDescriptorPool pool;
            uint32_t capacity = 0;
            uint32_t poolAllocated = 0;
        };

        struct SetLayoutPoolGroup
        {
            VkDescriptorSetLayout layout;
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            std::vector<VkDescriptorBindingFlags> flags;

            std::vector<VkDescriptorPoolSize> poolSizes;
            std::vector<PoolInfo> poolInfos;
            std::vector<VkDescriptorSet> freeSets;
        };

        struct SetLayoutBindingKey
        {
            std::span<const VkDescriptorSetLayoutBinding> bindings;
            std::span<const VkDescriptorBindingFlags> flags;
        };

        struct SetLayoutBindingKeyEqual
        {
            bool operator()(const SetLayoutBindingKey& lhs, const SetLayoutBindingKey& rhs) const;
        };

        struct SetLayoutBindingKeyHasher
        {
            std::size_t operator()(const SetLayoutBindingKey& key) const;
        };

        using SetLayoutMap = std::unordered_map<SetLayoutBindingKey, VkDescriptorSetLayout, SetLayoutBindingKeyHasher, SetLayoutBindingKeyEqual>;

        SetLayoutPoolGroup& ensurePoolGroup(
            std::span<const VkDescriptorSetLayoutBinding> bindings,
            std::span<const VkDescriptorBindingFlags> flags
        );

    private:
        const Device& mDevice;
        SetLayoutMap mSetLayoutMap;
        std::unordered_map<VkDescriptorSetLayout, SetLayoutPoolGroup> mSetLayoutPoolGroupMap;
        std::vector<VkDescriptorSet> mAllocatedSets;
        static constexpr uint32_t kBaseCapacity = 1;
        static constexpr uint32_t kMaxCapacity = 256;
    };
}
