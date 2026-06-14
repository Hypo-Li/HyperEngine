#include "GlobalBindings.h"
#include "RenderBackend.h"

namespace hyp
{
    GlobalBindings::GlobalBindings(const vk::Device& device, BindlessRegistry& bindlessRegistry) :
        mDevice(device)
    {
        bindlessRegistry.setGlobalBindings(this);
        initBindlessDefaultResources();
        initGlobalDataDescriptorSet(bindlessRegistry);
    }

    void GlobalBindings::writeTexture2D(uint32_t index, VkImageView imageView)
    {
        vk::DescriptorWriter writer;
        imageView = imageView == VK_NULL_HANDLE ? mDefaultTexture2DImageView->getHandle() : imageView;
        writer.writeImage(uint32_t(BindlessBinding::Texture2D), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, index);
        writer.update(*mGlobalDataDescriptorSet);
    }

    void GlobalBindings::writeTexture2DArray(uint32_t index, VkImageView imageView)
    {
        vk::DescriptorWriter writer;
        imageView = imageView == VK_NULL_HANDLE ? mDefaultTexture2DArrayImageView->getHandle() : imageView;
        writer.writeImage(uint32_t(BindlessBinding::Texture2DArray), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, index);
        writer.update(*mGlobalDataDescriptorSet);
    }

    void GlobalBindings::writeTextureCube(uint32_t index, VkImageView imageView)
    {
        vk::DescriptorWriter writer;
        imageView = imageView == VK_NULL_HANDLE ? mDefaultTextureCubeImageView->getHandle() : imageView;
        writer.writeImage(uint32_t(BindlessBinding::TextureCube), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, index);
        writer.update(*mGlobalDataDescriptorSet);
    }

#if SUPPORT_TEXTURE_CUBE_ARRAY
    void GlobalBindings::writeTextureCubeArray(uint32_t index, VkImageView imageView)
    {
        vk::DescriptorWriter writer;
        imageView = imageView == VK_NULL_HANDLE ? mDefaultTextureCubeArrayImageView->getHandle() : imageView;
        writer.writeImage(uint32_t(BindlessBinding::TextureCubeArray), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, index);
        writer.update(*mGlobalDataDescriptorSet);
    }
#endif

    void GlobalBindings::writeTexture3D(uint32_t index, VkImageView imageView)
    {
        vk::DescriptorWriter writer;
        imageView = imageView == VK_NULL_HANDLE ? mDefaultTexture3DImageView->getHandle() : imageView;
        writer.writeImage(uint32_t(BindlessBinding::Texture3D), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, index);
        writer.update(*mGlobalDataDescriptorSet);
    }

    void GlobalBindings::writeSampler(uint32_t index, VkSampler sampler)
    {
        vk::DescriptorWriter writer;
        sampler = sampler == VK_NULL_HANDLE ? mDefaultSampler->getHandle() : sampler;
        writer.writeImage(uint32_t(BindlessBinding::Sampler), VK_DESCRIPTOR_TYPE_SAMPLER, sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, index);
        writer.update(*mGlobalDataDescriptorSet);
    }

    void GlobalBindings::updateFrameData(vk::CommandBuffer& cmd, const FrameData& frameData)
    {
        cmd.updateBuffer(mFrameDataUniformBuffer->getHandle(), 0, sizeof(FrameData), &frameData);

        VkBufferMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_2_UNIFORM_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = mFrameDataUniformBuffer->getHandle();
        barrier.offset = 0;
        barrier.size = sizeof(FrameData);
        cmd.pipelineBarrier2({ &barrier, 1 });
    }

    void GlobalBindings::initBindlessDefaultResources()
    {
        vk::Image::CreateInfo imageInfo;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = { 1, 1, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 6;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.isCube = true;

        mDefaultTextureImage2D = new vk::Image(mDevice, imageInfo, VMA_MEMORY_USAGE_GPU_ONLY);

        {
            vk::Image::ViewInfo imageViewInfo;
            imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewInfo.subresourceRange.baseMipLevel = 0;
            imageViewInfo.subresourceRange.baseArrayLayer = 0;
            imageViewInfo.subresourceRange.levelCount = 1;
            imageViewInfo.subresourceRange.layerCount = 1;
            mDefaultTexture2DImageView = mDefaultTextureImage2D->createView(imageViewInfo);
        }

        {
            vk::Image::ViewInfo imageViewInfo;
            imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            imageViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewInfo.subresourceRange.baseMipLevel = 0;
            imageViewInfo.subresourceRange.baseArrayLayer = 0;
            imageViewInfo.subresourceRange.levelCount = 1;
            imageViewInfo.subresourceRange.layerCount = 6;
            mDefaultTexture2DArrayImageView = mDefaultTextureImage2D->createView(imageViewInfo);
        }

        {
            vk::Image::ViewInfo imageViewInfo;
            imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            imageViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewInfo.subresourceRange.baseMipLevel = 0;
            imageViewInfo.subresourceRange.baseArrayLayer = 0;
            imageViewInfo.subresourceRange.levelCount = 1;
            imageViewInfo.subresourceRange.layerCount = 6;
            mDefaultTextureCubeImageView = mDefaultTextureImage2D->createView(imageViewInfo);
        }

#if SUPPORT_TEXTURE_CUBE_ARRAY
        {
            vk::Image::ViewInfo imageViewInfo;
            imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            imageViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewInfo.subresourceRange.baseMipLevel = 0;
            imageViewInfo.subresourceRange.baseArrayLayer = 0;
            imageViewInfo.subresourceRange.levelCount = 1;
            imageViewInfo.subresourceRange.layerCount = 6;
            mDefaultTextureCubeArrayImageView = mDefaultTextureImage2D->createView(imageViewInfo);
        }
#endif

        imageInfo.imageType = VK_IMAGE_TYPE_3D;
        imageInfo.arrayLayers = 1;
        imageInfo.isCube = false;
        mDefaultTextureImage3D = new vk::Image(mDevice, imageInfo, VMA_MEMORY_USAGE_GPU_ONLY);

        {
            vk::Image::ViewInfo imageViewInfo;
            imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
            imageViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewInfo.subresourceRange.baseMipLevel = 0;
            imageViewInfo.subresourceRange.baseArrayLayer = 0;
            imageViewInfo.subresourceRange.levelCount = 1;
            imageViewInfo.subresourceRange.layerCount = 1;
            mDefaultTexture3DImageView = mDefaultTextureImage3D->createView(imageViewInfo);
        }

        vk::Sampler::CreateInfo samplerInfo;
        mDefaultSampler = new vk::Sampler(mDevice, samplerInfo);
    }

    void GlobalBindings::initGlobalDataDescriptorSet(const BindlessRegistry& bindlessRegistry)
    {
        auto& bindlessConfig = bindlessRegistry.getBindlessConfig();
        VkDescriptorSetLayoutBinding bindings[] =
        {
            { uint32_t(BindlessBinding::Texture2D), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, bindlessConfig.maxTexture2DCount, VK_SHADER_STAGE_ALL, nullptr },
            { uint32_t(BindlessBinding::Texture2DArray), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, bindlessConfig.maxTexture2DArrayCount, VK_SHADER_STAGE_ALL, nullptr },
            { uint32_t(BindlessBinding::TextureCube), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, bindlessConfig.maxTextureCubeCount, VK_SHADER_STAGE_ALL, nullptr },
#if SUPPORT_TEXTURE_CUBE_ARRAY
            { uint32_t(BindlessBinding::TextureCubeArray), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, bindlessConfig.maxTextureCubeArrayCount, VK_SHADER_STAGE_ALL, nullptr},
#endif
            { uint32_t(BindlessBinding::Texture3D), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, bindlessConfig.maxTexture3DCount, VK_SHADER_STAGE_ALL, nullptr},
            { uint32_t(BindlessBinding::Sampler), VK_DESCRIPTOR_TYPE_SAMPLER, bindlessConfig.maxSamplerCount, VK_SHADER_STAGE_ALL, nullptr},
            { 6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr }
        };
        VkDescriptorBindingFlags flags[] =
        {
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
#if SUPPORT_TEXTURE_CUBE_ARRAY
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
#endif
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
            0
        };
        mGlobalDataDescriptorSet = mDevice.getDescriptorAllocator().allocate(bindings, flags);

        std::vector<VkSampler> samplers;
        std::vector<VkImageView> imageViews;
        std::vector<VkImageLayout> imageLayouts;

        vk::DescriptorWriter writer;

        samplers.resize(bindlessConfig.maxTexture2DCount);
        imageViews.resize(bindlessConfig.maxTexture2DCount);
        imageLayouts.resize(bindlessConfig.maxTexture2DCount);
        for (uint32_t i = 0; i < bindlessConfig.maxTexture2DCount; ++i)
        {
            samplers[i] = VK_NULL_HANDLE;
            imageViews[i] = mDefaultTexture2DImageView->getHandle();
            imageLayouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        writer.writeImages(uint32_t(BindlessBinding::Texture2D), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, samplers, imageViews, imageLayouts);

        samplers.resize(bindlessConfig.maxTexture2DArrayCount);
        imageViews.resize(bindlessConfig.maxTexture2DArrayCount);
        imageLayouts.resize(bindlessConfig.maxTexture2DArrayCount);
        for (uint32_t i = 0; i < bindlessConfig.maxTexture2DArrayCount; ++i)
        {
            samplers[i] = VK_NULL_HANDLE;
            imageViews[i] = mDefaultTexture2DArrayImageView->getHandle();
            imageLayouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        writer.writeImages(uint32_t(BindlessBinding::Texture2DArray), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, samplers, imageViews, imageLayouts);

        samplers.resize(bindlessConfig.maxTextureCubeCount);
        imageViews.resize(bindlessConfig.maxTextureCubeCount);
        imageLayouts.resize(bindlessConfig.maxTextureCubeCount);
        for (uint32_t i = 0; i < bindlessConfig.maxTextureCubeCount; ++i)
        {
            samplers[i] = VK_NULL_HANDLE;
            imageViews[i] = mDefaultTextureCubeImageView->getHandle();
            imageLayouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        writer.writeImages(uint32_t(BindlessBinding::TextureCube), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, samplers, imageViews, imageLayouts);

#if SUPPORT_TEXTURE_CUBE_ARRAY
        samplers.resize(bindlessConfig.maxTextureCubeArrayCount);
        imageViews.resize(bindlessConfig.maxTextureCubeArrayCount);
        imageLayouts.resize(bindlessConfig.maxTextureCubeArrayCount);
        for (uint32_t i = 0; i < bindlessConfig.maxTextureCubeArrayCount; ++i)
        {
            samplers[i] = VK_NULL_HANDLE;
            imageViews[i] = mDefaultTextureCubeArrayImageView->getHandle();
            imageLayouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        writer.writeImages(uint32_t(BindlessBinding::TextureCubeArray), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, samplers, imageViews, imageLayouts);
#endif

        samplers.resize(bindlessConfig.maxTexture3DCount);
        imageViews.resize(bindlessConfig.maxTexture3DCount);
        imageLayouts.resize(bindlessConfig.maxTexture3DCount);
        for (uint32_t i = 0; i < bindlessConfig.maxTexture3DCount; ++i)
        {
            samplers[i] = VK_NULL_HANDLE;
            imageViews[i] = mDefaultTexture3DImageView->getHandle();
            imageLayouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        writer.writeImages(uint32_t(BindlessBinding::Texture3D), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, samplers, imageViews, imageLayouts);

        samplers.resize(bindlessConfig.maxSamplerCount);
        imageViews.resize(bindlessConfig.maxSamplerCount);
        imageLayouts.resize(bindlessConfig.maxSamplerCount);
        for (uint32_t i = 0; i < bindlessConfig.maxSamplerCount; ++i)
        {
            samplers[i] = mDefaultSampler->getHandle();
            imageViews[i] = VK_NULL_HANDLE;
            imageLayouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        writer.writeImages(uint32_t(BindlessBinding::Sampler), VK_DESCRIPTOR_TYPE_SAMPLER, samplers, imageViews, imageLayouts);

        vk::Buffer::CreateInfo bufferInfo;
        bufferInfo.size = sizeof(FrameData);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        mFrameDataUniformBuffer = new vk::Buffer(mDevice, bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY);

        writer.writeBuffer(6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, mFrameDataUniformBuffer->getHandle(), 0, sizeof(FrameData));

        writer.update(*mGlobalDataDescriptorSet);

        VkDescriptorSetLayout setLayouts[] = {
            mGlobalDataDescriptorSet->getLayout().getHandle()
        };
        VkPushConstantRange pushConstantRange = {
            VK_SHADER_STAGE_ALL, 0, 128
        };
        mPipelineLayout = new vk::PipelineLayout(mDevice, setLayouts, { &pushConstantRange, 1 });
    }

}
