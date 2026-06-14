#include "RenderPipeline.h"
#include "DirectionalLightProxy.h"

#include "RenderPass/CascadeShadowDepthPass.h"
#include "RenderPass/GBufferPass.h"
#include "RenderPass/DirectLightingPass.h"
#include "RenderPass/AtmospherePass.h"
#include "RenderPass/SkyLightPass.h"
#include "RenderPass/GSplatPass.h"

namespace hyp
{
    struct PrepareData
    {
        VkPipelineLayout globalPipelineLayout;
        VkDescriptorSet sceneAndViewDescriptorSet;
        uint32_t viewDataDynamicOffset;
    };

    struct PresentData
    {
        rg::TextureHandle sceneColor;
    };

    void DeferredPipeline::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const RenderData& renderData
    )
    {
        renderGraph.addComputePass<PrepareData>(
            "Prepare"_hs,
            [&](rg::RenderGraph::Builder& builder, PrepareData& data)
            {
                builder.markSideEffect();
                data.globalPipelineLayout = rendererContext.getGlobalPipelineLayout().getHandle();
                data.sceneAndViewDescriptorSet = scene.getSceneAndViewDescriptorSet().getHandle();

                auto& viewDataManager = rendererContext.getViewDataManager();
                data.viewDataDynamicOffset = viewDataManager.getViewDataDynamicOffset(view.getViewId());
            },
            [](rg::ExecuteContext& context, PrepareData& data)
            {
                auto& cmd = context.getCommandBuffer();

                VkPipelineBindPoint bindPoints[] = {
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
                };
                VkDescriptorSet descriptorSets[] = { data.sceneAndViewDescriptorSet };
                uint32_t dynamicOffsets[] = { data.viewDataDynamicOffset };
                for (auto bindPoint : bindPoints)
                {
                    cmd.bindDescriptorSets(
                        bindPoint,
                        data.globalPipelineLayout,
                        1,
                        descriptorSets,
                        dynamicOffsets
                    );
                }
            }
        );

        GBufferPass::addToRenderGraph(renderGraph, rendererContext, scene, view, renderData);

        DirectLightingPass::addToRenderGraph(renderGraph, rendererContext, scene, view);

        AtmosphereProxy* activeAtmosphere = scene.getActiveAtmosphereProxy();
        if (activeAtmosphere)
        {
            AtmospherePass::addToRenderGraph(renderGraph, rendererContext, scene, view, *activeAtmosphere);
            SkyLightPass::addToRenderGraph(renderGraph, rendererContext, scene, view, *activeAtmosphere);
        }

        GSplatScene* gsplatScene = scene.getGSplatScene();
        if (gsplatScene && gsplatScene->getTotalSplatCount())
        {
            GSplatPass::addToRenderGraph(renderGraph, rendererContext, view, *gsplatScene);
        }

        renderGraph.addPresentPass<PresentData>(
            "Present"_hs,
            [&](rg::RenderGraph::Builder& builder, PresentData& data)
            {
                uint32_t viewId = view.getViewId();
                rg::Blackboard& blackboard = builder.getBlackboard();
                rg::TextureHandle sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(viewId));
                data.sceneColor = builder.transfer(sceneColor, rg::AccessType::Read_Only);
            },
            [](rg::ExecuteContext& context, PresentData& data)
            {
                auto& swapchain = context.getSwapchain();
                uint32_t imageIndex = swapchain.getCurrentImageIndex();
                vk::Image* swapchainImage = swapchain.getImage(imageIndex);
                vk::ImageView* sceneColor = context.getTexture(data.sceneColor);

                auto& cmd = context.getCommandBuffer();

                // pre barrier
                {
                    VkImageMemoryBarrier2 barrier = {};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                    barrier.srcAccessMask = VK_ACCESS_2_NONE;
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
                    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = swapchainImage->getHandle();
                    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

                    cmd.pipelineBarrier2({&barrier , 1});
                }

                VkImageBlit blit = {};
                blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                blit.srcOffsets[0] = { 0, 0, 0 };
                VkExtent3D sceneColorExtent = sceneColor->getImage().getExtent();
                blit.srcOffsets[1] = {
                    int32_t(sceneColorExtent.width),
                    int32_t(sceneColorExtent.height),
                    1
                };
                blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                blit.dstOffsets[0] = { 0, 0, 0 };
                VkExtent3D swapchainExtent = swapchainImage->getExtent();
                blit.dstOffsets[1] = {
                    int32_t(swapchainExtent.width),
                    int32_t(swapchainExtent.height),
                    1
                };
                cmd.blitImage(
                    sceneColor->getImage().getHandle(),
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    swapchainImage->getHandle(),
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    {&blit, 1},
                    VK_FILTER_NEAREST
                );

                {
                    VkImageMemoryBarrier2 barrier = {};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
                    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
                    barrier.dstAccessMask = VK_ACCESS_2_NONE;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = swapchainImage->getHandle();
                    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    cmd.pipelineBarrier2({ &barrier, 1 });
                }
            },
            rg::PassFlags::Skip_Begin_Render_Pass | rg::PassFlags::Skip_End_Render_Pass
        );
    }

    struct ShadowDepthData
    {
        SceneRendererContext* rendererContext;
        VkPipelineLayout globalPipelineLayout;
        VkDescriptorSet sceneAndViewDescriptorSet;
        std::vector<uint32_t> viewDataDynamicOffsets;
        std::vector<float4> viewports;
        std::vector<std::shared_ptr<DrawItemBucket>> shadowDepthBuckets;
        rg::TextureHandle shadowDepth;
    };

    void CascadeShadowPipeline::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneProxy& scene,
        std::span<const SceneView> views,
        std::span<const RenderData> renderData,
        const DirectionalLightProxy& directionalLight
    )
    {
        size_t totalBucketCount = 0;
        for (auto& rd : renderData)
        {
            totalBucketCount += rd.shadowDepthBucket->size();
        }
        if (!totalBucketCount)
            return;
        CascadeShadowDepthPass::addToRenderGraph(renderGraph, rendererContext, scene, views, renderData, directionalLight);
    }

}
