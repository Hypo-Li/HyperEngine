#pragma once
#include "DAG.h"
#include "TransientResources.h"

#include <Core/Memory/Allocator.h>

namespace hyp::rg
{
    class RenderGraph;
    class PassNode;
    class VirtualResource
    {
        friend class RenderGraph;
    public:
        VirtualResource(HString name, ResourceType type, bool imported) noexcept;

        virtual ~VirtualResource() noexcept = default;

        virtual void devirtualize(TransientResources& transientResource) noexcept = 0;

        virtual bool isDevirtualized() const noexcept = 0;

        HString getName() const noexcept
        {
            return mName;
        }

        ResourceType getType() const noexcept
        {
            return mType;
        }

        bool isImported() const noexcept
        {
            return mIsImported;
        }

        bool isRegistered() const noexcept
        {
            return mIsRegistered;
        }

        void registerPass(PassNode* passNode) noexcept
        {
            if (!mFirstPass)
                mFirstPass = passNode;
            mLastPass = passNode;
            mIsRegistered = true;
        }

        PassNode* getFirstPass() const noexcept
        {
            return mFirstPass;
        }

        PassNode* getLastPass() const noexcept
        {
            return mLastPass;
        }

    private:
        HString mName;
        ResourceType mType;
        bool mIsImported : 1;
        bool mIsRegistered : 1;
        PassNode* mFirstPass = nullptr;
        PassNode* mLastPass = nullptr;
    };

    class BufferResource final : public VirtualResource
    {
    public:
        using Description = BufferDescription;
        using PhysicalResource = vk::Buffer;
        using Usage = BufferUsage;

        BufferResource(HString name, const Description& desc, Arena& arena) noexcept;

        BufferResource(HString name, PhysicalResource* physicalResource, Arena& arena) noexcept;

        virtual void devirtualize(TransientResources& transientResources) noexcept override;

        virtual bool isDevirtualized() const noexcept override
        {
            return mPhysicalResource != nullptr;
        }

        const Description& getDescription() const noexcept
        {
            return mKey.description;
        }

        PhysicalResource* getPhysicalResource() const noexcept
        {
            return mPhysicalResource;
        }

        std::span<const BufferAccessRecord> getAccessRecords() const noexcept
        {
            return mAccessRecords;
        }

        void addAccessRecord(PassNode* passNode, BufferUsage usage, AccessType accessType)
        {
            mAccessRecords.emplace_back(passNode, usage, accessType);
        }

    private:
        void resolveUsage() noexcept;

    private:
        BufferKey mKey;
        TRef<PhysicalResource> mPhysicalResource;
        ArenaVector<BufferAccessRecord> mAccessRecords;
    };

    class TextureResource final : public VirtualResource
    {
    public:
        using Description = TextureDescription;
        using PhysicalResource = vk::ImageView;
        using Usage = TextureUsage;

        TextureResource(HString name, const Description& desc, Arena& arena) noexcept;

        TextureResource(HString name, PhysicalResource* physicalResource, VkImageLayout defaultLayout, Arena& arena) noexcept;

        virtual void devirtualize(TransientResources& transientResources) noexcept override;

        virtual bool isDevirtualized() const noexcept override
        {
            return mPhysicalResource != nullptr;
        }

        const Description& getDescription() const noexcept
        {
            return mKey.description;
        }

        PhysicalResource* getPhysicalResource() const noexcept
        {
            return mPhysicalResource;
        }

        VkImageLayout getDefaultLayout() const noexcept
        {
            return mDefaultLayout;
        }

        std::span<const TextureAccessRecord> getAccessRecords() const noexcept
        {
            return mAccessRecords;
        }

        void addAccessRecord(PassNode* passNode, TextureUsage usage, AccessType accessType, const VkImageSubresourceRange& range)
        {
            mAccessRecords.emplace_back(passNode, usage, accessType, range);
        }

    private:
        void resolveUsage() noexcept;

    private:
        TextureKey mKey;
        TRef<PhysicalResource> mPhysicalResource;
        VkImageLayout mDefaultLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ArenaVector<TextureAccessRecord> mAccessRecords;
    };

    class SubTextureResource final : public VirtualResource
    {
    public:
        using Description = SubTextureDescription;
        using PhysicalResource = vk::ImageView;
        using Usage = TextureUsage;

        SubTextureResource(HString name, uint16_t textureResourceId, TextureResource* textureResource, const Description& desc) noexcept;

        virtual void devirtualize(TransientResources& transientResources) noexcept override;

        virtual bool isDevirtualized() const noexcept override
        {
            return mPhysicalResource != nullptr;
        }

        uint16_t getTextureResourceId() const noexcept
        {
            return mTextureResourceId;
        }

        TextureResource* getTextureResource() const
        {
            return mTextureResource;
        }

        const Description& getDescription() const noexcept
        {
            return mKey.description;
        }

        PhysicalResource* getPhysicalResource() const noexcept
        {
            return mPhysicalResource;
        }

    private:
        uint16_t mTextureResourceId;
        TextureResource* mTextureResource;
        SubTextureKey mKey;
        TRef<PhysicalResource> mPhysicalResource;
    };

    template <typename RESOURCE>
    struct TResourceHandle : public ResourceHandle
    {
        TResourceHandle() noexcept = default;
        TResourceHandle(uint16_t resourceId, uint16_t nodeId, uint16_t version) noexcept : ResourceHandle(resourceId, nodeId, version) {}
        explicit TResourceHandle(const ResourceHandle& other) noexcept : ResourceHandle(other) {}
    };

    using BufferHandle = TResourceHandle<BufferResource>;
    using TextureHandle = TResourceHandle<TextureResource>;

    class ResourceNode final : public DAG::Node
    {
    public:
        ResourceNode(DAG& dag, uint16_t resourceId)
            : Node(dag, Type::Resource), mResourceId(resourceId)
        {
        }

        uint32_t getResourceId() const
        {
            return mResourceId;
        }

    private:
        uint16_t mResourceId;
    };
}
