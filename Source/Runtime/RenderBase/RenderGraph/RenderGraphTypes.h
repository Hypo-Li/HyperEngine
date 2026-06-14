#pragma once
#include "../Vulkan/Buffer.h"
#include "../Vulkan/Image.h"
#include "../Vulkan/RenderPass.h"
#include "../Vulkan/Framebuffer.h"

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

    struct BufferDescription
    {
        VkDeviceSize size;
        VkBufferUsageFlags usage;
    };

    struct alignas(8) BufferKey
    {
        BufferDescription description;
        HString name;

        bool operator==(const BufferKey& other) const noexcept;
    };

    struct BufferKeyHash
    {
        size_t operator()(const BufferKey& key) const noexcept;
    };

    struct TextureDescription
    {
        VkImageType imageType;
        VkFormat format;
        VkExtent3D extent;
        uint32_t mipLevels;
        uint32_t arrayLayers;
        uint8_t samples;
        bool isCube = false;
        VkImageUsageFlags usage;
    };

    struct alignas(8) TextureKey
    {
        TextureDescription description;
        HString name;

        bool operator==(const TextureKey& other) const noexcept;
    };

    struct TextureKeyHash
    {
        size_t operator()(const TextureKey& key) const noexcept;
    };

    struct SubTextureDescription
    {
        VkImageAspectFlags aspectMask;
        uint32_t baseMipLevel;
        uint32_t levelCount;
        uint32_t baseArrayLayer;
        uint32_t layerCount;
        bool isCube = false;
    };

    struct alignas(8) SubTextureKey
    {
        SubTextureDescription description;
        HString name;

        bool operator==(const SubTextureKey& other) const noexcept;
    };

    struct SubTextureKeyHash
    {
        size_t operator()(const SubTextureKey& key) const noexcept;
    };

    enum class AttachmentFormat : uint8_t
    {
        Undefined,
        R8_Unrom,
        R8_Uint,
        R8G8_Unorm,
        R8G8_Uint,
        R8G8B8_Unorm,
        R8G8B8_Srgb,
        R8G8B8_Uint,
        R8G8B8A8_Unorm,
        R8G8B8A8_Srgb,
        R16_Unrom,
        R16_Uint,
        R16_Sfloat,
        R16G16_Unorm,
        R16G16_Uint,
        R16G16_Sfloat,
        R16G16B16_Unorm,
        R16G16B16_Uint,
        R16G16B16_Sfloat,
        R16G16B16A16_Unorm,
        R16G16B16A16_Uint,
        R16G16B16A16_Sfloat,
        R32_Uint,
        R32_Sfloat,
        R32G32_Uint,
        R32G32_Sfloat,
        R32G32B32_Uint,
        R32G32B32_Sfloat,
        R32G32B32A32_Uint,
        R32G32B32A32_Sfloat,
        A2R10G10B10_Unorm,
        A2B10G10R10_Unorm,
        D16_Unorm,
        D24_Unorm_S8_Uint,
        D32_Sfloat,
        D32_Sfloat_S8_Uint,
    };

    VkFormat convertToVkFormat(AttachmentFormat format) noexcept;

    AttachmentFormat convertFromVkFormat(VkFormat format) noexcept;

    enum class AttachmentLoadOp : uint8_t
    {
        Load,
        Clear,
        DontCare,
    };

    VkAttachmentLoadOp convertToVkAttachmentLoadOp(AttachmentLoadOp loadOp) noexcept;

    AttachmentLoadOp convertFromVkAttachmentLoadOp(VkAttachmentLoadOp loadOp) noexcept;

    enum class AttachmentStoreOp : uint8_t
    {
        Store,
        DontCare,
    };

    VkAttachmentStoreOp convertToVkAttachmentStoreOp(AttachmentStoreOp storeOp) noexcept;

    AttachmentFormat convertFromVkAttachmentStoreOp(VkAttachmentStoreOp storeOp) noexcept;

    enum class ClearColorValue : uint8_t
    {
        BlackOpaque,
        WhiteOpaque,
        BlackTransparent,
        WhiteTransparent,
    };

    VkClearColorValue convertToVkClearColorValue(ClearColorValue clearColor) noexcept;

    enum class ClearDepthValue : uint8_t
    {
        One,
        Zero,
    };

    struct ClearDepthStencilValue
    {
        ClearDepthValue depth;
        uint8_t stencil;
    };

    VkClearDepthStencilValue convertToVkClearDepthStencilValue(ClearDepthStencilValue clearDepthStencilValue) noexcept;

    enum class ImageLayout : uint8_t
    {
        Undefined,
        General,
        Color_Attachment_Optimal,
        Depth_Stencil_Attachment_Optimal,
        Depth_Stencil_Read_Only_Optimal,
        Shader_Read_Only_Optimal,
        Transfer_Src_Optimal,
        Transfer_Dst_Optimal,
        Depth_Read_Only_Stencil_Attachment_Optimal,
        Depth_Attachment_Stencil_Read_Only_Optimal,
        Depth_Attachment_Optimal,
        Depth_Read_Only_Optimal,
        Stencil_Attachment_Optimal,
        Stencil_Read_Only_Optimal,
        Attachment_Optimal,
        Present_Src,
    };

    VkImageLayout convertToVkImageLayout(ImageLayout layout) noexcept;

    ImageLayout convertFromVkImageLayout(VkImageLayout layout) noexcept;

    static constexpr uint32_t kMaxColorAttachmentCount = 8;

    struct AttachmentKey
    {
        AttachmentFormat format = AttachmentFormat::Undefined;
        AttachmentLoadOp loadOp = AttachmentLoadOp::DontCare;
        AttachmentStoreOp storeOp = AttachmentStoreOp::DontCare;
        ImageLayout layout = ImageLayout::Undefined;

        bool operator==(const AttachmentKey& other) const noexcept;
    };

    enum class RenderPassFlags : uint8_t
    {
        None = 0,
        Present = 1 << 0,
    };
    ENABLE_ENUM_CLASS_BITWISE(RenderPassFlags)

    struct alignas(8) RenderPassKey
    {
        AttachmentKey colorAttachments[kMaxColorAttachmentCount];
        AttachmentKey depthStencilAttachment;
        AttachmentLoadOp stencilLoadOp = AttachmentLoadOp::DontCare;
        AttachmentStoreOp stencilStoreOp = AttachmentStoreOp::DontCare;
        uint8_t samples = 1;
        RenderPassFlags flags = RenderPassFlags::None;

        bool operator==(const RenderPassKey& other) const noexcept;
    };

    struct RenderPassKeyHash
    {
        size_t operator()(const RenderPassKey& key) const noexcept;
    };

    struct alignas(8) FramebufferKey
    {
        vk::RenderPass* renderPass;
        uint16_t width;
        uint16_t height;
        uint16_t layers;
        uint8_t samples;
        VkImageView colors[kMaxColorAttachmentCount] = { VK_NULL_HANDLE };
        //VkImageView resolve[kMaxColorAttachmentCount];
        VkImageView depthStencil = VK_NULL_HANDLE;

        bool operator==(const FramebufferKey& other) const noexcept;
    };

    struct FramebufferKeyHash
    {
        size_t operator()(const FramebufferKey& key) const noexcept;
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

    enum class BufferUsage : uint8_t
    {
        None        = 0,
        Uniform     = 1 << 0,
        Storage     = 1 << 1,
        Vertex      = 1 << 2,
        Index       = 1 << 3,
        Indirect    = 1 << 4,
        Transfer    = 1 << 5,
    };
    ENABLE_ENUM_CLASS_BITWISE(BufferUsage)

    struct BufferAccessRecord
    {
        PassNode* passNode;
        BufferUsage usage;
        AccessType accessType;
    };

    enum class TextureUsage : uint8_t
    {
        Color_Attachment,
        Depth_Attachment,
        Stencil_Attachment,
        Depth_Stencil_Attachment,
        Sampled,
        Storage,
        Transfer,
    };

    struct TextureAccessRecord
    {
        PassNode* passNode;
        TextureUsage usage;
        AccessType accessType;
        VkImageSubresourceRange range;
    };

    enum class PassType : uint8_t
    {
        Compute,
        Graphics,
        Present,
    };

    struct PassDependencyRecord
    {
        ResourceHandle resource;
        uint16_t usage;
        AccessType accessType;
    };
}
