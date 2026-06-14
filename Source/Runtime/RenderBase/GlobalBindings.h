#pragma once
#include "RenderBaseDefines.h"
#include "StagingAllocator.h"
#include "BindlessRegistry.h"
#include "Vulkan/Buffer.h"
#include "Vulkan/DescriptorSet.h"
#include "Vulkan/Pipeline.h"
#include "Vulkan/CommandBuffer.h"

#define SUPPORT_TEXTURE_CUBE_ARRAY 0

namespace hyp
{
    struct FrameData
    {
        float wrappedTime;
        uint32_t timeWrapCount;
        float deltaTime;
        uint32_t frameIndex;
    };

    class GlobalBindings
    {
    public:
        GlobalBindings(const vk::Device& device, BindlessRegistry& bindlessRegistry);

        const vk::DescriptorSet& getGlobalDataDescriptorSet() const  noexcept
        {
            return *mGlobalDataDescriptorSet;
        }

        const vk::PipelineLayout& getPipelineLayout() const noexcept
        {
            return *mPipelineLayout;
        }

        void writeTexture2D(uint32_t index, VkImageView imageView);

        void writeTexture2DArray(uint32_t index, VkImageView imageView);

        void writeTextureCube(uint32_t index, VkImageView imageView);

#if SUPPORT_TEXTURE_CUBE_ARRAY
        void writeTextureCubeArray(uint32_t index, VkImageView imageView);
#endif

        void writeTexture3D(uint32_t index, VkImageView imageView);

        void writeSampler(uint32_t index, VkSampler sampler);

        void updateFrameData(vk::CommandBuffer& cmd, const FrameData& frameData);

    private:
        void initBindlessDefaultResources();

        void initGlobalDataDescriptorSet(const BindlessRegistry& bindlessRegistry);

    private:
        const vk::Device& mDevice;

        TRef<vk::Image> mDefaultTextureImage2D;
        TRef<vk::ImageView> mDefaultTexture2DImageView;
        TRef<vk::ImageView> mDefaultTexture2DArrayImageView;
        TRef<vk::ImageView> mDefaultTextureCubeImageView;
#if SUPPORT_TEXTURE_CUBE_ARRAY
        TRef<vk::ImageView> mDefaultTextureCubeArrayImageView;
#endif
        TRef<vk::Image> mDefaultTextureImage3D;
        TRef<vk::ImageView> mDefaultTexture3DImageView;
        TRef<vk::Sampler> mDefaultSampler;

        TRef<vk::Buffer> mFrameDataUniformBuffer;

        TRef<vk::DescriptorSet> mGlobalDataDescriptorSet;
        TRef<vk::PipelineLayout> mPipelineLayout;
    };
}
