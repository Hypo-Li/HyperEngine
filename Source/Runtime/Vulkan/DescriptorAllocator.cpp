#include "DescriptorAllocator.h"
#include <Core/Common/Hash.h>

namespace hyp::vk
{
    DescriptorAllocator::DescriptorAllocator(const Device& device) :
        mDevice(device)
    {

    }

    DescriptorAllocator::~DescriptorAllocator()
    {
        for (auto& [layout, poolGroup] : mSetLayoutPoolGroupMap)
        {
            for (size_t i = 0; i < poolGroup.poolInfos.size(); ++i)
            {
                vkDestroyDescriptorPool(mDevice.getHandle(), poolGroup.poolInfos[i].pool, nullptr);
            }
            vkDestroyDescriptorSetLayout(mDevice.getHandle(), poolGroup.layout, nullptr);
        }
    }

    VkDescriptorSetLayout DescriptorAllocator::getSetLayout(
        std::span<const VkDescriptorSetLayoutBinding> bindings,
        std::span<const VkDescriptorBindingFlags> flags
    )
    {
        return ensurePoolGroup(bindings, flags).layout;
    }

    VkDescriptorSet DescriptorAllocator::allocateSet(VkDescriptorSetLayout layout)
    {
        auto& poolGroup = mSetLayoutPoolGroupMap.at(layout);

        // 优先复用之前 free 的 set
        if (!poolGroup.freeSets.empty())
        {
            VkDescriptorSet set = poolGroup.freeSets.back();
            poolGroup.freeSets.pop_back();
            mAllocatedSets.push_back(set);
            return set;
        }

        // 如果没有可复用的 set, 再尝试从pool中分配
        // 尝试找到可用的pool
        PoolInfo* validPoolInfo = nullptr;
        for (auto& poolInfo : poolGroup.poolInfos)
        {
            if (poolInfo.poolAllocated < poolInfo.capacity)
            {
                validPoolInfo = &poolInfo;
                break;
            }
        }

        // 如果没有可用的pool, 则创建新的pool
        if (validPoolInfo == nullptr)
        {
            uint32_t capacity = poolGroup.poolInfos.empty() ? kBaseCapacity : kBaseCapacity << (poolGroup.poolInfos.size() - 1);
            capacity = std::min(capacity, kMaxCapacity);

            auto& setLayoutPoolSizes = poolGroup.poolSizes;
            std::vector<VkDescriptorPoolSize> poolSizes(setLayoutPoolSizes.begin(), setLayoutPoolSizes.end());
            for (auto& poolSize : poolSizes)
                poolSize.descriptorCount *= capacity;

            VkDescriptorPoolCreateFlags poolFlags = 0;
            for (auto flag : poolGroup.flags)
            {
                if (flag & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                    poolFlags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            }

            VkDescriptorPool newPool = VK_NULL_HANDLE;
            {
                VkDescriptorPoolCreateInfo poolInfo = {};
                poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                poolInfo.maxSets = capacity;
                poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
                poolInfo.pPoolSizes = poolSizes.data();
                poolInfo.flags = poolFlags;
                vkCreateDescriptorPool(mDevice.getHandle(), &poolInfo, nullptr, &newPool);
            }

            validPoolInfo = &poolGroup.poolInfos.emplace_back(newPool, capacity, 0);
        }

        // 分配descriptor set
        VkDescriptorSetAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = validPoolInfo->pool;
        VkDescriptorSetLayout setLayouts[] = { poolGroup.layout };
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = setLayouts;

        VkDescriptorSet descriptorSet;
        VkResult result = vkAllocateDescriptorSets(mDevice.getHandle(), &allocateInfo, &descriptorSet);
        assert(result == VK_SUCCESS);
        ++validPoolInfo->poolAllocated;
        mAllocatedSets.push_back(descriptorSet);
        return descriptorSet;
    }

    void DescriptorAllocator::freeSet(VkDescriptorSetLayout layout, VkDescriptorSet set)
    {
        auto setIt = std::find(mAllocatedSets.begin(), mAllocatedSets.end(), set);
        if (setIt != mAllocatedSets.end())
        {
            auto& poolGroup = mSetLayoutPoolGroupMap.at(layout);
            poolGroup.freeSets.push_back(set);
            mAllocatedSets.erase(setIt);
        }
    }

    bool DescriptorAllocator::SetLayoutBindingKeyEqual::operator()(const SetLayoutBindingKey& lhs, const SetLayoutBindingKey& rhs) const
    {
        if (lhs.bindings.size() != rhs.bindings.size() ||
               lhs.flags.size() != rhs.flags.size())
        {
            return false;
        }

        for (size_t i = 0; i < lhs.bindings.size(); ++i)
        {
            auto& a = lhs.bindings[i];
            auto& b = rhs.bindings[i];
            if (a.binding != b.binding ||
                a.descriptorType != b.descriptorType ||
                a.descriptorCount != b.descriptorCount ||
                a.stageFlags != b.stageFlags ||
                a.pImmutableSamplers != b.pImmutableSamplers)
            {
                return false;
            }
        }

        for (size_t i = 0; i < lhs.flags.size(); ++i)
        {
            if (lhs.flags[i] != rhs.flags[i])
            {
                return false;
            }
        }

        return true;
    }

    std::size_t DescriptorAllocator::SetLayoutBindingKeyHasher::operator()(const SetLayoutBindingKey& key) const
    {
        size_t seed = 0;
        for (const auto& binding : key.bindings)
        {
            Hash::combine(seed, binding.binding);
            Hash::combine(seed, binding.descriptorType);
            Hash::combine(seed, binding.descriptorCount);
            Hash::combine(seed, binding.stageFlags);
        }
        for (const auto& flag : key.flags)
        {
            Hash::combine(seed, flag);
        }
        return seed;
    }

    DescriptorAllocator::SetLayoutPoolGroup& DescriptorAllocator::ensurePoolGroup(
        std::span<const VkDescriptorSetLayoutBinding> bindings,
        std::span<const VkDescriptorBindingFlags> flags
    )
    {
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        SetLayoutBindingKey key{ bindings, flags };
        auto it = mSetLayoutMap.find(key);
        if (it != mSetLayoutMap.end())
        {
            layout = it->second;
            return mSetLayoutPoolGroupMap.at(layout);
        }
        else
        {
            VkDescriptorSetLayoutCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            createInfo.pBindings = bindings.data();
            VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo = {};
            if (!flags.empty())
            {
                flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
                flagsInfo.bindingCount = static_cast<uint32_t>(flags.size());
                flagsInfo.pBindingFlags = flags.data();
                createInfo.pNext = &flagsInfo;

                for (auto flag : flags)
                {
                    if (flag & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                        createInfo.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
                }
            }
            vkCreateDescriptorSetLayout(mDevice.getHandle(), &createInfo, nullptr, &layout);

            static constexpr uint32_t kMaxDescriptorTypeCount = static_cast<uint32_t>(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT + 1);
            std::vector<VkDescriptorPoolSize> poolSizes(kMaxDescriptorTypeCount);
            for (uint32_t i = 0; i < kMaxDescriptorTypeCount; ++i)
            {
                poolSizes[i].type = static_cast<VkDescriptorType>(i);
                poolSizes[i].descriptorCount = 0;
            }

            for (auto& binding : bindings)
            {
                uint32_t index = static_cast<uint32_t>(binding.descriptorType);
                poolSizes[index].descriptorCount += binding.descriptorCount;
            }

            poolSizes.erase(
                std::remove_if(
                    poolSizes.begin(), poolSizes.end(),
                    [](const VkDescriptorPoolSize& size)
                    {
                        return size.descriptorCount == 0;
                    }
                ),
                poolSizes.end()
            );

            SetLayoutPoolGroup newPoolGroup;
            newPoolGroup.layout = layout;
            newPoolGroup.poolSizes = std::move(poolSizes);
            newPoolGroup.bindings.assign(bindings.begin(), bindings.end());
            newPoolGroup.flags.assign(flags.begin(), flags.end());
            auto [emplacedIt, inserted] = mSetLayoutPoolGroupMap.emplace(layout, std::move(newPoolGroup));

            SetLayoutBindingKey layoutKey;
            layoutKey.bindings = emplacedIt->second.bindings;
            layoutKey.flags = emplacedIt->second.flags;
            mSetLayoutMap.emplace(layoutKey, layout);

            return emplacedIt->second;
        }
    }

}
