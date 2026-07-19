#include "RenderPipeline.h"
#include "DirectionalLightProxy.h"

#include "RenderPass/CascadeShadowDepthPass.h"
#include "RenderPass/GBufferPass.h"
#include "RenderPass/DirectLightingPass.h"
#include "RenderPass/AtmospherePass.h"
#include "RenderPass/SkyLightPass.h"
#include "RenderPass/IndirectLightingPass.h"
#include "RenderPass/GSplatPass.h"
#include "RenderPass/TranslucentPass.h"
#include "RenderPass/PresentPass.h"

namespace hyp
{
    struct PrepareData
    {
        const rhi::PipelineLayout* globalPipelineLayout;
        const rhi::BindGroup* sceneAndViewBindGroup;
        uint32_t viewDataDynamicOffset;
    };

    void DeferredPipeline::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const RenderData& renderData
    )
    {
        // setBindGroup 不需要处于任何 pass 的 begin/end 范围内: Vulkan 的 vkCmdBindDescriptorSets
        // 本就无条件立即执行, 不看当前是否在 render/compute pass 里; WebGPU 后端的
        // setBindGroupBackend 专门实现了延迟绑定 —— pass 外调用会缓存进
        // mPendingGraphicsBindGroups/mPendingComputeBindGroups, 等下一次 beginRenderPass/
        // beginComputePass 时自动重放到新开的 pass encoder 上(见 WebGPU/CommandList.cpp)。
        // 换成 TransferPass 后, 这个 pass 不再需要包一层从未真正被用到的空 compute pass。
        renderGraph.addTransferPass<PrepareData>(
            "Prepare"_hs,
            [&](rg::RenderGraph::Builder& builder, PrepareData& data)
            {
                builder.markSideEffect();
                data.globalPipelineLayout = &rendererContext.getGlobalPipelineLayout();
                data.sceneAndViewBindGroup = &scene.getSceneAndViewBindGroup();

                auto& viewDataManager = rendererContext.getViewDataManager();
                data.viewDataDynamicOffset = viewDataManager.getViewDataDynamicOffset(view.getViewId());
            },
            [](rg::ExecuteContext& context, PrepareData& data)
            {
                rhi::CommandList& cmdList = context.getCommandList();

                rhi::PipelineBindPoint bindPoints[] = {
                    rhi::PipelineBindPoint::Graphics,
                    rhi::PipelineBindPoint::Compute
                };
                for (auto bindPoint : bindPoints)
                {
                    cmdList.setBindGroup(
                        1,
                        bindPoint,
                        data.globalPipelineLayout,
                        data.sceneAndViewBindGroup,
                        { &data.viewDataDynamicOffset, 1 }
                    );
                }
            }
        );

        GBufferPass::addToRenderGraph(renderGraph, rendererContext, scene, view, renderData);

        // DirectLightingPass 现在要按太阳光穿过大气的透过率衰减直接光照, 需要读 AtmospherePass
        // 产出的 TransmittanceLut(通过 blackboard), 必须排在 AtmospherePass 后面
        AtmosphereProxy* activeAtmosphere = scene.getActiveAtmosphereProxy();
        if (activeAtmosphere)
        {
            AtmospherePass::addToRenderGraph(renderGraph, rendererContext, scene, view, *activeAtmosphere);
        }

        DirectLightingPass::addToRenderGraph(renderGraph, rendererContext, scene, view, activeAtmosphere);

        SkyLightProxy* activeSkyLight = scene.getActiveSkyLightProxy();
        if (activeSkyLight)
        {
            SkyLightPass::addToRenderGraph(
                renderGraph,
                rendererContext,
                scene,
                *activeSkyLight,
                activeAtmosphere
            );
            IndirectLightingPass::addToRenderGraph(renderGraph, rendererContext, scene, view, *activeSkyLight);
        }

        GSplatScene* gsplatScene = scene.getGSplatScene();
        if (gsplatScene && gsplatScene->getTotalSplatCount())
        {
            GSplatPass::addToRenderGraph(renderGraph, rendererContext, scene, view, *gsplatScene);
        }

        // 半透明网格排在最后: 需要读取前面所有 pass(不透明 + 直接光照 + 天光 IBL + GSplat)
        // 合成完的 SceneColor 作为混合背景, 且不参与 GSplat 的可见性/排序
        if (!renderData.translucentBucket->empty())
        {
            TranslucentPass::addToRenderGraph(renderGraph, rendererContext, scene, view, renderData, activeSkyLight);
        }

        PresentPass::addToRenderGraph(renderGraph, rendererContext, view);
    }

    struct ShadowDepthData
    {
        RendererContext* rendererContext;
        const rhi::PipelineLayout* globalPipelineLayout;
        const rhi::BindGroup* sceneAndViewBindGroup;
        std::vector<uint32_t> viewDataDynamicOffsets;
        std::vector<float4> viewports;
        std::vector<std::shared_ptr<DrawItemBucket>> shadowDepthBuckets;
        rg::TextureHandle shadowDepth;
    };

    void CascadeShadowPipeline::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
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
