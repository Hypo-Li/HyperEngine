#include "ScreenProbeTemporalFilterPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>

#include <ScreenProbeTemporalFilter.generated.h>

namespace hyp
{
    namespace
    {
        // UE LumenScreenProbeGather.cpp 的出厂默认值
        constexpr float kMaxFramesAccumulated = 10.0f;
        constexpr uint32_t kShortRangeAODownsampleFactor = 2u;

        // 跟 Shader/ScreenProbe/ScreenProbeTemporalFilter.slang 里的 ScreenProbeTemporalFilterPushConstants 逐字段对应
        struct ScreenProbeTemporalFilterPushConstants
        {
            uint2 viewRectMin;
            uint32_t historyValid;
            float maxFramesAccumulated;
            uint2 downsampledViewExtent;
        };
        static_assert(sizeof(ScreenProbeTemporalFilterPushConstants) == 24);

        constexpr uint32_t kThreadGroupSize = 8u;
    }

    struct ScreenProbeTemporalFilterData
    {
        uint32_t viewId;
        ScreenProbeTemporalFilterPushConstants constants;
        uint2 dispatchGroupCount;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle velocity;
        rg::TextureHandle sceneDepthHistory;
        rg::TextureHandle diffuseIndirectHistoryRead;
        rg::TextureHandle diffuseIndirectHistoryWrite;
        rg::TextureHandle diffuseIndirect;
        rg::TextureHandle diffuseIndirectFiltered;
        rg::TextureHandle lightingValid;
        rg::TextureHandle shortRangeAOStochasticOffset;
        rg::TextureHandle shortRangeAO;
        rg::TextureHandle shortRangeAOHistoryRead;
        rg::TextureHandle shortRangeAOHistoryWrite;
        rg::TextureHandle roughSpecularIndirect;
        rg::TextureHandle roughSpecularIndirectHistoryRead;
        rg::TextureHandle roughSpecularIndirectHistoryWrite;
        rg::TextureHandle roughSpecularIndirectFiltered;
    };

    struct ScreenProbeTemporalFilterStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> layout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<19>> caches;
    };

    void ScreenProbeTemporalFilterPass::addToRenderGraph(
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

        // 按当前 viewport 尺寸懒创建/重建两张常驻 ping-pong history 纹理——必须在 import 之前调,
        // 语义跟 TAAPass::addToRenderGraph 里 postProcess.ensureTAAHistory() 的注意事项完全一样
        postProcess.ensureDiffuseIndirectHistory(viewExtent);
        postProcess.ensureShortRangeAOHistory(viewExtent);
        postProcess.ensureRoughSpecularIndirectHistory(viewExtent);
        const bool historyValid = postProcess.isDiffuseIndirectHistoryValid();
        const uint2 downsampledViewExtent(
            (viewExtent.x + kShortRangeAODownsampleFactor - 1u) / kShortRangeAODownsampleFactor,
            (viewExtent.y + kShortRangeAODownsampleFactor - 1u) / kShortRangeAODownsampleFactor);

        rg::TextureHandle historyReadHandle = renderGraph.importTexture(
            "ScreenProbeDiffuseIndirectHistoryRead"_hs(viewId),
            &postProcess.getDiffuseIndirectHistoryReadView(),
            rhi::ResourceState::Shader_Sampled
        );
        rg::TextureHandle historyWriteHandle = renderGraph.importTexture(
            "ScreenProbeDiffuseIndirectHistoryWrite"_hs(viewId),
            &postProcess.getDiffuseIndirectHistoryWriteView(),
            rhi::ResourceState::Shader_Sampled
        );

        rg::TextureHandle shortRangeAOHistoryReadHandle = renderGraph.importTexture(
            "ScreenProbeShortRangeAOHistoryRead"_hs(viewId),
            &postProcess.getShortRangeAOHistoryReadView(),
            rhi::ResourceState::Shader_Sampled
        );
        rg::TextureHandle shortRangeAOHistoryWriteHandle = renderGraph.importTexture(
            "ScreenProbeShortRangeAOHistoryWrite"_hs(viewId),
            &postProcess.getShortRangeAOHistoryWriteView(),
            rhi::ResourceState::Shader_Sampled
        );

        rg::TextureHandle roughSpecularHistoryReadHandle = renderGraph.importTexture(
            "ScreenProbeRoughSpecularIndirectHistoryRead"_hs(viewId),
            &postProcess.getRoughSpecularIndirectHistoryReadView(),
            rhi::ResourceState::Shader_Sampled
        );
        rg::TextureHandle roughSpecularHistoryWriteHandle = renderGraph.importTexture(
            "ScreenProbeRoughSpecularIndirectHistoryWrite"_hs(viewId),
            &postProcess.getRoughSpecularIndirectHistoryWriteView(),
            rhi::ResourceState::Shader_Sampled
        );

        // 同一个 key 走 getOrImportResource, 复用 ScreenProbeTracePass 已经导入过的句柄
        rg::TextureHandle sceneDepthHistoryHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "ScreenProbeSceneDepthHistory"_hs(viewId), [&]
        {
            return renderGraph.importTexture(
                "ScreenProbeSceneDepthHistory"_hs(viewId), &postProcess.getSceneDepthHistoryView(), rhi::ResourceState::Shader_Sampled);
        });

        renderGraph.addComputePass<ScreenProbeTemporalFilterData>(
            "ScreenProbeTemporalFilter"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeTemporalFilterData& data)
            {
                data.viewId = viewId;
                data.constants.viewRectMin = viewRectMin;
                data.constants.historyValid = historyValid ? 1u : 0u;
                data.constants.maxFramesAccumulated = kMaxFramesAccumulated;
                data.constants.downsampledViewExtent = downsampledViewExtent;
                data.dispatchGroupCount = {
                    (viewExtent.x + kThreadGroupSize - 1u) / kThreadGroupSize,
                    (viewExtent.y + kThreadGroupSize - 1u) / kThreadGroupSize };

                auto& blackboard = builder.getBlackboard();
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId)));
                data.velocity = builder.sample(blackboard.get<rg::TextureHandle>("Velocity"_hs(viewId)));
                data.sceneDepthHistory = builder.sample(sceneDepthHistoryHandle);
                data.diffuseIndirectHistoryRead = builder.sample(historyReadHandle);
                data.diffuseIndirectHistoryWrite = builder.storage(historyWriteHandle, rg::AccessType::Write_Only);
                data.diffuseIndirect = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeDiffuseIndirect"_hs(viewId)));
                data.lightingValid = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeLightingValid"_hs(viewId)));
                data.shortRangeAOStochasticOffset = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeShortRangeAOStochasticOffset"_hs(viewId)));
                data.shortRangeAO = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeShortRangeAO"_hs(viewId)));
                data.shortRangeAOHistoryRead = builder.sample(shortRangeAOHistoryReadHandle);
                data.shortRangeAOHistoryWrite = builder.storage(shortRangeAOHistoryWriteHandle, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeShortRangeAOFiltered"_hs(viewId), data.shortRangeAOHistoryWrite);
                data.roughSpecularIndirect = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRoughSpecularIndirect"_hs(viewId)));
                data.roughSpecularIndirectHistoryRead = builder.sample(roughSpecularHistoryReadHandle);
                data.roughSpecularIndirectHistoryWrite = builder.storage(roughSpecularHistoryWriteHandle, rg::AccessType::Write_Only);

                rhi::TextureDesc filteredDesc;
                filteredDesc.type = rhi::TextureType::Texture_2D;
                filteredDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                filteredDesc.extent = { viewExtent.x, viewExtent.y, 1u };
                filteredDesc.mipCount = 1u;
                filteredDesc.layerCount = 1u;
                filteredDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto diffuseIndirectFiltered = builder.createTexture("ScreenProbeDiffuseIndirectFiltered"_hs(viewId), filteredDesc);
                data.diffuseIndirectFiltered = builder.storage(diffuseIndirectFiltered, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeDiffuseIndirectFiltered"_hs(viewId), data.diffuseIndirectFiltered);

                auto roughSpecularIndirectFiltered = builder.createTexture("ScreenProbeRoughSpecularIndirectFiltered"_hs(viewId), filteredDesc);
                data.roughSpecularIndirectFiltered = builder.storage(roughSpecularIndirectFiltered, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeRoughSpecularIndirectFiltered"_hs(viewId), data.roughSpecularIndirectFiltered);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeTemporalFilterData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeTemporalFilterPass, ScreenProbeTemporalFilterStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeTemporalFilter",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeTemporalFilter, Compute)));
                    storage.layout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeTemporalFilter_Bindings);
                    storage.pipeline = createComputePipeline(rendererContext, storage.shader->getVariant(), *storage.layout);
                }

                if (storage.caches.size() <= data.viewId)
                    storage.caches.resize(data.viewId + 1u);
                auto& cache = storage.caches[data.viewId];

                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* velocity = context.getTexture(data.velocity);
                const rhi::TextureView* sceneDepthHistory = context.getTexture(data.sceneDepthHistory);
                const rhi::TextureView* diffuseIndirectHistoryRead = context.getTexture(data.diffuseIndirectHistoryRead);
                const rhi::TextureView* diffuseIndirect = context.getTexture(data.diffuseIndirect);
                rhi::Sampler* pointClampSampler = renderBackend.getSamplerCache().getPointClampNoMip();
                rhi::TextureView* diffuseIndirectFiltered = context.getTexture(data.diffuseIndirectFiltered);
                rhi::TextureView* diffuseIndirectHistoryWrite = context.getTexture(data.diffuseIndirectHistoryWrite);
                const rhi::TextureView* lightingValid = context.getTexture(data.lightingValid);
                const rhi::TextureView* shortRangeAOStochasticOffset = context.getTexture(data.shortRangeAOStochasticOffset);
                const rhi::TextureView* shortRangeAO = context.getTexture(data.shortRangeAO);
                const rhi::TextureView* shortRangeAOHistoryRead = context.getTexture(data.shortRangeAOHistoryRead);
                rhi::TextureView* shortRangeAOHistoryWrite = context.getTexture(data.shortRangeAOHistoryWrite);
                const rhi::TextureView* roughSpecularIndirect = context.getTexture(data.roughSpecularIndirect);
                const rhi::TextureView* roughSpecularIndirectHistoryRead = context.getTexture(data.roughSpecularIndirectHistoryRead);
                rhi::TextureView* roughSpecularIndirectHistoryWrite = context.getTexture(data.roughSpecularIndirectHistoryWrite);
                rhi::TextureView* roughSpecularIndirectFiltered = context.getTexture(data.roughSpecularIndirectFiltered);

                if (cache.needUpdate(
                        sceneDepth, gbufferA, gbufferB, velocity, sceneDepthHistory, diffuseIndirectHistoryRead,
                        diffuseIndirect, pointClampSampler, diffuseIndirectFiltered, diffuseIndirectHistoryWrite,
                        lightingValid, shortRangeAOStochasticOffset, shortRangeAO, shortRangeAOHistoryRead, shortRangeAOHistoryWrite,
                        roughSpecularIndirect, roughSpecularIndirectHistoryRead, roughSpecularIndirectHistoryWrite, roughSpecularIndirectFiltered))
                {
                    cache.update(renderBackend, *storage.layout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeTemporalFilter_Bindings);
                        writer.setTexture("SceneDepth", sceneDepth);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("Velocity", velocity);
                        writer.setTexture("SceneDepthHistory", sceneDepthHistory);
                        writer.setTexture("DiffuseIndirectHistory", diffuseIndirectHistoryRead);
                        writer.setTexture("DiffuseIndirect", diffuseIndirect);
                        writer.setSampler("PointClampSampler", pointClampSampler);
                        writer.setTexture("DiffuseIndirectFiltered", diffuseIndirectFiltered);
                        writer.setTexture("NewDiffuseIndirectHistory", diffuseIndirectHistoryWrite);
                        writer.setTexture("LightingValid", lightingValid);
                        writer.setTexture("ShortRangeAOStochasticOffset", shortRangeAOStochasticOffset);
                        writer.setTexture("ShortRangeAO", shortRangeAO);
                        writer.setTexture("ShortRangeAOHistory", shortRangeAOHistoryRead);
                        writer.setTexture("NewShortRangeAOHistory", shortRangeAOHistoryWrite);
                        writer.setTexture("RoughSpecularIndirect", roughSpecularIndirect);
                        writer.setTexture("RoughSpecularIndirectHistory", roughSpecularIndirectHistoryRead);
                        writer.setTexture("RoughSpecularIndirectFiltered", roughSpecularIndirectFiltered);
                        writer.setTexture("NewRoughSpecularIndirectHistory", roughSpecularIndirectHistoryWrite);
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

        postProcess.swapDiffuseIndirectHistory();
        postProcess.swapShortRangeAOHistory();
        postProcess.swapRoughSpecularIndirectHistory();
    }
}
