#include "ReflectionDenoiserTemporalPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>

#include <ReflectionDenoiserTemporal.generated.h>

namespace hyp
{
    namespace
    {
        constexpr uint32_t kThreadGroupSize = 8u;

        struct ReflectionDenoiserTemporalPushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            uint32_t historyValid;
        };
        static_assert(sizeof(ReflectionDenoiserTemporalPushConstants) == 20);
    }

    struct ReflectionDenoiserTemporalData
    {
        uint32_t viewId;
        uint2 dispatchGroupCount;
        ReflectionDenoiserTemporalPushConstants constants;
        rg::TextureHandle resolvedSpecularIndirect;
        rg::TextureHandle resolvedSpecularIndirectDepth;
        rg::TextureHandle rayDepth;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle gbufferD;
        rg::TextureHandle velocity;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle sceneDepthHistory;
        rg::TextureHandle specularHistoryRead;
        rg::TextureHandle numFramesHistoryRead;
        rg::TextureHandle specularHistoryWrite;
        rg::TextureHandle numFramesHistoryWrite;
    };

    struct ReflectionDenoiserTemporalStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> layout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<15>> caches;
    };

    void ReflectionDenoiserTemporalPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view,
        PostProcessState& postProcess
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 viewRectMin(static_cast<uint32_t>(viewport.x), static_cast<uint32_t>(viewport.y));
        const uint2 viewExtent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));

        // 必须在 import 之前调, 语义跟 TAAPass::addToRenderGraph 里 postProcess.ensureTAAHistory() 一样
        postProcess.ensureReflectionHistory(viewExtent);
        const bool historyValid = postProcess.isReflectionHistoryValid();

        rg::TextureHandle specularHistoryReadHandle = renderGraph.importTexture(
            "ReflectionSpecularHistoryRead"_hs(viewId), &postProcess.getReflectionSpecularHistoryReadView(), rhi::ResourceState::Shader_Sampled);
        rg::TextureHandle specularHistoryWriteHandle = renderGraph.importTexture(
            "ReflectionSpecularHistoryWrite"_hs(viewId), &postProcess.getReflectionSpecularHistoryWriteView(), rhi::ResourceState::Shader_Sampled);
        rg::TextureHandle numFramesHistoryReadHandle = renderGraph.importTexture(
            "ReflectionNumFramesHistoryRead"_hs(viewId), &postProcess.getReflectionNumFramesHistoryReadView(), rhi::ResourceState::Shader_Sampled);
        rg::TextureHandle numFramesHistoryWriteHandle = renderGraph.importTexture(
            "ReflectionNumFramesHistoryWrite"_hs(viewId), &postProcess.getReflectionNumFramesHistoryWriteView(), rhi::ResourceState::Shader_Sampled);

        // 同一个 key 走 getOrImportResource, 复用 ScreenProbeTracePass 这一帧已经导入过的句柄
        rg::TextureHandle sceneDepthHistoryHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "ScreenProbeSceneDepthHistory"_hs(viewId), [&]
        {
            return renderGraph.importTexture(
                "ScreenProbeSceneDepthHistory"_hs(viewId), &postProcess.getSceneDepthHistoryView(), rhi::ResourceState::Shader_Sampled);
        });

        renderGraph.addComputePass<ReflectionDenoiserTemporalData>(
            "ReflectionDenoiserTemporal"_hs,
            [&](rg::RenderGraph::Builder& builder, ReflectionDenoiserTemporalData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = {
                    (viewExtent.x + kThreadGroupSize - 1u) / kThreadGroupSize,
                    (viewExtent.y + kThreadGroupSize - 1u) / kThreadGroupSize };
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.historyValid = historyValid ? 1u : 0u;

                auto& blackboard = builder.getBlackboard();
                data.resolvedSpecularIndirect = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionResolvedSpecularIndirect"_hs(viewId)));
                data.resolvedSpecularIndirectDepth = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionResolvedSpecularIndirectDepth"_hs(viewId)));
                data.rayDepth = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionRayDepth"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId)));
                data.gbufferC = builder.sample(blackboard.get<rg::TextureHandle>("GBufferC"_hs(viewId)));
                data.gbufferD = builder.sample(blackboard.get<rg::TextureHandle>("GBufferD"_hs(viewId)));
                data.velocity = builder.sample(blackboard.get<rg::TextureHandle>("Velocity"_hs(viewId)));
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.sceneDepthHistory = builder.sample(sceneDepthHistoryHandle);
                data.specularHistoryRead = builder.sample(specularHistoryReadHandle);
                data.numFramesHistoryRead = builder.sample(numFramesHistoryReadHandle);
                data.specularHistoryWrite = builder.storage(specularHistoryWriteHandle, rg::AccessType::Write_Only);
                data.numFramesHistoryWrite = builder.storage(numFramesHistoryWriteHandle, rg::AccessType::Write_Only);
                // 这一帧刚写的结果就是 R5(Spatial)要读的输入——发布 write-side 句柄, 不是
                // ping-pong 里下一帧才会变成"历史"的 read-side
                blackboard.put("ReflectionSpecularAndSecondMoment"_hs(viewId), data.specularHistoryWrite);
                blackboard.put("ReflectionNumFramesAccumulated"_hs(viewId), data.numFramesHistoryWrite);
            },
            [&rendererContext](rg::ExecuteContext& context, ReflectionDenoiserTemporalData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ReflectionDenoiserTemporalPass, ReflectionDenoiserTemporalStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ReflectionDenoiserTemporal",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ReflectionDenoiserTemporal, Compute)));
                    storage.layout = createPassBindGroupLayout(renderBackend, generated::kReflectionDenoiserTemporal_Bindings);
                    storage.pipeline = createComputePipeline(rendererContext, storage.shader->getVariant(), *storage.layout);
                }

                if (storage.caches.size() <= data.viewId)
                    storage.caches.resize(data.viewId + 1u);
                auto& cache = storage.caches[data.viewId];

                const rhi::TextureView* resolvedSpecularIndirect = context.getTexture(data.resolvedSpecularIndirect);
                const rhi::TextureView* resolvedSpecularIndirectDepth = context.getTexture(data.resolvedSpecularIndirectDepth);
                const rhi::TextureView* rayDepth = context.getTexture(data.rayDepth);
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* gbufferC = context.getTexture(data.gbufferC);
                const rhi::TextureView* gbufferD = context.getTexture(data.gbufferD);
                const rhi::TextureView* velocity = context.getTexture(data.velocity);
                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                const rhi::TextureView* sceneDepthHistory = context.getTexture(data.sceneDepthHistory);
                const rhi::TextureView* specularHistoryRead = context.getTexture(data.specularHistoryRead);
                const rhi::TextureView* numFramesHistoryRead = context.getTexture(data.numFramesHistoryRead);
                rhi::TextureView* specularHistoryWrite = context.getTexture(data.specularHistoryWrite);
                rhi::TextureView* numFramesHistoryWrite = context.getTexture(data.numFramesHistoryWrite);
                rhi::Sampler* pointClampSampler = renderBackend.getSamplerCache().getPointClampNoMip();

                if (cache.needUpdate(
                        resolvedSpecularIndirect, resolvedSpecularIndirectDepth, rayDepth, gbufferA, gbufferB, gbufferC, gbufferD,
                        velocity, sceneDepth, sceneDepthHistory, specularHistoryRead, numFramesHistoryRead,
                        pointClampSampler, specularHistoryWrite, numFramesHistoryWrite))
                {
                    cache.update(renderBackend, *storage.layout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kReflectionDenoiserTemporal_Bindings);
                        writer.setTexture("ResolvedSpecularIndirect", resolvedSpecularIndirect);
                        writer.setTexture("ResolvedSpecularIndirectDepth", resolvedSpecularIndirectDepth);
                        writer.setTexture("RayDepth", rayDepth);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("GbufferCImage", gbufferC);
                        writer.setTexture("GbufferDImage", gbufferD);
                        writer.setTexture("Velocity", velocity);
                        writer.setTexture("SceneDepthImage", sceneDepth);
                        writer.setTexture("SceneDepthHistory", sceneDepthHistory);
                        writer.setTexture("SpecularAndSecondMomentHistory", specularHistoryRead);
                        writer.setTexture("NumFramesAccumulatedHistory", numFramesHistoryRead);
                        writer.setSampler("PointClampSampler", pointClampSampler);
                        writer.setTexture("NewSpecularAndSecondMoment", specularHistoryWrite);
                        writer.setTexture("NewNumFramesAccumulated", numFramesHistoryWrite);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            });

        postProcess.swapReflectionHistory();
    }
}
