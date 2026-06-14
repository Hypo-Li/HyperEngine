#pragma once
#include "Device.h"

namespace hyp::vk
{
    class DescriptorSetLayout : public Refable
    {
    public:
        DescriptorSetLayout(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings, std::span<const VkDescriptorBindingFlags> flags = {});

        ~DescriptorSetLayout() noexcept;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        VkDescriptorSetLayout getHandle() const noexcept
        {
            return mDescriptorSetLayout;
        }

        std::span<const VkDescriptorSetLayoutBinding> getBindings() const noexcept
        {
            return mBindings;
        }

        std::span<const VkDescriptorBindingFlags> getFlags() const noexcept
        {
            return mFlags;
        }

    private:
        const Device& mDevice;
        VkDescriptorSetLayout mDescriptorSetLayout{ VK_NULL_HANDLE };
        std::vector<VkDescriptorSetLayoutBinding> mBindings;
        std::vector<VkDescriptorBindingFlags> mFlags;
    };

    class DescriptorPool : public Refable
    {
    public:
        DescriptorPool(const Device& device, uint32_t maxSets, std::span<const VkDescriptorPoolSize> poolSizes, VkDescriptorPoolCreateFlags flags = 0);

        ~DescriptorPool() noexcept;

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        VkDescriptorPool getHandle() const noexcept
        {
            return mDescriptorPool;
        }

        void reset() const noexcept;

    private:
        const Device& mDevice;
        VkDescriptorPool mDescriptorPool{ VK_NULL_HANDLE };
    };

    class DescriptorSet;

    class IDescriptorAllocator
    {
    public:
        virtual ~IDescriptorAllocator() = default;

        virtual void free(DescriptorSet& set) noexcept = 0;

    protected:
        struct SetLayoutBindingKey
        {
            std::span<const VkDescriptorSetLayoutBinding> bindings;
            std::span<const VkDescriptorBindingFlags> flags;
        };

        struct SetLayoutBindingKeyHash
        {
            std::size_t operator()(const SetLayoutBindingKey& key) const noexcept;
        };

        struct SetLayoutBindingKeyEqual
        {
            bool operator()(const SetLayoutBindingKey& a, const SetLayoutBindingKey& b) const noexcept;
        };
    };

    // Transient Descriptor Arena
    class DescriptorArena : public IDescriptorAllocator
    {
    public:
        DescriptorArena(const Device& device, uint32_t maxSets, std::span<const VkDescriptorPoolSize> poolSizes, VkDescriptorPoolCreateFlags flags);

        ~DescriptorArena() noexcept;

        TRef<DescriptorSet> allocate(const DescriptorSetLayout& setLayout);

        TRef<DescriptorSet> allocate(std::span<const VkDescriptorSetLayoutBinding> bindings, std::span<const VkDescriptorBindingFlags> flags = {});

        void free(DescriptorSet& set) noexcept override;

        void reset() const noexcept;

    private:
        TRef<DescriptorSet> createSet(DescriptorSetLayout& setLayout);

    private:
        const Device& mDevice;
        TRef<DescriptorPool> mPool;
        using SetLayoutMap = std::unordered_map<SetLayoutBindingKey, TRef<DescriptorSetLayout>, SetLayoutBindingKeyHash, SetLayoutBindingKeyEqual>;
        SetLayoutMap mSetLayoutCache;
    };

    // Persistent Descriptor Allocator
    class DescriptorAllocator : public IDescriptorAllocator
    {
    public:
        DescriptorAllocator(const Device& device);

        ~DescriptorAllocator() noexcept;

        DescriptorSetLayout* getSetLayout(std::span<const VkDescriptorSetLayoutBinding> bindings, std::span<const VkDescriptorBindingFlags> flags = {});

        TRef<DescriptorSet> allocate(const DescriptorSetLayout& layout);

        TRef<DescriptorSet> allocate(std::span<const VkDescriptorSetLayoutBinding> bindings, std::span<const VkDescriptorBindingFlags> flags = {});

        void cleanup() noexcept;

    private:
        void free(DescriptorSet& set) noexcept override;

    private:
        static std::unordered_map<VkDescriptorType, uint32_t> buildPoolSizeMap(std::span<const VkDescriptorSetLayoutBinding> bindings);

        static std::vector<VkDescriptorPoolSize> makePoolSizesFromMap(const std::unordered_map<VkDescriptorType, uint32_t>& map);

        struct PoolInfo
        {
            TRef<DescriptorPool> pool;
            uint32_t capacity = 0;
            uint32_t used = 0;
        };

        struct SetLayoutPoolGroup
        {
            TRef<DescriptorSetLayout> layout;
            std::vector<VkDescriptorPoolSize> poolSizes;
            std::vector<PoolInfo> pools;
            std::vector<TRef<DescriptorSet>> freeSets;
        };
        
        using SetLayoutPoolMap = std::unordered_map<SetLayoutBindingKey, SetLayoutPoolGroup, SetLayoutBindingKeyHash, SetLayoutBindingKeyEqual>;

        SetLayoutPoolGroup& ensurePoolGroup(std::span<const VkDescriptorSetLayoutBinding> bindings, std::span<const VkDescriptorBindingFlags> flags);

    private:
        const Device& mDevice;

        SetLayoutPoolMap mSetLayoutPoolMap;
        std::vector<DescriptorSet*> mSets;
        static constexpr uint32_t kBaseCapacity = 1;
        static constexpr uint32_t kMaxCapacity = 256;
    };

    class DescriptorSet : public Refable
    {
        friend class DescriptorAllocator;
    public:
        DescriptorSet(IDescriptorAllocator& allocator, const DescriptorPool& pool, DescriptorSetLayout& layout, VkDescriptorSet set);

        ~DescriptorSet() noexcept;

        const DescriptorPool& getPool() const noexcept
        {
            return mPool;
        }

        DescriptorSetLayout& getLayout() const noexcept
        {
            return mLayout;
        }

        VkDescriptorSet getHandle() const noexcept
        {
            return mDescriptorSet;
        }

        void update(std::span<const VkWriteDescriptorSet> writes) noexcept;

    private:
        IDescriptorAllocator& mAllocator;
        const DescriptorPool& mPool;
        DescriptorSetLayout& mLayout;
        VkDescriptorSet mDescriptorSet{ VK_NULL_HANDLE };
    };

    class DescriptorWriter
    {
    public:
        DescriptorWriter();

        DescriptorWriter(DescriptorWriter&& other) noexcept = default;

        ~DescriptorWriter() noexcept = default;

        void writeBuffer(
            uint32_t binding,
            VkDescriptorType type,
            VkBuffer buffer,
            size_t offset,
            size_t range,
            uint32_t arrayElement = 0
        );

        void writeImage(
            uint32_t binding,
            VkDescriptorType type,
            VkSampler sampler,
            VkImageView imageView,
            VkImageLayout imageLayout,
            uint32_t arrayElement = 0
        );

        void writeImages(
            uint32_t binding,
            VkDescriptorType type,
            std::span<const VkSampler> samplers,
            std::span<const VkImageView> imageViews,
            std::span<const VkImageLayout> imageLayouts,
            uint32_t arrayElement = 0
        );
        
        void writeAccelerationStructure(uint32_t binding, VkAccelerationStructureKHR accelerationStructure);

        void update(DescriptorSet& set) noexcept;

    private:
        static bool isBufferDescriptorType(VkDescriptorType type);

        static bool isImageDescriptorType(VkDescriptorType type);

        static bool isAccelerationStructureType(VkDescriptorType type);

    private:
        std::vector<VkDescriptorBufferInfo> mBufferInfos;
        std::vector<VkDescriptorImageInfo> mImageInfos;
        std::vector<VkAccelerationStructureKHR> mAccelerationStructures;

        std::vector<VkWriteDescriptorSet> mWrites;
        std::vector<uint32_t> mWriteInfoIndices;
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> mAccelerationStructureWrites;
        std::vector<uint32_t> mAccelerationStructureIndices;
    };
}
