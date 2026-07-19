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
        using Description = rhi::BufferDesc;
        using PhysicalResource = rhi::Buffer;
        using Usage = rhi::BufferUsage;

        BufferResource(HString name, const Description& desc, Arena& arena) noexcept;

        BufferResource(HString name, PhysicalResource* physicalResource, Arena& arena) noexcept;

        virtual void devirtualize(TransientResources& transientResources) noexcept override;

        virtual bool isDevirtualized() const noexcept override
        {
            return mPhysicalResource != nullptr;
        }

        const Description& getDescription() const noexcept
        {
            return mKey.desc;
        }

        PhysicalResource* getPhysicalResource() const noexcept
        {
            return mPhysicalResource;
        }

        std::span<const BufferAccessRecord> getAccessRecords() const noexcept
        {
            return mAccessRecords;
        }

        uint32_t addAccessRecord(PassNode* passNode, rhi::BufferUsage usage, AccessType accessType)
        {
            uint32_t accessRecordIndex = static_cast<uint32_t>(mAccessRecords.size());
            mAccessRecords.emplace_back(passNode, usage, accessType);
            return accessRecordIndex;
        }

    private:
        void aggregateUsage() noexcept;

    private:
        BufferKey mKey;
        TRef<PhysicalResource> mPhysicalResource;
        ArenaVector<BufferAccessRecord> mAccessRecords;
    };

    class TextureResource final : public VirtualResource
    {
    public:
        using Description = rhi::TextureDesc;
        using PhysicalResource = rhi::TextureView;
        using Usage = rhi::TextureUsage;

        TextureResource(HString name, const Description& desc, Arena& arena) noexcept;

        TextureResource(HString name, PhysicalResource* physicalResource, rhi::ResourceState defaultState, Arena& arena) noexcept;

        virtual void devirtualize(TransientResources& transientResources) noexcept override;

        virtual bool isDevirtualized() const noexcept override
        {
            return mPhysicalResource != nullptr;
        }

        const Description& getDescription() const noexcept
        {
            return mKey.desc;
        }

        PhysicalResource* getPhysicalResource() const noexcept
        {
            return mPhysicalResource;
        }

        rhi::ResourceState getDefaultState() const noexcept
        {
            return mDefaultState;
        }

        std::span<const TextureAccessRecord> getAccessRecords() const noexcept
        {
            return mAccessRecords;
        }

        uint32_t addAccessRecord(PassNode* passNode, rhi::TextureUsage usage, AccessType accessType, const rhi::TextureRange& range)
        {
            uint32_t accessRecordIndex = static_cast<uint32_t>(mAccessRecords.size());
            mAccessRecords.emplace_back(passNode, usage, accessType, range);
            return accessRecordIndex;
        }

    private:
        void aggregateUsage() noexcept;

    private:
        TextureKey mKey;
        TRef<PhysicalResource> mPhysicalResource;
        rhi::ResourceState mDefaultState = rhi::ResourceState::None;
        ArenaVector<TextureAccessRecord> mAccessRecords;
    };

    class SubTextureResource final : public VirtualResource
    {
    public:
        using Description = rhi::TextureViewDesc;
        using PhysicalResource = rhi::TextureView;
        using Usage = rhi::TextureUsage;

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
            return mKey.desc;
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
