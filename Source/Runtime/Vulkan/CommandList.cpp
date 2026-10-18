#include "CommandList.h"

#include "BindGroup.h"
#include "Buffer.h"
#include "Pipeline.h"
#include "RenderPassCache.h"
#include "Texture.h"
#include "VkFormatUtil.h"

#include <Core/Common/LogSystem.h>

#include <vector>
#include <cstring>

namespace hyp::vk
{
    static VkIndexType convertIndexType(rhi::IndexType indexType) noexcept
    {
        switch (indexType)
        {
        case rhi::IndexType::Uint16: return VK_INDEX_TYPE_UINT16;
        case rhi::IndexType::Uint32: return VK_INDEX_TYPE_UINT32;
        default:                     return VK_INDEX_TYPE_UINT32;
        }
    }

    static VkAttachmentLoadOp convertLoadOp(rhi::AttachmentLoadOp op) noexcept
    {
        switch (op)
        {
        case rhi::AttachmentLoadOp::Load:      return VK_ATTACHMENT_LOAD_OP_LOAD;
        case rhi::AttachmentLoadOp::Clear:     return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case rhi::AttachmentLoadOp::Dont_Care: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default:                               return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
    }

    static VkAttachmentStoreOp convertStoreOp(rhi::AttachmentStoreOp op) noexcept
    {
        switch (op)
        {
        case rhi::AttachmentStoreOp::Store:     return VK_ATTACHMENT_STORE_OP_STORE;
        case rhi::AttachmentStoreOp::Dont_Care: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        case rhi::AttachmentStoreOp::None:      return VK_ATTACHMENT_STORE_OP_NONE;
        default:                                return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
    }

    static void getBufferPipelineStageAndAccessMask(rhi::ResourceState state, VkPipelineStageFlags& stageMask, VkAccessFlags& accessMask) noexcept
    {
        stageMask = 0;
        accessMask = 0;

        if (state == rhi::ResourceState::None)
        {
            stageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            return;
        }

        if (enumHasAnyFlags(state, rhi::ResourceState::Indirect_Argument))
        {
            stageMask |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
            accessMask |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Index_Attachment))
        {
            stageMask |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            accessMask |= VK_ACCESS_INDEX_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Vertex_Attachment))
        {
            stageMask |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            accessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Graphics_Shader_Uniform))
        {
            stageMask |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            accessMask |= VK_ACCESS_UNIFORM_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Compute_Shader_Uniform))
        {
            stageMask |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            accessMask |= VK_ACCESS_UNIFORM_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Graphics_Shader_Storage_Read))
        {
            stageMask |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            accessMask |= VK_ACCESS_SHADER_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Compute_Shader_Storage_Read))
        {
            stageMask |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            accessMask |= VK_ACCESS_SHADER_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Graphics_Shader_Storage_Write))
        {
            stageMask |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            accessMask |= VK_ACCESS_SHADER_WRITE_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Compute_Shader_Storage_Write))
        {
            stageMask |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            accessMask |= VK_ACCESS_SHADER_WRITE_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Transfer_Src))
        {
            stageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            accessMask |= VK_ACCESS_TRANSFER_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Transfer_Dst))
        {
            stageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            accessMask |= VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Readback))
        {
            stageMask |= VK_PIPELINE_STAGE_HOST_BIT;
            accessMask |= VK_ACCESS_HOST_READ_BIT;
        }
    }

    static void getTexturePipelineStageAndAccessMask(rhi::ResourceState state, VkPipelineStageFlags& stageMask, VkAccessFlags& accessMask) noexcept
    {
        static constexpr VkPipelineStageFlags kGraphicsShaderStages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        static constexpr VkPipelineStageFlags kComputeShaderStages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        static constexpr VkPipelineStageFlags kAllShaderStages = kGraphicsShaderStages | kComputeShaderStages;
        stageMask = 0;
        accessMask = 0;

        switch (state)
        {
        case rhi::ResourceState::Graphics_Shader_Sampled:
            stageMask = kGraphicsShaderStages;
            accessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        case rhi::ResourceState::Compute_Shader_Sampled:
            stageMask = kComputeShaderStages;
            accessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        case rhi::ResourceState::Shader_Sampled:
            stageMask = kAllShaderStages;
            accessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        case rhi::ResourceState::Graphics_Shader_Storage_Read:
            stageMask = kGraphicsShaderStages;
            accessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        case rhi::ResourceState::Compute_Shader_Storage_Read:
            stageMask = kComputeShaderStages;
            accessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        case rhi::ResourceState::Graphics_Shader_Storage_Write:
            stageMask = kGraphicsShaderStages;
            accessMask = VK_ACCESS_SHADER_WRITE_BIT;
            break;
        case rhi::ResourceState::Compute_Shader_Storage_Write:
            stageMask = kComputeShaderStages;
            accessMask = VK_ACCESS_SHADER_WRITE_BIT;
            break;
        case rhi::ResourceState::Graphics_Shader_Storage:
            stageMask = kGraphicsShaderStages;
            accessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            break;
        case rhi::ResourceState::Compute_Shader_Storage:
            stageMask = kComputeShaderStages;
            accessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            break;
        case rhi::ResourceState::Shader_Storage_Read:
            stageMask = kAllShaderStages;
            accessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        case rhi::ResourceState::Shader_Storage_Write:
            stageMask = kAllShaderStages;
            accessMask = VK_ACCESS_SHADER_WRITE_BIT;
            break;
        case rhi::ResourceState::Shader_Storage:
            stageMask = kAllShaderStages;
            accessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            break;
        case rhi::ResourceState::Color_Attachment_Write:
            stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            accessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case rhi::ResourceState::Color_Attachment:
            stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            accessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case rhi::ResourceState::Depth_Attachment_Read:
        case rhi::ResourceState::Stencil_Attachment_Read:
        case rhi::ResourceState::Depth_Stencil_Attachment_Read:
            stageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            break;
        case rhi::ResourceState::Depth_Attachment:
        case rhi::ResourceState::Stencil_Attachment:
        case rhi::ResourceState::Depth_Read_Stencil_Attachment:
        case rhi::ResourceState::Depth_Stencil_Read_Attachment:
        case rhi::ResourceState::Depth_Stencil_Attachment:
            stageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case rhi::ResourceState::Transfer_Src:
            stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            accessMask = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case rhi::ResourceState::Transfer_Dst:
            stageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            accessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case rhi::ResourceState::Readback:
            stageMask = VK_PIPELINE_STAGE_HOST_BIT;
            accessMask = VK_ACCESS_HOST_READ_BIT;
            break;
        case rhi::ResourceState::Present:
            stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            accessMask = 0;
            break;
        case rhi::ResourceState::None:
            stageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            accessMask = 0;
            break;
        default:
            assert(false && "Invalid texture resource state");
            stageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            accessMask = 0;
            break;
        }
    }

    static void getBufferPipelineStageAndAccessMask2(rhi::ResourceState state, VkPipelineStageFlags2& stageMask, VkAccessFlags2& accessMask) noexcept
    {
        stageMask = 0;
        accessMask = 0;

        if (state == rhi::ResourceState::None)
        {
            stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            return;
        }

        if (enumHasAnyFlags(state, rhi::ResourceState::Indirect_Argument))
        {
            stageMask |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            accessMask |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Index_Attachment))
        {
            stageMask |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
            accessMask |= VK_ACCESS_2_INDEX_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Vertex_Attachment))
        {
            stageMask |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
            accessMask |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Graphics_Shader_Uniform))
        {
            stageMask |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            accessMask |= VK_ACCESS_2_UNIFORM_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Compute_Shader_Uniform))
        {
            stageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            accessMask |= VK_ACCESS_2_UNIFORM_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Graphics_Shader_Storage_Read))
        {
            stageMask |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            accessMask |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Compute_Shader_Storage_Read))
        {
            stageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            accessMask |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Graphics_Shader_Storage_Write))
        {
            stageMask |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            accessMask |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Compute_Shader_Storage_Write))
        {
            stageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            accessMask |= VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Transfer_Src))
        {
            stageMask |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            accessMask |= VK_ACCESS_2_TRANSFER_READ_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Transfer_Dst))
        {
            stageMask |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            accessMask |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }
        if (enumHasAnyFlags(state, rhi::ResourceState::Readback))
        {
            stageMask |= VK_PIPELINE_STAGE_2_HOST_BIT;
            accessMask |= VK_ACCESS_2_HOST_READ_BIT;
        }
    }

    static void getTexturePipelineStageAndAccessMask2(rhi::ResourceState state, VkPipelineStageFlags2& stageMask, VkAccessFlags2& accessMask) noexcept
    {
        static constexpr VkPipelineStageFlags2 kGraphicsShaderStages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        static constexpr VkPipelineStageFlags2 kComputeShaderStages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        static constexpr VkPipelineStageFlags2 kAllShaderStages = kGraphicsShaderStages | kComputeShaderStages;
        stageMask = 0;
        accessMask = 0;

        switch (state)
        {
        case rhi::ResourceState::Graphics_Shader_Sampled:
            stageMask = kGraphicsShaderStages;
            accessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            break;
        case rhi::ResourceState::Compute_Shader_Sampled:
            stageMask = kComputeShaderStages;
            accessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            break;
        case rhi::ResourceState::Shader_Sampled:
            stageMask = kAllShaderStages;
            accessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            break;
        case rhi::ResourceState::Graphics_Shader_Storage_Read:
            stageMask = kGraphicsShaderStages;
            accessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            break;
        case rhi::ResourceState::Compute_Shader_Storage_Read:
            stageMask = kComputeShaderStages;
            accessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            break;
        case rhi::ResourceState::Graphics_Shader_Storage_Write:
            stageMask = kGraphicsShaderStages;
            accessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            break;
        case rhi::ResourceState::Compute_Shader_Storage_Write:
            stageMask = kComputeShaderStages;
            accessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            break;
        case rhi::ResourceState::Graphics_Shader_Storage:
            stageMask = kGraphicsShaderStages;
            accessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            break;
        case rhi::ResourceState::Compute_Shader_Storage:
            stageMask = kComputeShaderStages;
            accessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            break;
        case rhi::ResourceState::Shader_Storage_Read:
            stageMask = kAllShaderStages;
            accessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
            break;
        case rhi::ResourceState::Shader_Storage_Write:
            stageMask = kAllShaderStages;
            accessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            break;
        case rhi::ResourceState::Shader_Storage:
            stageMask = kAllShaderStages;
            accessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            break;
        case rhi::ResourceState::Color_Attachment_Write:
            stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case rhi::ResourceState::Color_Attachment:
            stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            accessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case rhi::ResourceState::Depth_Attachment_Read:
        case rhi::ResourceState::Stencil_Attachment_Read:
        case rhi::ResourceState::Depth_Stencil_Attachment_Read:
            stageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            break;
        case rhi::ResourceState::Depth_Attachment:
        case rhi::ResourceState::Stencil_Attachment:
        case rhi::ResourceState::Depth_Read_Stencil_Attachment:
        case rhi::ResourceState::Depth_Stencil_Read_Attachment:
        case rhi::ResourceState::Depth_Stencil_Attachment:
            stageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            accessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case rhi::ResourceState::Transfer_Src:
            stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            accessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            break;
        case rhi::ResourceState::Transfer_Dst:
            stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            accessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            break;
        case rhi::ResourceState::Readback:
            stageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            accessMask = VK_ACCESS_2_HOST_READ_BIT;
            break;
        case rhi::ResourceState::Present:
            stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            accessMask = VK_ACCESS_2_NONE;
            break;
        case rhi::ResourceState::None:
            stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            accessMask = VK_ACCESS_2_NONE;
            break;
        default:
            assert(false && "Invalid texture resource state");
            stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            accessMask = VK_ACCESS_2_NONE;
            break;
        }
    }

    // NVIDIA 驱动 617.14 在源阶段为空的 barrier 之后会丢掉对更早 compute dispatch 的等待
    template <typename StageFlags>
    static void replaceEmptyComputeBarrierSource(rhi::ResourceState oldState, StageFlags dstStageMask, StageFlags& srcStageMask) noexcept
    {
        if (oldState == rhi::ResourceState::None && (dstStageMask & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT))
            srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }

    static VkImageLayout getImageLayout(rhi::ResourceState state, bool presentSrc = false) noexcept
    {
        switch (state)
        {
        case rhi::ResourceState::Present:
            return presentSrc ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        case rhi::ResourceState::Color_Attachment_Write:
        case rhi::ResourceState::Color_Attachment:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case rhi::ResourceState::Depth_Attachment_Read:
            return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        case rhi::ResourceState::Depth_Attachment:
            return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        case rhi::ResourceState::Stencil_Attachment_Read:
            return VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL;
        case rhi::ResourceState::Stencil_Attachment:
            return VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
        case rhi::ResourceState::Depth_Stencil_Attachment_Read:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case rhi::ResourceState::Depth_Read_Stencil_Attachment:
            return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;
        case rhi::ResourceState::Depth_Stencil_Read_Attachment:
            return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
        case rhi::ResourceState::Depth_Stencil_Attachment:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case rhi::ResourceState::Transfer_Dst:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case rhi::ResourceState::Transfer_Src:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case rhi::ResourceState::Graphics_Shader_Sampled:
        case rhi::ResourceState::Compute_Shader_Sampled:
        case rhi::ResourceState::Shader_Sampled:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case rhi::ResourceState::Graphics_Shader_Storage_Read:
        case rhi::ResourceState::Graphics_Shader_Storage_Write:
        case rhi::ResourceState::Graphics_Shader_Storage:
        case rhi::ResourceState::Compute_Shader_Storage_Read:
        case rhi::ResourceState::Compute_Shader_Storage_Write:
        case rhi::ResourceState::Compute_Shader_Storage:
        case rhi::ResourceState::Shader_Storage_Read:
        case rhi::ResourceState::Shader_Storage_Write:
        case rhi::ResourceState::Shader_Storage:
        case rhi::ResourceState::Readback:
            return VK_IMAGE_LAYOUT_GENERAL;
        case rhi::ResourceState::None:
            return VK_IMAGE_LAYOUT_UNDEFINED;
        default:
            assert(false && "Invalid texture resource state");
            return VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    CommandList::CommandList(const Device& device, rhi::QueueType queueType, VkCommandPool commandPool) :
        rhi::CommandList(device, queueType),
        mCommandPool(commandPool),
        mDynamicRendering(device.getVkFeatures().dynamicRendering),
        mSynchronization2(device.getVkFeatures().synchronization2)
    {
        VkCommandBufferAllocateInfo commandBufferInfo = {};
        commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferInfo.commandPool = commandPool;
        commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(device.getHandle(), &commandBufferInfo, &mCommandBuffer);
    }

    CommandList::~CommandList()
    {
        vkFreeCommandBuffers(getVkDevice().getHandle(), mCommandPool, 1, &mCommandBuffer);
    }

    void CommandList::reset()
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        rhi::CommandList::reset();
        vkResetCommandBuffer(mCommandBuffer, 0);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(mCommandBuffer, &beginInfo);
#else
        rhi::CommandList::reset();
        vkResetCommandBuffer(mCommandBuffer, 0);
#endif
    }

    void CommandList::decode()
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        vkEndCommandBuffer(mCommandBuffer);
#else
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(mCommandBuffer, &beginInfo);

        std::span<const uint8_t> storage = getStorage();
        const uint8_t* ptr = storage.data();
        const uint8_t* end = ptr + storage.size();

        while (ptr < end)
        {
            ptr = alignPointer(ptr, 8);
            if (ptr >= end)
                break;

            const auto* header = reinterpret_cast<const rhi::CommandHeader*>(ptr);
            switch (header->type)
            {
            case rhi::CommandType::Bind_Pipeline:
            {
                const auto* cmd = reinterpret_cast<const rhi::BindPipelineCommand*>(ptr);
                bindPipelineBackend(cmd->pipeline);
                ptr += sizeof(rhi::BindPipelineCommand);
                break;
            }
            case rhi::CommandType::Set_Bind_Group:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetBindGroupCommand*>(ptr);
                const uint8_t* trailing = alignPointer(ptr + sizeof(rhi::SetBindGroupCommand), alignof(uint32_t));
                const uint32_t* dynamicOffsets = reinterpret_cast<const uint32_t*>(trailing);

                setBindGroupBackend(
                    cmd->index,
                    cmd->bindPoint,
                    cmd->pipelineLayout,
                    cmd->bindGroup,
                    std::span(dynamicOffsets, cmd->dynamicOffsetCount)
                );

                ptr = trailing + cmd->dynamicOffsetCount * sizeof(uint32_t);
                break;
            }
            case rhi::CommandType::Bind_Vertex_Buffers:
            {
                const auto* cmd = reinterpret_cast<const rhi::BindVertexBuffersCommand*>(ptr);
                const uint8_t* buffersPtr = alignPointer(ptr + sizeof(rhi::BindVertexBuffersCommand), alignof(rhi::Buffer*));
                const rhi::Buffer* const* buffers = reinterpret_cast<const rhi::Buffer* const*>(buffersPtr);
                const uint8_t* offsetsPtr = alignPointer(buffersPtr + cmd->bindingCount * sizeof(rhi::Buffer*), alignof(uint64_t));
                const uint64_t* offsets = reinterpret_cast<const uint64_t*>(offsetsPtr);

                bindVertexBuffersBackend(
                    cmd->firstBinding,
                    std::span(buffers, cmd->bindingCount),
                    std::span(offsets, cmd->bindingCount)
                );

                ptr = offsetsPtr + cmd->bindingCount * sizeof(uint64_t);
                break;
            }
            case rhi::CommandType::Bind_Index_Buffer:
            {
                const auto* cmd = reinterpret_cast<const rhi::BindIndexBufferCommand*>(ptr);
                bindIndexBufferBackend(cmd->buffer, cmd->offset, cmd->indexType);
                ptr += sizeof(rhi::BindIndexBufferCommand);
                break;
            }
            case rhi::CommandType::Set_Viewport:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetViewportCommand*>(ptr);
                setViewportBackend(cmd->x, cmd->y, cmd->width, cmd->height, cmd->minDepth, cmd->maxDepth);
                ptr += sizeof(rhi::SetViewportCommand);
                break;
            }
            case rhi::CommandType::Set_Scissor:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetScissorCommand*>(ptr);
                setScissorBackend(cmd->x, cmd->y, cmd->width, cmd->height);
                ptr += sizeof(rhi::SetScissorCommand);
                break;
            }
            case rhi::CommandType::Set_Blend_Constants:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetBlendConstantsCommand*>(ptr);
                setBlendConstantsBackend(cmd->blendConstants);
                ptr += sizeof(rhi::SetBlendConstantsCommand);
                break;
            }
            case rhi::CommandType::Set_Stencil_Reference:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetStencilReferenceCommand*>(ptr);
                setStencilReferenceBackend(cmd->reference);
                ptr += sizeof(rhi::SetStencilReferenceCommand);
                break;
            }
            case rhi::CommandType::Set_Immediate_Data:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetImmediateDataCommand*>(ptr);
                const uint8_t* data = ptr + sizeof(rhi::SetImmediateDataCommand);
                setImmediateDataBackend(cmd->pipelineLayout, cmd->offset, std::span(data, cmd->size));
                ptr = data + cmd->size;
                break;
            }
            case rhi::CommandType::Begin_Render_Pass:
            {
                const auto* cmd = reinterpret_cast<const rhi::BeginRenderPassCommand*>(ptr);
                const uint8_t* attachmentsPtr = alignPointer(ptr + sizeof(rhi::BeginRenderPassCommand), alignof(rhi::ColorAttachmentInfo));
                const rhi::ColorAttachmentInfo* colorAttachments = reinterpret_cast<const rhi::ColorAttachmentInfo*>(attachmentsPtr);

                beginRenderPassBackend(
                    cmd->renderArea,
                    cmd->depthStencilAttachment,
                    std::span(colorAttachments, cmd->colorAttachmentCount)
                );

                ptr = attachmentsPtr + cmd->colorAttachmentCount * sizeof(rhi::ColorAttachmentInfo);
                break;
            }
            case rhi::CommandType::End_Render_Pass:
            {
                endRenderPassBackend();
                ptr += sizeof(rhi::EndRenderPassCommand);
                break;
            }
            case rhi::CommandType::Begin_Compute_Pass:
            {
                beginComputePassBackend();
                ptr += sizeof(rhi::BeginComputePassCommand);
                break;
            }
            case rhi::CommandType::End_Compute_Pass:
            {
                endComputePassBackend();
                ptr += sizeof(rhi::EndComputePassCommand);
                break;
            }
            case rhi::CommandType::Draw:
            {
                const auto* cmd = reinterpret_cast<const rhi::DrawCommand*>(ptr);
                drawBackend(cmd->vertexCount, cmd->instanceCount, cmd->firstVertex, cmd->firstInstance);
                ptr += sizeof(rhi::DrawCommand);
                break;
            }
            case rhi::CommandType::Draw_Indexed:
            {
                const auto* cmd = reinterpret_cast<const rhi::DrawIndexedCommand*>(ptr);
                drawIndexedBackend(cmd->indexCount, cmd->instanceCount, cmd->firstIndex, cmd->vertexOffset, cmd->firstInstance);
                ptr += sizeof(rhi::DrawIndexedCommand);
                break;
            }
            case rhi::CommandType::Draw_Indirect:
            {
                const auto* cmd = reinterpret_cast<const rhi::DrawIndirectCommand*>(ptr);
                drawIndirectBackend(cmd->buffer, cmd->offset, cmd->drawCount, cmd->stride);
                ptr += sizeof(rhi::DrawIndirectCommand);
                break;
            }
            case rhi::CommandType::Draw_Indexed_Indirect:
            {
                const auto* cmd = reinterpret_cast<const rhi::DrawIndexedIndirectCommand*>(ptr);
                drawIndexedIndirectBackend(cmd->buffer, cmd->offset, cmd->drawCount, cmd->stride);
                ptr += sizeof(rhi::DrawIndexedIndirectCommand);
                break;
            }
            case rhi::CommandType::Dispatch:
            {
                const auto* cmd = reinterpret_cast<const rhi::DispatchCommand*>(ptr);
                dispatchBackend(cmd->groupCountX, cmd->groupCountY, cmd->groupCountZ);
                ptr += sizeof(rhi::DispatchCommand);
                break;
            }
            case rhi::CommandType::Dispatch_Indirect:
            {
                const auto* cmd = reinterpret_cast<const rhi::DispatchIndirectCommand*>(ptr);
                dispatchIndirectBackend(cmd->buffer, cmd->offset);
                ptr += sizeof(rhi::DispatchIndirectCommand);
                break;
            }
            case rhi::CommandType::Barrier:
            {
                const auto* cmd = reinterpret_cast<const rhi::BarrierCommand*>(ptr);
                const uint8_t* bufferBarriersPtr = alignPointer(ptr + sizeof(rhi::BarrierCommand), alignof(rhi::BufferBarrier));
                const rhi::BufferBarrier* bufferBarriers = reinterpret_cast<const rhi::BufferBarrier*>(bufferBarriersPtr);
                const uint8_t* textureBarriersPtr = alignPointer(bufferBarriersPtr + cmd->bufferBarrierCount * sizeof(rhi::BufferBarrier), alignof(rhi::TextureBarrier));
                const rhi::TextureBarrier* textureBarriers = reinterpret_cast<const rhi::TextureBarrier*>(textureBarriersPtr);

                insertBarriersBackend(
                    std::span(bufferBarriers, cmd->bufferBarrierCount),
                    std::span(textureBarriers, cmd->textureBarrierCount)
                );

                ptr = textureBarriersPtr + cmd->textureBarrierCount * sizeof(rhi::TextureBarrier);
                break;
            }
            case rhi::CommandType::Copy_Buffer:
            {
                const auto* cmd = reinterpret_cast<const rhi::CopyBufferCommand*>(ptr);
                const uint8_t* regionsPtr = alignPointer(ptr + sizeof(rhi::CopyBufferCommand), alignof(rhi::BufferCopyRegion));
                const rhi::BufferCopyRegion* regions = reinterpret_cast<const rhi::BufferCopyRegion*>(regionsPtr);

                copyBufferBackend(cmd->srcBuffer, cmd->dstBuffer, std::span(regions, cmd->regionCount));

                ptr = regionsPtr + cmd->regionCount * sizeof(rhi::BufferCopyRegion);
                break;
            }
            case rhi::CommandType::Copy_Buffer_To_Texture:
            {
                const auto* cmd = reinterpret_cast<const rhi::CopyBufferToTextureCommand*>(ptr);
                const uint8_t* regionsPtr = alignPointer(ptr + sizeof(rhi::CopyBufferToTextureCommand), alignof(rhi::BufferTextureCopyRegion));
                const rhi::BufferTextureCopyRegion* regions = reinterpret_cast<const rhi::BufferTextureCopyRegion*>(regionsPtr);

                copyBufferToTextureBackend(cmd->srcBuffer, cmd->dstTexture, std::span(regions, cmd->regionCount));

                ptr = regionsPtr + cmd->regionCount * sizeof(rhi::BufferTextureCopyRegion);
                break;
            }
            case rhi::CommandType::Clear_Buffer:
            {
                const auto* cmd = reinterpret_cast<const rhi::ClearBufferCommand*>(ptr);
                clearBufferBackend(cmd->dstBuffer, cmd->dstOffset, cmd->size);
                ptr += sizeof(rhi::ClearBufferCommand);
                break;
            }
            case rhi::CommandType::Copy_Texture:
            {
                const auto* cmd = reinterpret_cast<const rhi::CopyTextureCommand*>(ptr);
                const uint8_t* regionsPtr = alignPointer(ptr + sizeof(rhi::CopyTextureCommand), alignof(rhi::TextureCopyRegion));
                const rhi::TextureCopyRegion* regions = reinterpret_cast<const rhi::TextureCopyRegion*>(regionsPtr);

                copyTextureBackend(cmd->srcTexture, cmd->dstTexture, std::span(regions, cmd->regionCount));

                ptr = regionsPtr + cmd->regionCount * sizeof(rhi::TextureCopyRegion);
                break;
            }
            case rhi::CommandType::Copy_Texture_To_Buffer:
            {
                const auto* cmd = reinterpret_cast<const rhi::CopyTextureToBufferCommand*>(ptr);
                const uint8_t* regionsPtr = alignPointer(ptr + sizeof(rhi::CopyTextureToBufferCommand), alignof(rhi::BufferTextureCopyRegion));
                const rhi::BufferTextureCopyRegion* regions = reinterpret_cast<const rhi::BufferTextureCopyRegion*>(regionsPtr);

                copyTextureToBufferBackend(cmd->srcTexture, cmd->dstBuffer, std::span(regions, cmd->regionCount));

                ptr = regionsPtr + cmd->regionCount * sizeof(rhi::BufferTextureCopyRegion);
                break;
            }
            case rhi::CommandType::Begin_Debug_Group:
            {
                const auto* cmd = reinterpret_cast<const rhi::BeginDebugGroupCommand*>(ptr);
                beginDebugGroupBackend(cmd->label, cmd->color);
                ptr += sizeof(rhi::BeginDebugGroupCommand);
                break;
            }
            case rhi::CommandType::End_Debug_Group:
            {
                endDebugGroupBackend();
                ptr += sizeof(rhi::EndDebugGroupCommand);
                break;
            }
            case rhi::CommandType::Insert_Debug_Marker:
            {
                const auto* cmd = reinterpret_cast<const rhi::InsertDebugMarkerCommand*>(ptr);
                insertDebugMarkerBackend(cmd->label, cmd->color);
                ptr += sizeof(rhi::InsertDebugMarkerCommand);
                break;
            }
            case rhi::CommandType::Native_Callback:
            {
                const auto* cmd = reinterpret_cast<const rhi::NativeCallbackCommand*>(ptr);
                invokeNativeCallbackBackend(*cmd->callback);
                ptr += sizeof(rhi::NativeCallbackCommand);
                break;
            }
            default:
            {
                LOG_ERROR("Unknown RHI command type");
                return;
            }
            }
        }

        vkEndCommandBuffer(mCommandBuffer);
#endif
    }

    void CommandList::bindPipelineBackend(const rhi::Pipeline* pipeline)
    {
        auto* vkPipeline = static_cast<const Pipeline*>(pipeline);
        vkCmdBindPipeline(mCommandBuffer, vkPipeline->getBindPoint(), vkPipeline->getHandle());
    }

    void CommandList::setBindGroupBackend(uint32_t index, rhi::PipelineBindPoint bindPoint, const rhi::PipelineLayout* pipelineLayout, const rhi::BindGroup* bindGroup, std::span<const uint32_t> dynamicOffsets)
    {
        VkPipelineLayout layout = static_cast<const PipelineLayout*>(pipelineLayout)->getHandle();
        VkDescriptorSet descriptorSet = static_cast<const BindGroup*>(bindGroup)->getHandle();
        VkPipelineBindPoint vkBindPoint = bindPoint == rhi::PipelineBindPoint::Compute
            ? VK_PIPELINE_BIND_POINT_COMPUTE
            : VK_PIPELINE_BIND_POINT_GRAPHICS;

        vkCmdBindDescriptorSets(
            mCommandBuffer,
            vkBindPoint,
            layout,
            index,
            1,
            &descriptorSet,
            static_cast<uint32_t>(dynamicOffsets.size()),
            dynamicOffsets.data()
        );
    }

    void CommandList::bindVertexBuffersBackend(uint32_t firstBinding, std::span<const rhi::Buffer* const> buffers, std::span<const uint64_t> offsets)
    {
        static constexpr uint32_t kMaxVertexBindings = 16;
        VkBuffer vkBuffers[kMaxVertexBindings];
        VkDeviceSize vkOffsets[kMaxVertexBindings];
        uint32_t count = std::min(static_cast<uint32_t>(buffers.size()), kMaxVertexBindings);

        for (uint32_t i = 0; i < count; ++i)
        {
            auto* buffer = static_cast<const Buffer*>(buffers[i]);
            vkBuffers[i] = buffer ? buffer->getHandle() : VK_NULL_HANDLE;
            vkOffsets[i] = offsets[i];
        }

        vkCmdBindVertexBuffers(mCommandBuffer, firstBinding, count, vkBuffers, vkOffsets);
    }

    void CommandList::bindIndexBufferBackend(const rhi::Buffer* buffer, uint64_t offset, rhi::IndexType indexType)
    {
        VkBuffer vkBuffer = buffer ? static_cast<const Buffer*>(buffer)->getHandle() : VK_NULL_HANDLE;
        vkCmdBindIndexBuffer(mCommandBuffer, vkBuffer, offset, convertIndexType(indexType));
    }

    void CommandList::setViewportBackend(float x, float y, float width, float height, float minDepth, float maxDepth)
    {
        VkViewport viewport = {};
        viewport.x = x;
        viewport.y = y + height;
        viewport.width = width;
        viewport.height = -height;
        viewport.minDepth = minDepth;
        viewport.maxDepth = maxDepth;
        vkCmdSetViewport(mCommandBuffer, 0, 1, &viewport);
    }

    void CommandList::setScissorBackend(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        VkRect2D scissor = {};
        scissor.offset.x = static_cast<int32_t>(x);
        scissor.offset.y = static_cast<int32_t>(y);
        scissor.extent.width = width;
        scissor.extent.height = height;
        vkCmdSetScissor(mCommandBuffer, 0, 1, &scissor);
    }

    void CommandList::setImmediateDataBackend(const rhi::PipelineLayout* pipelineLayout, uint32_t offset, std::span<const uint8_t> data)
    {
        VkPipelineLayout layout = static_cast<const PipelineLayout*>(pipelineLayout)->getHandle();
        vkCmdPushConstants(mCommandBuffer, layout, VK_SHADER_STAGE_ALL, offset, static_cast<uint32_t>(data.size()), data.data());
    }

    void CommandList::setBlendConstantsBackend(float4 blendConstants)
    {
        const float constants[4] = {
            blendConstants.x,
            blendConstants.y,
            blendConstants.z,
            blendConstants.w
        };
        vkCmdSetBlendConstants(mCommandBuffer, constants);
    }

    void CommandList::setStencilReferenceBackend(uint32_t reference)
    {
        vkCmdSetStencilReference(mCommandBuffer, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
    }

    void CommandList::beginRenderPassBackend(rhi::RenderArea renderArea, const rhi::DepthStencilAttachmentInfo& depthStencilAttachment, std::span<const rhi::ColorAttachmentInfo> colorAttachments)
    {
        if (colorAttachments.size() > RenderPassKey::kMaxColorAttachments)
        {
            LOG_ERROR(
                "Vulkan render pass supports at most {} color attachments, got {}",
                RenderPassKey::kMaxColorAttachments,
                colorAttachments.size()
            );
            return;
        }

        if (mDynamicRendering)
        {
            std::array<VkRenderingAttachmentInfo, RenderPassKey::kMaxColorAttachments> vkColorAttachments{};
            for (uint32_t i = 0; i < colorAttachments.size(); ++i)
            {
                const auto& src = colorAttachments[i];
                VkRenderingAttachmentInfo info = {};
                info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                info.imageView = src.textureView ? static_cast<const TextureView*>(src.textureView)->getHandle() : VK_NULL_HANDLE;
                info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                info.loadOp = convertLoadOp(src.loadOp);
                info.storeOp = convertStoreOp(src.storeOp);
                std::memcpy(&info.clearValue.color, &src.clearColor, sizeof(src.clearColor));
                vkColorAttachments[i] = info;
            }

            VkRenderingAttachmentInfo depthAttachment = {};
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            VkRenderingAttachmentInfo stencilAttachment = {};
            stencilAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            const VkRenderingAttachmentInfo* pDepthAttachment = nullptr;
            const VkRenderingAttachmentInfo* pStencilAttachment = nullptr;

            if (depthStencilAttachment.textureView)
            {
                const auto& ds = depthStencilAttachment;
                const auto* view = static_cast<const TextureView*>(ds.textureView);
                VkImageAspectFlags aspect = convertToVkImageAspectFlags(view->getDesc().range.aspect);
                VkImageLayout layout = getImageLayout(ds.state);

                if (aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
                {
                    depthAttachment.imageView = view->getHandle();
                    depthAttachment.imageLayout = layout;
                    depthAttachment.loadOp = convertLoadOp(ds.depthLoadOp);
                    depthAttachment.storeOp = convertStoreOp(ds.depthStoreOp);
                    depthAttachment.clearValue.depthStencil = { ds.clearDepthStencil.depth, ds.clearDepthStencil.stencil };
                    pDepthAttachment = &depthAttachment;
                }

                if (aspect & VK_IMAGE_ASPECT_STENCIL_BIT)
                {
                    stencilAttachment.imageView = view->getHandle();
                    stencilAttachment.imageLayout = layout;
                    stencilAttachment.loadOp = convertLoadOp(ds.stencilLoadOp);
                    stencilAttachment.storeOp = convertStoreOp(ds.stencilStoreOp);
                    stencilAttachment.clearValue.depthStencil = { ds.clearDepthStencil.depth, ds.clearDepthStencil.stencil };
                    pStencilAttachment = &stencilAttachment;
                }
            }

            VkRenderingInfo renderingInfo = {};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.offset = { renderArea.x, renderArea.y };
            renderingInfo.renderArea.extent = { renderArea.width, renderArea.height };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
            renderingInfo.pColorAttachments = vkColorAttachments.data();
            renderingInfo.pDepthAttachment = pDepthAttachment;
            renderingInfo.pStencilAttachment = pStencilAttachment;

            getVkDevice().getDispatch().cmdBeginRendering(mCommandBuffer, &renderingInfo);
        }
        else
        {
            std::array<VkFormat, RenderPassKey::kMaxColorAttachments> colorFormats{};
            std::array<VkAttachmentLoadOp, RenderPassKey::kMaxColorAttachments> colorLoadOps{};
            std::array<VkAttachmentStoreOp, RenderPassKey::kMaxColorAttachments> colorStoreOps{};
            std::array<VkImageView, RenderPassKey::kMaxColorAttachments> colorViews{};

            for (uint32_t i = 0; i < colorAttachments.size(); ++i)
            {
                const auto& src = colorAttachments[i];
                const TextureView* view = static_cast<const TextureView*>(src.textureView);
                colorFormats[i] = convertToVkFormat(view->getDesc().format);
                colorLoadOps[i] = convertLoadOp(src.loadOp);
                colorStoreOps[i] = convertStoreOp(src.storeOp);
                colorViews[i] = view->getHandle();
            }

            VkFormat depthStencilFormat = VK_FORMAT_UNDEFINED;
            VkAttachmentLoadOp depthLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            VkAttachmentStoreOp depthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            VkAttachmentStoreOp stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkImageView depthStencilView = VK_NULL_HANDLE;

            if (depthStencilAttachment.textureView)
            {
                const auto& ds = depthStencilAttachment;
                const TextureView* view = static_cast<const TextureView*>(ds.textureView);
                depthStencilFormat = convertToVkFormat(view->getDesc().format);
                depthLoadOp = convertLoadOp(ds.depthLoadOp);
                depthStoreOp = convertStoreOp(ds.depthStoreOp);
                stencilLoadOp = convertLoadOp(ds.stencilLoadOp);
                stencilStoreOp = convertStoreOp(ds.stencilStoreOp);
                depthStencilView = view->getHandle();
            }

            VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
            if (colorAttachments.size() > 0)
            {
                const TextureView* firstView = static_cast<const TextureView*>(colorAttachments[0].textureView);
                sampleCount = static_cast<VkSampleCountFlagBits>(firstView->getVkTexture().getDesc().samples);
            }
            else if (depthStencilAttachment.textureView)
            {
                const TextureView* dsView = static_cast<const TextureView*>(depthStencilAttachment.textureView);
                sampleCount = static_cast<VkSampleCountFlagBits>(dsView->getVkTexture().getDesc().samples);
            }

            VkRenderPass renderPass = getVkDevice().getRenderPassCache().getOrCreateRenderPass(
                { colorFormats.data(), static_cast<uint32_t>(colorAttachments.size()) },
                { colorLoadOps.data(), static_cast<uint32_t>(colorAttachments.size()) },
                { colorStoreOps.data(), static_cast<uint32_t>(colorAttachments.size()) },
                depthStencilFormat,
                depthLoadOp,
                depthStoreOp,
                stencilLoadOp,
                stencilStoreOp,
                getImageLayout(depthStencilAttachment.state),
                sampleCount
            );

            std::array<VkImageView, RenderPassKey::kMaxColorAttachments + 1> attachments{};
            uint32_t attachmentCount = static_cast<uint32_t>(colorAttachments.size());
            for (uint32_t i = 0; i < colorAttachments.size(); ++i)
            {
                attachments[i] = colorViews[i];
            }

            if (depthStencilView != VK_NULL_HANDLE)
            {
                attachments[attachmentCount] = depthStencilView;
                attachmentCount++;
            }

            VkFramebuffer framebuffer = getVkDevice().getFramebufferCache().getOrCreateFramebuffer(
                renderPass,
                { attachments.data(), attachmentCount },
                renderArea.width,
                renderArea.height,
                1
            );

            std::array<VkClearValue, 9> clearValues{};
            uint32_t clearValueCount = 0;
            for (uint32_t i = 0; i < colorAttachments.size(); ++i)
            {
                std::memcpy(&clearValues[clearValueCount].color, &colorAttachments[i].clearColor, sizeof(colorAttachments[i].clearColor));
                clearValueCount++;
            }

            if (depthStencilView != VK_NULL_HANDLE)
            {
                clearValues[clearValueCount].depthStencil = {
                    depthStencilAttachment.clearDepthStencil.depth,
                    depthStencilAttachment.clearDepthStencil.stencil
                };
                clearValueCount++;
            }

            VkRenderPassBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            beginInfo.renderPass = renderPass;
            beginInfo.framebuffer = framebuffer;
            beginInfo.renderArea.offset = { renderArea.x, renderArea.y };
            beginInfo.renderArea.extent = { renderArea.width, renderArea.height };
            beginInfo.clearValueCount = clearValueCount;
            beginInfo.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(mCommandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
        }
    }

    void CommandList::endRenderPassBackend()
    {
        if (mDynamicRendering)
        {
            getVkDevice().getDispatch().cmdEndRendering(mCommandBuffer);
        }
        else
        {
            vkCmdEndRenderPass(mCommandBuffer);
        }
    }

    void CommandList::beginComputePassBackend()
    {
        // Vulkan does not require explicit compute pass markers; ignored.
    }

    void CommandList::endComputePassBackend()
    {
        // Vulkan does not require explicit compute pass markers; ignored.
    }

    void CommandList::drawBackend(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        vkCmdDraw(mCommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void CommandList::drawIndexedBackend(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        vkCmdDrawIndexed(mCommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void CommandList::drawIndirectBackend(const rhi::Buffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)
    {
        VkBuffer vkBuffer = buffer ? static_cast<const Buffer*>(buffer)->getHandle() : VK_NULL_HANDLE;
        vkCmdDrawIndirect(mCommandBuffer, vkBuffer, offset, drawCount, stride);
    }

    void CommandList::drawIndexedIndirectBackend(const rhi::Buffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)
    {
        VkBuffer vkBuffer = buffer ? static_cast<const Buffer*>(buffer)->getHandle() : VK_NULL_HANDLE;
        vkCmdDrawIndexedIndirect(mCommandBuffer, vkBuffer, offset, drawCount, stride);
    }

    void CommandList::dispatchBackend(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        vkCmdDispatch(mCommandBuffer, groupCountX, groupCountY, groupCountZ);
    }

    void CommandList::dispatchIndirectBackend(const rhi::Buffer* buffer, uint64_t offset)
    {
        VkBuffer vkBuffer = buffer ? static_cast<const Buffer*>(buffer)->getHandle() : VK_NULL_HANDLE;
        vkCmdDispatchIndirect(mCommandBuffer, vkBuffer, offset);
    }

    void CommandList::insertBarriersBackend(std::span<const rhi::BufferBarrier> bufferBarriers, std::span<const rhi::TextureBarrier> textureBarriers)
    {
        if (mSynchronization2)
        {
            std::vector<VkBufferMemoryBarrier2> vkBufferBarriers;
            vkBufferBarriers.reserve(bufferBarriers.size());
            for (const auto& src : bufferBarriers)
            {
                VkBufferMemoryBarrier2 barrier = {};
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                barrier.srcStageMask = 0;
                barrier.srcAccessMask = 0;
                barrier.dstStageMask = 0;
                barrier.dstAccessMask = 0;
                getBufferPipelineStageAndAccessMask2(src.oldState, barrier.srcStageMask, barrier.srcAccessMask);
                getBufferPipelineStageAndAccessMask2(src.newState, barrier.dstStageMask, barrier.dstAccessMask);
                replaceEmptyComputeBarrierSource(src.oldState, barrier.dstStageMask, barrier.srcStageMask);
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = src.buffer ? static_cast<const Buffer*>(src.buffer)->getHandle() : VK_NULL_HANDLE;
                barrier.offset = src.offset;
                barrier.size = src.size == rhi::kWholeBuffer ? VK_WHOLE_SIZE : static_cast<VkDeviceSize>(src.size);
                vkBufferBarriers.push_back(barrier);
            }

            std::vector<VkImageMemoryBarrier2> vkImageBarriers;
            vkImageBarriers.reserve(textureBarriers.size());
            for (const auto& src : textureBarriers)
            {
                VkImageSubresourceRange range = {};
                range.aspectMask = convertToVkImageAspectFlags(src.range.aspect);
                range.baseMipLevel = src.range.baseMip;
                range.levelCount = src.range.mipCount == rhi::kWholeMipLevels
                    ? VK_REMAINING_MIP_LEVELS
                    : src.range.mipCount;
                range.baseArrayLayer = src.range.baseLayer;
                range.layerCount = src.range.layerCount == rhi::kWholeArrayLayers
                    ? VK_REMAINING_ARRAY_LAYERS
                    : src.range.layerCount;

                VkImageMemoryBarrier2 barrier = {};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                barrier.srcStageMask = 0;
                barrier.srcAccessMask = 0;
                barrier.dstStageMask = 0;
                barrier.dstAccessMask = 0;
                getTexturePipelineStageAndAccessMask2(src.oldState, barrier.srcStageMask, barrier.srcAccessMask);
                getTexturePipelineStageAndAccessMask2(src.newState, barrier.dstStageMask, barrier.dstAccessMask);
                replaceEmptyComputeBarrierSource(src.oldState, barrier.dstStageMask, barrier.srcStageMask);
                barrier.oldLayout = getImageLayout(src.oldState, src.oldState == rhi::ResourceState::Present);
                barrier.newLayout = getImageLayout(src.newState);
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = src.texture ? static_cast<const Texture*>(src.texture)->getHandle() : VK_NULL_HANDLE;
                barrier.subresourceRange = range;

                vkImageBarriers.push_back(barrier);
            }

            // 一个 VkDependencyInfo 同时携带 buffer/image memory barrier,一次 vkCmdPipelineBarrier2 搞定
            if (!vkBufferBarriers.empty() || !vkImageBarriers.empty())
            {
                VkDependencyInfoKHR dependencyInfo = {};
                dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
                dependencyInfo.dependencyFlags = 0;
                dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(vkBufferBarriers.size());
                dependencyInfo.pBufferMemoryBarriers = vkBufferBarriers.data();
                dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(vkImageBarriers.size());
                dependencyInfo.pImageMemoryBarriers = vkImageBarriers.data();

                getVkDevice().getDispatch().cmdPipelineBarrier2(mCommandBuffer, &dependencyInfo);
            }
        }
        else
        {
            std::vector<VkBufferMemoryBarrier> vkBufferBarriers;
            vkBufferBarriers.reserve(bufferBarriers.size());
            std::vector<VkImageMemoryBarrier> vkImageBarriers;
            vkImageBarriers.reserve(textureBarriers.size());
            VkPipelineStageFlags srcStage = 0;
            VkPipelineStageFlags dstStage = 0;

            for (const auto& src : bufferBarriers)
            {
                VkBufferMemoryBarrier barrier = {};
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = src.buffer ? static_cast<const Buffer*>(src.buffer)->getHandle() : VK_NULL_HANDLE;
                barrier.offset = src.offset;
                barrier.size = src.size == rhi::kWholeBuffer ? VK_WHOLE_SIZE : static_cast<VkDeviceSize>(src.size);

                VkPipelineStageFlags localSrcStage = 0;
                VkPipelineStageFlags localDstStage = 0;
                getBufferPipelineStageAndAccessMask(src.oldState, localSrcStage, barrier.srcAccessMask);
                getBufferPipelineStageAndAccessMask(src.newState, localDstStage, barrier.dstAccessMask);
                replaceEmptyComputeBarrierSource(src.oldState, localDstStage, localSrcStage);
                srcStage |= localSrcStage;
                dstStage |= localDstStage;

                vkBufferBarriers.push_back(barrier);
            }

            for (const auto& src : textureBarriers)
            {
                VkImageSubresourceRange range = {};
                range.aspectMask = convertToVkImageAspectFlags(src.range.aspect);
                range.baseMipLevel = src.range.baseMip;
                range.levelCount = src.range.mipCount == rhi::kWholeMipLevels
                    ? VK_REMAINING_MIP_LEVELS
                    : src.range.mipCount;
                range.baseArrayLayer = src.range.baseLayer;
                range.layerCount = src.range.layerCount == rhi::kWholeArrayLayers
                    ? VK_REMAINING_ARRAY_LAYERS
                    : src.range.layerCount;

                VkImageMemoryBarrier barrier = {};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = getImageLayout(src.oldState, src.oldState == rhi::ResourceState::Present);
                barrier.newLayout = getImageLayout(src.newState);
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = src.texture ? static_cast<const Texture*>(src.texture)->getHandle() : VK_NULL_HANDLE;
                barrier.subresourceRange = range;

                VkPipelineStageFlags localSrcStage = 0;
                VkPipelineStageFlags localDstStage = 0;
                getTexturePipelineStageAndAccessMask(src.oldState, localSrcStage, barrier.srcAccessMask);
                getTexturePipelineStageAndAccessMask(src.newState, localDstStage, barrier.dstAccessMask);
                replaceEmptyComputeBarrierSource(src.oldState, localDstStage, localSrcStage);
                srcStage |= localSrcStage;
                dstStage |= localDstStage;

                vkImageBarriers.push_back(barrier);
            }

            if (!vkBufferBarriers.empty() || !vkImageBarriers.empty())
            {
                vkCmdPipelineBarrier(
                    mCommandBuffer,
                    srcStage,
                    dstStage,
                    0,
                    0, nullptr,
                    static_cast<uint32_t>(vkBufferBarriers.size()), vkBufferBarriers.data(),
                    static_cast<uint32_t>(vkImageBarriers.size()), vkImageBarriers.data()
                );
            }
        }
    }

    void CommandList::copyBufferBackend(const rhi::Buffer* srcBuffer, const rhi::Buffer* dstBuffer, std::span<const rhi::BufferCopyRegion> regions)
    {
        if (regions.empty())
            return;

        VkBuffer vkSrcBuffer = srcBuffer ? static_cast<const Buffer*>(srcBuffer)->getHandle() : VK_NULL_HANDLE;
        VkBuffer vkDstBuffer = dstBuffer ? static_cast<const Buffer*>(dstBuffer)->getHandle() : VK_NULL_HANDLE;

        std::vector<VkBufferCopy> vkRegions(regions.size());
        for (size_t i = 0; i < regions.size(); ++i)
        {
            vkRegions[i].srcOffset = regions[i].srcOffset;
            vkRegions[i].dstOffset = regions[i].dstOffset;
            vkRegions[i].size = regions[i].size;
        }

        vkCmdCopyBuffer(
            mCommandBuffer,
            vkSrcBuffer,
            vkDstBuffer,
            static_cast<uint32_t>(vkRegions.size()),
            vkRegions.data()
        );
    }

    void CommandList::copyBufferToTextureBackend(const rhi::Buffer* srcBuffer, const rhi::Texture* dstTexture, std::span<const rhi::BufferTextureCopyRegion> regions)
    {
        VkBuffer vkSrcBuffer = srcBuffer ? static_cast<const Buffer*>(srcBuffer)->getHandle() : VK_NULL_HANDLE;
        VkImage vkDstImage = dstTexture ? static_cast<const Texture*>(dstTexture)->getHandle() : VK_NULL_HANDLE;

        std::vector<VkBufferImageCopy> vkRegions(regions.size());
        for (uint32_t i = 0; i < regions.size(); ++i)
        {
            const auto& r = regions[i];
            vkRegions[i].bufferOffset = r.bufferOffset;
            rhi::TextureFormatBlockInfo blockInfo = rhi::getTextureFormatBlockInfo(dstTexture->getDesc().format);
            uint32_t blocksPerRow = r.bytesPerRow / blockInfo.blockSizeInBytes;
            vkRegions[i].bufferRowLength = blocksPerRow * blockInfo.blockWidth;
            vkRegions[i].bufferImageHeight = r.rowsPerTexture * blockInfo.blockHeight;
            vkRegions[i].imageSubresource.aspectMask = convertToVkImageAspectFlags(r.slice.aspect);
            vkRegions[i].imageSubresource.mipLevel = r.slice.mipLevel;
            vkRegions[i].imageSubresource.baseArrayLayer = r.slice.baseLayer;
            vkRegions[i].imageSubresource.layerCount = r.slice.layerCount;
            vkRegions[i].imageOffset = { r.textureOffset.x, r.textureOffset.y, r.textureOffset.z };
            vkRegions[i].imageExtent = { r.textureExtent.x, r.textureExtent.y, r.textureExtent.z };
        }

        if (!vkRegions.empty())
        {
            vkCmdCopyBufferToImage(mCommandBuffer, vkSrcBuffer, vkDstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
        }
    }

    void CommandList::clearBufferBackend(const rhi::Buffer* dstBuffer, uint64_t dstOffset, uint64_t size)
    {
        VkBuffer vkBuffer = dstBuffer ? static_cast<const Buffer*>(dstBuffer)->getHandle() : VK_NULL_HANDLE;
        VkDeviceSize vkSize = size == rhi::kWholeBuffer ? VK_WHOLE_SIZE : static_cast<VkDeviceSize>(size);
        vkCmdFillBuffer(mCommandBuffer, vkBuffer, dstOffset, vkSize, 0);
    }

    void CommandList::copyTextureBackend(const rhi::Texture* srcTexture, const rhi::Texture* dstTexture, std::span<const rhi::TextureCopyRegion> regions)
    {
        VkImage vkSrcImage = srcTexture ? static_cast<const Texture*>(srcTexture)->getHandle() : VK_NULL_HANDLE;
        VkImage vkDstImage = dstTexture ? static_cast<const Texture*>(dstTexture)->getHandle() : VK_NULL_HANDLE;

        std::vector<VkImageCopy> vkRegions(regions.size());
        for (uint32_t i = 0; i < regions.size(); ++i)
        {
            const auto& r = regions[i];
            vkRegions[i].srcSubresource.aspectMask = convertToVkImageAspectFlags(r.srcSlice.aspect);
            vkRegions[i].srcSubresource.mipLevel = r.srcSlice.mipLevel;
            vkRegions[i].srcSubresource.baseArrayLayer = r.srcSlice.baseLayer;
            vkRegions[i].srcSubresource.layerCount = r.srcSlice.layerCount;
            vkRegions[i].srcOffset = { r.srcOffset.x, r.srcOffset.y, r.srcOffset.z };
            vkRegions[i].dstSubresource.aspectMask = convertToVkImageAspectFlags(r.dstSlice.aspect);
            vkRegions[i].dstSubresource.mipLevel = r.dstSlice.mipLevel;
            vkRegions[i].dstSubresource.baseArrayLayer = r.dstSlice.baseLayer;
            vkRegions[i].dstSubresource.layerCount = r.dstSlice.layerCount;
            vkRegions[i].dstOffset = { r.dstOffset.x, r.dstOffset.y, r.dstOffset.z };
            vkRegions[i].extent = { r.extent.x, r.extent.y, r.extent.z };
        }

        if (!vkRegions.empty())
        {
            vkCmdCopyImage(mCommandBuffer, vkSrcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                vkDstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
        }
    }

    void CommandList::copyTextureToBufferBackend(const rhi::Texture* srcTexture, const rhi::Buffer* dstBuffer, std::span<const rhi::BufferTextureCopyRegion> regions)
    {
        VkImage vkSrcImage = srcTexture ? static_cast<const Texture*>(srcTexture)->getHandle() : VK_NULL_HANDLE;
        VkBuffer vkDstBuffer = dstBuffer ? static_cast<const Buffer*>(dstBuffer)->getHandle() : VK_NULL_HANDLE;

        std::vector<VkBufferImageCopy> vkRegions(regions.size());
        for (uint32_t i = 0; i < regions.size(); ++i)
        {
            const auto& r = regions[i];
            vkRegions[i].bufferOffset = r.bufferOffset;
            rhi::TextureFormatBlockInfo blockInfo = rhi::getTextureFormatBlockInfo(srcTexture->getDesc().format);
            uint32_t blocksPerRow = r.bytesPerRow / blockInfo.blockSizeInBytes;
            vkRegions[i].bufferRowLength = blocksPerRow * blockInfo.blockWidth;
            vkRegions[i].bufferImageHeight = r.rowsPerTexture * blockInfo.blockHeight;
            vkRegions[i].imageSubresource.aspectMask = convertToVkImageAspectFlags(r.slice.aspect);
            vkRegions[i].imageSubresource.mipLevel = r.slice.mipLevel;
            vkRegions[i].imageSubresource.baseArrayLayer = r.slice.baseLayer;
            vkRegions[i].imageSubresource.layerCount = r.slice.layerCount;
            vkRegions[i].imageOffset = { r.textureOffset.x, r.textureOffset.y, r.textureOffset.z };
            vkRegions[i].imageExtent = { r.textureExtent.x, r.textureExtent.y, r.textureExtent.z };
        }

        if (!vkRegions.empty())
        {
            vkCmdCopyImageToBuffer(mCommandBuffer, vkSrcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vkDstBuffer,
                static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
        }
    }

    void CommandList::beginDebugGroupBackend(HString label, float4 color)
    {
        if (vkCmdBeginDebugUtilsLabelEXT)
        {
            VkDebugUtilsLabelEXT debugLabel = {};
            debugLabel.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            debugLabel.pLabelName = label.toCString();
            debugLabel.color[0] = color.r;
            debugLabel.color[1] = color.g;
            debugLabel.color[2] = color.b;
            debugLabel.color[3] = color.a;
            vkCmdBeginDebugUtilsLabelEXT(mCommandBuffer, &debugLabel);
        }
    }

    void CommandList::endDebugGroupBackend()
    {
        if (vkCmdEndDebugUtilsLabelEXT)
        {
            vkCmdEndDebugUtilsLabelEXT(mCommandBuffer);
        }
    }

    void CommandList::insertDebugMarkerBackend(HString label, float4 color)
    {
        if (vkCmdInsertDebugUtilsLabelEXT)
        {
            VkDebugUtilsLabelEXT debugLabel = {};
            debugLabel.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            debugLabel.pLabelName = label.toCString();
            debugLabel.color[0] = color.r;
            debugLabel.color[1] = color.g;
            debugLabel.color[2] = color.b;
            debugLabel.color[3] = color.a;
            vkCmdInsertDebugUtilsLabelEXT(mCommandBuffer, &debugLabel);
        }
    }

    void CommandList::invokeNativeCallbackBackend(const rhi::NativeRenderCallback& callback)
    {
        // Vulkan 这边任何时候都能立即拿到正在录制的 VkCommandBuffer, 不像 WebGPU 那样需要等到
        // decode() 重放到 Begin_Render_Pass 才有一个活的 encoder —— 见 rhi::NativeRenderCallback
        // 的注释。
        rhi::NativePassHandle handle;
        handle.type = rhi::NativePassHandleType::Vulkan_Command_Buffer;
        handle.handle = mCommandBuffer;
        callback(handle);
    }
}
