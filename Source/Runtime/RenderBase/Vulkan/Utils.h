#pragma once
#include "Buffer.h"
#include "Image.h"
#include "Pipeline.h"
#include <filesystem>

namespace hyp::vk
{
    class Utils
    {
    public:
        static void uploadDataToBuffer(
            const Device& device,
            Buffer& dstBuffer,
            const void* data,
            VkDeviceSize size,
            VkDeviceSize offset = 0
        );

        static void uploadDataToImage(
            const Device& device,
            Image& dstImage,
            uint32_t level,
            uint32_t layer,
            const void* data,
            VkDeviceSize size
        );

        static void uploadDataToImageAndGenMips(
            const Device& device,
            Image& dstImage,
            const void* data,
            VkDeviceSize size
        );

        static void transitionImageLayout(
            const Device& device,
            Image& image,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            VkAccessFlags srcAccessMask,
            VkAccessFlags dstAccessMask,
            const VkImageSubresourceRange& subresourceRange,
            VkPipelineStageFlags srcStageMask,
            VkPipelineStageFlags dstStageMask
        );

        static TRef<ShaderModule> createShaderModuleFromFile(
            const Device& device,
            const std::filesystem::path& path
        );
    };
}
