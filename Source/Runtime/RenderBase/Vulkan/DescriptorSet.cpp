#include "DescriptorSet.h"

#include <Core/Common/Hash.h>

namespace hyp::vk
{
    DescriptorSetLayout::DescriptorSetLayout(
        const Device& device,
        std::span<const VkDescriptorSetLayoutBinding> bindings,
        std::span<const VkDescriptorBindingFlags> flags
    ) : mDevice(device)
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

        if (vkCreateDescriptorSetLayout(mDevice.getHandle(), &createInfo, nullptr, &mDescriptorSetLayout) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create vulkan descriptor set layout");
        }

        mBindings.assign(bindings.begin(), bindings.end());
        mFlags.assign(flags.begin(), flags.end());
    }

    DescriptorSetLayout::~DescriptorSetLayout() noexcept
    {
        vkDestroyDescriptorSetLayout(mDevice.getHandle(), mDescriptorSetLayout, nullptr);
    }

    DescriptorPool::DescriptorPool(
        const Device& device,
        uint32_t maxSets,
        std::span<const VkDescriptorPoolSize> poolSizes,
        VkDescriptorPoolCreateFlags flags
    ) : mDevice(device)
    {
        VkDescriptorPoolCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.maxSets = maxSets;
        createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        createInfo.pPoolSizes = poolSizes.data();
        createInfo.flags = flags;

        vkCreateDescriptorPool(mDevice.getHandle(), &createInfo, nullptr, &mDescriptorPool);
    }

    DescriptorPool::~DescriptorPool() noexcept
    {
        vkDestroyDescriptorPool(mDevice.getHandle(), mDescriptorPool, nullptr);
    }

    void DescriptorPool::reset() const noexcept
    {
        vkResetDescriptorPool(mDevice.getHandle(), mDescriptorPool, 0);
    }

    std::size_t IDescriptorAllocator::SetLayoutBindingKeyHash::operator()(const SetLayoutBindingKey& key) const noexcept
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

    bool IDescriptorAllocator::SetLayoutBindingKeyEqual::operator()(const SetLayoutBindingKey& a, const SetLayoutBindingKey& b) const noexcept
    {
        if (a.bindings.size() != b.bindings.size() ||
            a.flags.size() != b.flags.size())
            return false;
        for (size_t i = 0; i < a.bindings.size(); ++i)
        {
            const VkDescriptorSetLayoutBinding& bindingA = a.bindings[i];
            const VkDescriptorSetLayoutBinding& bindingB = b.bindings[i];
            if (bindingA.binding != bindingB.binding ||
                bindingA.descriptorType != bindingB.descriptorType ||
                bindingA.descriptorCount != bindingB.descriptorCount ||
                bindingA.stageFlags != bindingB.stageFlags)
            {
                return false;
            }
        }
        for (size_t i = 0; i < a.flags.size(); ++i)
        {
            if (a.flags[i] != b.flags[i])
                return false;
        }
        return true;
    }

    DescriptorArena::DescriptorArena(
        const Device& device,
        uint32_t maxSets,
        std::span<const VkDescriptorPoolSize> poolSizes,
        VkDescriptorPoolCreateFlags flags
    ) : mDevice(device),
        mPool(new DescriptorPool(device, maxSets, poolSizes, flags))
    {
    }

    DescriptorArena::~DescriptorArena() noexcept
    {
    }

    TRef<DescriptorSet> DescriptorArena::allocate(const DescriptorSetLayout& setLayout)
    {
        return allocate(setLayout.getBindings(), setLayout.getFlags());
    }

    TRef<DescriptorSet> DescriptorArena::allocate(std::span<const VkDescriptorSetLayoutBinding> bindings, std::span<const VkDescriptorBindingFlags> flags)
    {
        SetLayoutBindingKey key = { bindings, flags };
        TRef<DescriptorSetLayout> setLayout = nullptr;
        auto it = mSetLayoutCache.find(key);
        if (it != mSetLayoutCache.end())
        {
            setLayout = it->second;
        }
        else
        {
            setLayout = new DescriptorSetLayout(mDevice, bindings, flags);
            SetLayoutBindingKey storagedKey = { setLayout->getBindings(), setLayout->getFlags() };
            mSetLayoutCache.emplace(storagedKey, setLayout);
        }
        return createSet(*setLayout);
    }

    void DescriptorArena::free(DescriptorSet& set) noexcept
    {
    }

    void DescriptorArena::reset() const noexcept
    {
        mPool->reset();
    }

    TRef<DescriptorSet> DescriptorArena::createSet(DescriptorSetLayout& setLayout)
    {
        VkDescriptorSetAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = mPool->getHandle();
        VkDescriptorSetLayout setLayoutHandle = setLayout.getHandle();
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &setLayoutHandle;

        VkDescriptorSet descriptorSet;
        VkResult result = vkAllocateDescriptorSets(mDevice.getHandle(), &allocateInfo, &descriptorSet);
        assert(result == VK_SUCCESS);

        TRef<DescriptorSet> set = new DescriptorSet(*this, *mPool, setLayout, descriptorSet);
        return set;
    }

    DescriptorAllocator::DescriptorAllocator(const Device& device)
        : mDevice(device)
    {
    }

    DescriptorAllocator::~DescriptorAllocator() noexcept
    {
        cleanup();
    }

    DescriptorSetLayout* DescriptorAllocator::getSetLayout(std::span<const VkDescriptorSetLayoutBinding> bindings, std::span<const VkDescriptorBindingFlags> flags)
    {
        return ensurePoolGroup(bindings, flags).layout;
    }

    TRef<DescriptorSet> DescriptorAllocator::allocate(const DescriptorSetLayout& layout)
    {
        return allocate(layout.getBindings(), layout.getFlags());
    }

    TRef<DescriptorSet> DescriptorAllocator::allocate(std::span<const VkDescriptorSetLayoutBinding> bindings, std::span<const VkDescriptorBindingFlags> flags)
    {
        auto& poolGroup = ensurePoolGroup(bindings, flags);

        // 优先复用之前 free 的 set
        if (!poolGroup.freeSets.empty())
        {
            TRef<DescriptorSet> set = poolGroup.freeSets.back();
            poolGroup.freeSets.pop_back();
            mSets.push_back(set.get());
            return set;
        }

        // 如果没有可复用的 set, 再尝试从pool中分配
        
        // 尝试找到可用的pool
        PoolInfo* poolInfo = nullptr;
        for (auto& pool : poolGroup.pools)
        {
            if (pool.used < pool.capacity)
            {
                poolInfo = &pool;
                break;
            }
        }

        // 如果没有可用的pool, 则创建新的pool
        if (poolInfo == nullptr)
        {
            uint32_t capacity = poolGroup.pools.empty() ? kBaseCapacity : kBaseCapacity << (poolGroup.pools.size() - 1);
            capacity = std::min(capacity, kMaxCapacity);

            auto& setLayoutPoolSizes = poolGroup.poolSizes;
            std::vector<VkDescriptorPoolSize> poolSizes(setLayoutPoolSizes.begin(), setLayoutPoolSizes.end());
            for (auto& poolSize : poolSizes)
                poolSize.descriptorCount *= capacity;

            VkDescriptorPoolCreateFlags poolFlags = 0;
            for (auto flag : flags)
            {
                if (flag & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                    poolFlags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            }

            poolInfo = &poolGroup.pools.emplace_back(
                new DescriptorPool(mDevice, capacity, poolSizes, poolFlags),
                capacity,
                0
            );
        }

        // 分配descriptor set
        VkDescriptorSetAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = poolInfo->pool->getHandle();
        VkDescriptorSetLayout setLayouts[] = { poolGroup.layout->getHandle() };
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = setLayouts;

        VkDescriptorSet descriptorSet;
        VkResult result = vkAllocateDescriptorSets(mDevice.getHandle(), &allocateInfo, &descriptorSet);
        if (result != VK_SUCCESS)
            return nullptr;

        DescriptorSet* set = new DescriptorSet(*this, *poolInfo->pool, *poolGroup.layout, descriptorSet);
        mSets.push_back(set);
        ++poolInfo->used;
        return set;
    }

    void DescriptorAllocator::free(DescriptorSet& set) noexcept
    {
        auto setIt = std::remove(mSets.begin(), mSets.end(), &set);
        if (setIt == mSets.end())
            return;
        mSets.erase(setIt);

        auto layoutIt = std::find_if(
            mSetLayoutPoolMap.begin(), mSetLayoutPoolMap.end(),
            [&set](const SetLayoutPoolMap::value_type& value) { return value.second.layout == &set.getLayout(); }
        );
        if (layoutIt == mSetLayoutPoolMap.end())
            return;

        // 重建 DescriptorSet, 并加入到 freeSets 中
        VkDescriptorSet descriptorSet = set.getHandle();
        DescriptorPool& pool = const_cast<DescriptorPool&>(set.getPool());
        DescriptorSetLayout& layout = set.getLayout();

        set.mDescriptorSet = VK_NULL_HANDLE;

        auto& poolGroup = layoutIt->second;
        poolGroup.freeSets.emplace_back(new DescriptorSet(
            *this,
            pool,
            layout,
            descriptorSet
        ));
    }

    void DescriptorAllocator::cleanup() noexcept
    {
        for (auto set : mSets)
        {
            set->mDescriptorSet = VK_NULL_HANDLE;
        }
        mSets.clear();

        for (auto& [key, poolGroup] : mSetLayoutPoolMap)
        {
            for (auto& ref : poolGroup.freeSets)
                ref->mDescriptorSet = VK_NULL_HANDLE;
            poolGroup.freeSets.clear();
        }

        mSetLayoutPoolMap.clear();
    }

    std::unordered_map<VkDescriptorType, uint32_t> DescriptorAllocator::buildPoolSizeMap(std::span<const VkDescriptorSetLayoutBinding> bindings)
    {
        std::unordered_map<VkDescriptorType, uint32_t> poolSizeMap;
        poolSizeMap.reserve(bindings.size());
        for (auto& binding : bindings)
        {
            auto it = poolSizeMap.find(binding.descriptorType);
            if (it == poolSizeMap.end())
            {
                poolSizeMap.emplace(binding.descriptorType, binding.descriptorCount);
            }
            else
            {
                it->second += binding.descriptorCount;
            }
        }
        return poolSizeMap;
    }

    std::vector<VkDescriptorPoolSize> DescriptorAllocator::makePoolSizesFromMap(const std::unordered_map<VkDescriptorType, uint32_t>& map)
    {
        std::vector<VkDescriptorPoolSize> out;
        out.reserve(map.size());
        for (auto& [type, count] : map)
        {
            out.emplace_back(type, count);
        }
        return out;
    }

    DescriptorAllocator::SetLayoutPoolGroup& DescriptorAllocator::ensurePoolGroup(std::span<const VkDescriptorSetLayoutBinding> bindings, std::span<const VkDescriptorBindingFlags> flags)
    {
        SetLayoutBindingKey key = { bindings, flags };
        auto it = mSetLayoutPoolMap.find(key);
        if (it != mSetLayoutPoolMap.end())
            return it->second;

        TRef<DescriptorSetLayout> layout = new DescriptorSetLayout(mDevice, bindings, flags);
        auto poolSizeMap = buildPoolSizeMap(bindings);
        SetLayoutBindingKey storagedKey = { layout->getBindings(), layout->getFlags() };
        auto [emplacedIt, inserted] = mSetLayoutPoolMap.emplace(storagedKey, SetLayoutPoolGroup{layout, makePoolSizesFromMap(poolSizeMap)});
        return emplacedIt->second;
    }

    DescriptorSet::DescriptorSet(
        IDescriptorAllocator& allocator,
        const DescriptorPool& pool,
        DescriptorSetLayout& layout,
        VkDescriptorSet set
    ) : mAllocator(allocator), mPool(pool), mLayout(layout), mDescriptorSet(set)
    {

    }

    DescriptorSet::~DescriptorSet() noexcept
    {
        if (mDescriptorSet != VK_NULL_HANDLE)
            mAllocator.free(*this);
    }

    void DescriptorSet::update(std::span<const VkWriteDescriptorSet> writes) noexcept
    {
        vkUpdateDescriptorSets(mPool.getDevice().getHandle(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    DescriptorWriter::DescriptorWriter()
    {
        mBufferInfos.reserve(4);
        mImageInfos.reserve(4);
        mWrites.reserve(8);
        mWriteInfoIndices.reserve(8);
    }

    void DescriptorWriter::writeBuffer(
        uint32_t binding,
        VkDescriptorType type,
        VkBuffer buffer,
        size_t offset,
        size_t range,
        uint32_t arrayElement
    )
    {
        mWriteInfoIndices.emplace_back(static_cast<uint32_t>(mBufferInfos.size()));
        auto& bufferInfo = mBufferInfos.emplace_back(buffer, offset, range);

        auto& write = mWrites.emplace_back();
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = VK_NULL_HANDLE;
        write.dstBinding = binding;
        write.dstArrayElement = arrayElement;
        write.descriptorType = type;
        write.descriptorCount = 1;
    }

    void DescriptorWriter::writeImage(
        uint32_t binding,
        VkDescriptorType type,
        VkSampler sampler,
        VkImageView imageView,
        VkImageLayout imageLayout,
        uint32_t arrayElement
    )
    {
        mWriteInfoIndices.emplace_back(static_cast<uint32_t>(mImageInfos.size()));
        auto& imageInfo = mImageInfos.emplace_back(sampler, imageView, imageLayout);

        auto& write = mWrites.emplace_back();
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = VK_NULL_HANDLE;
        write.dstBinding = binding;
        write.dstArrayElement = arrayElement;
        write.descriptorType = type;
        write.descriptorCount = 1;
    }

    void DescriptorWriter::writeImages(
        uint32_t binding,
        VkDescriptorType type,
        std::span<const VkSampler> samplers,
        std::span<const VkImageView> imageViews,
        std::span<const VkImageLayout> imageLayouts,
        uint32_t arrayElement
    )
    {
        assert(samplers.size() == imageViews.size() && samplers.size() == imageLayouts.size());
        mWriteInfoIndices.emplace_back(static_cast<uint32_t>(mImageInfos.size()));
        uint32_t count = static_cast<uint32_t>(samplers.size());
        for (uint32_t i = 0; i < count; ++i)
        {
            mImageInfos.emplace_back(samplers[i], imageViews[i], imageLayouts[i]);
        }

        auto& write = mWrites.emplace_back();
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = VK_NULL_HANDLE;
        write.dstBinding = binding;
        write.dstArrayElement = arrayElement;
        write.descriptorType = type;
        write.descriptorCount = count;
    }

    void DescriptorWriter::writeAccelerationStructure(uint32_t binding, VkAccelerationStructureKHR accelerationStructure)
    {
        mWriteInfoIndices.emplace_back(static_cast<uint32_t>(mAccelerationStructureWrites.size()));

        auto& asWrite = mAccelerationStructureWrites.emplace_back();
        asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        asWrite.accelerationStructureCount = 1;

        mAccelerationStructureIndices.emplace_back(static_cast<uint32_t>(mAccelerationStructures.size()));
        mAccelerationStructures.emplace_back(accelerationStructure);

        auto& write = mWrites.emplace_back();
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = VK_NULL_HANDLE;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        write.descriptorCount = 1;
    }

    void DescriptorWriter::update(DescriptorSet& set) noexcept
    {
        for (uint32_t i = 0; i < mWrites.size(); ++i)
        {
            auto& write = mWrites[i];
            write.dstSet = set.getHandle();
            if (isBufferDescriptorType(write.descriptorType))
            {
                write.pBufferInfo = &mBufferInfos[mWriteInfoIndices[i]];
            }
            else if (isImageDescriptorType(write.descriptorType))
            {
                write.pImageInfo = &mImageInfos[mWriteInfoIndices[i]];
            }
            else if (isAccelerationStructureType(write.descriptorType))
            {
                auto& as = mAccelerationStructureWrites[mWriteInfoIndices[i]];
                as.pAccelerationStructures = &mAccelerationStructures[mAccelerationStructureIndices[mWriteInfoIndices[i]]];
                write.pNext = &as;
            }
        }
        set.update(mWrites);
    }

    bool DescriptorWriter::isBufferDescriptorType(VkDescriptorType type)
    {
        switch (type)
        {
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            return true;
        default:
            return false;
        }
    }

    bool DescriptorWriter::isImageDescriptorType(VkDescriptorType type)
    {
        switch (type)
        {
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            return true;
        default:
            return false;
        }
    }

    bool DescriptorWriter::isAccelerationStructureType(VkDescriptorType type)
    {
        return type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }

}
