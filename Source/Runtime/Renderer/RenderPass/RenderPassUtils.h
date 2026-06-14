#pragma once
#include <algorithm>
#include <array>
#include <span>
#include <type_traits>
#include <cstring>

#include "RenderBase/Vulkan/DescriptorSet.h"
#include "Renderer/SceneRendererContext.h"

namespace hyp
{
    template<size_t HandleCount>
    class DescriptorCache
    {
    public:
        template <typename... Handles>
        bool needUpdate(Handles... currentHandles)
        {
            static_assert(sizeof...(Handles) == HandleCount,
                "Number of handles must match template parameter");
            std::array<uint64_t, HandleCount> currentHandleValues{ handleToUint64(currentHandles)... };
            bool needUpdate = !mDescriptorSet || mLastHandles != currentHandleValues;
            if (needUpdate)
            {
                mPendingHandles = currentHandleValues;
            }
            return needUpdate;
        }

        template <typename WriteFn>
        void update(
            RenderBackend& renderBackend,
            std::span<const VkDescriptorSetLayoutBinding> bindings,
            WriteFn&& writeCallback
        )
        {
            std::vector<VkDescriptorBindingFlags> flags(bindings.size(), 0);
            bool includeBindless = false;
            for (uint32_t i = 0; i < bindings.size(); ++i)
            {
                if (bindings[i].descriptorCount > 1)
                {
                    flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
                    includeBindless = true;
                }
            }
            if (!includeBindless)
                flags.clear();
            
            auto newDescriptorSet = renderBackend.getDevice().getDescriptorAllocator().allocate(bindings, flags);

            vk::DescriptorWriter writer;
            std::forward<WriteFn>(writeCallback)(writer);
            writer.update(*newDescriptorSet);

            if (mDescriptorSet)
            {
                renderBackend.getDeferredTaskQueue().push(
                    [oldSet = std::move(mDescriptorSet)]() mutable { oldSet = nullptr; },
                    kMaxFramesInFlight
                );
            }

            mDescriptorSet = newDescriptorSet;
            mLastHandles = mPendingHandles;
        }

        vk::DescriptorSet* getDescriptorSet() const noexcept { return mDescriptorSet.get(); }

    private:
        static uint64_t handleToUint64(VkBuffer buffer) noexcept { return reinterpret_cast<uint64_t>(buffer); }
        static uint64_t handleToUint64(VkImageView imageView) noexcept { return reinterpret_cast<uint64_t>(imageView); }

        TRef<vk::DescriptorSet> mDescriptorSet;
        std::array<uint64_t, HandleCount> mLastHandles{};
        std::array<uint64_t, HandleCount> mPendingHandles{};
    };

    inline vk::PipelineLayout* createRenderPassPipelineLayout(
        SceneRendererContext& rendererContext,
        VkDescriptorSetLayout passLayout = VK_NULL_HANDLE,
        VkDescriptorSetLayout materialLayout = VK_NULL_HANDLE)
    {
        assert(passLayout != VK_NULL_HANDLE || materialLayout != VK_NULL_HANDLE);

        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
        PipelineCache::PipelineLayoutKey key;
        key.descriptorSetLayouts[0] = rendererContext.getGlobalDataDescriptorSetLayout().getHandle();
        key.descriptorSetLayouts[1] = rendererContext.getSceneAndViewDescriptorSetLayout().getHandle();
        key.descriptorSetLayouts[2] = materialLayout != VK_NULL_HANDLE ? materialLayout : passLayout;
        key.descriptorSetLayouts[3] = VK_NULL_HANDLE;
        key.pushConstantRangeSize = 128;
        return pipelineCache.getOrCreatePipelineLayout(key);
    }

    inline vk::Pipeline* createComputePipeline(
        SceneRendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const vk::DescriptorSetLayout& descriptorSetLayout)
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, descriptorSetLayout.getHandle());
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
        PipelineCache::ComputeKey key;
        key.shader = shaderVariant;
        key.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateComputePipeline(key);
    }

    inline void setViewportAndScissor(vk::CommandBuffer& cmd, float4 inViewport)
    {
        VkViewport viewport = {
            inViewport.x,
            inViewport.y + inViewport.w,
            inViewport.z,
            -inViewport.w,
            0.0f,
            1.0f
        };
        VkRect2D scissor = {
            static_cast<int32_t>(inViewport.x),
            static_cast<int32_t>(inViewport.y),
            static_cast<uint32_t>(inViewport.z),
            static_cast<uint32_t>(inViewport.w)
        };
        cmd.setViewport(0, { &viewport, 1 });
        cmd.setScissor(0, { &scissor, 1 });
    }
}
