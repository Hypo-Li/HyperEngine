#pragma once
#include <RHI/Buffer.h>
#include <RHI/Texture.h>
#include <RHI/CommandList.h>

#include <Core/Common/EnumBitwise.h>
#include <Core/Common/HString.h>

namespace hyp::rg
{
    class PassNode;
    class RenderGraph;

    static constexpr uint16_t kInvalidIndex = static_cast<uint16_t>(-1);

    class alignas(8) ResourceHandle
    {
        friend class RenderGraph;
    public:
        ResourceHandle() noexcept = default;
        ResourceHandle(uint16_t resourceId, uint16_t nodeId, uint16_t version) noexcept
            : mResourceId(resourceId), mNodeId(nodeId), mVersion(version)
        {
        }

        bool isValid() const noexcept
        {
            return mResourceId != kInvalidIndex;
        }

        uint16_t getResourceId() const noexcept
        {
            return mResourceId;
        }

        uint16_t getNodeId() const noexcept
        {
            return mNodeId;
        }

        uint16_t getVersion() const noexcept
        {
            return mVersion;
        }

    private:
        uint16_t mResourceId = static_cast<uint16_t>(-1);
        uint16_t mNodeId = static_cast<uint16_t>(-1);
        uint16_t mVersion = 0;
    };

    struct alignas(8) BufferKey
    {
        rhi::BufferDesc desc;
        HString name;

        bool operator==(const BufferKey& other) const noexcept;
    };

    struct BufferKeyHash
    {
        size_t operator()(const BufferKey& key) const noexcept;
    };

    struct alignas(8) TextureKey
    {
        rhi::TextureDesc desc;
        HString name;

        bool operator==(const TextureKey& other) const noexcept;
    };

    struct TextureKeyHash
    {
        size_t operator()(const TextureKey& key) const noexcept;
    };

    struct alignas(8) SubTextureKey
    {
        rhi::TextureViewDesc desc;
        HString name;

        bool operator==(const SubTextureKey& other) const noexcept;
    };

    struct SubTextureKeyHash
    {
        size_t operator()(const SubTextureKey& key) const noexcept;
    };

    enum class AccessType : uint8_t
    {
        None = 0x0,
        Read_Only = 0x1,
        Write_Only = 0x2,
        Read_Write = Read_Only | Write_Only,

        Stencil_Read_Only = 0x4,
        Stencil_Write_Only = 0x8,
        Stencil_Read_Write = Stencil_Read_Only | Stencil_Write_Only,
    };
    ENABLE_ENUM_CLASS_BITWISE(AccessType)

    enum class ResourceType : uint8_t
    {
        Buffer,
        Texture,
        SubTexture,
    };

    struct BufferAccessRecord
    {
        PassNode* passNode;
        rhi::BufferUsage usage;
        AccessType accessType;
    };

    struct TextureAccessRecord
    {
        PassNode* passNode;
        rhi::TextureUsage usage;
        AccessType accessType;
        rhi::TextureRange range;
    };

    enum class PassType : uint8_t
    {
        Compute,
        Graphics,
        Present,
        Transfer,
    };

    struct PassDependencyRecord
    {
        ResourceHandle resource;
        uint16_t usage;
        AccessType accessType;
        // 在资源访问记录中的索引
        uint32_t accessRecordIndex = UINT32_MAX;
    };
}
