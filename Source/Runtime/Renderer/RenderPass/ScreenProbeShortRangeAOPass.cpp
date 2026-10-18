#include "ScreenProbeShortRangeAOPass.h"
#include "RenderPassUtils.h"
#include "HZBPass.h"

#include <RenderBase/RenderBackend.h>

#include <ScreenProbeShortRangeAODownsample.generated.h>
#include <ScreenProbeShortRangeAOUpsampleWeights.generated.h>
#include <ScreenProbeShortRangeAO.generated.h>

#include <algorithm>

namespace hyp
{
    namespace
    {
        // 必须跟 ScreenProbeShortRangeAOCommon.slang 里的同名常量一致
        constexpr uint32_t kShortRangeAODownsampleFactor = 2u;
        constexpr uint32_t kThreadGroupSize = 8u;

        uint2 dispatchGroupCountFor8(uint2 extent)
        {
            return uint2(
                std::max((extent.x + kThreadGroupSize - 1u) / kThreadGroupSize, 1u),
                std::max((extent.y + kThreadGroupSize - 1u) / kThreadGroupSize, 1u));
        }

        struct DownsamplePushConstants
        {
            uint2 viewRectMin;
            uint2 downsampledViewExtent;
        };
        static_assert(sizeof(DownsamplePushConstants) == 16);

        struct UpsampleWeightsPushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            uint2 downsampledViewExtent;
            uint2 padding0;
        };
        static_assert(sizeof(UpsampleWeightsPushConstants) == 32);

        struct HorizonSearchPushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            uint2 downsampledViewExtent;
            float2 screenToHZBUVScale;
        };
        static_assert(sizeof(HorizonSearchPushConstants) == 32);
    }

    struct ScreenProbeShortRangeAODownsampleData
    {
        uint32_t viewId;
        uint2 dispatchGroupCount;
        DownsamplePushConstants constants;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle downsampledSceneDepth;
        rg::TextureHandle downsampledWorldNormal;
    };

    struct ScreenProbeShortRangeAOUpsampleWeightsData
    {
        uint32_t viewId;
        uint2 dispatchGroupCount;
        UpsampleWeightsPushConstants constants;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle downsampledSceneDepth;
        rg::TextureHandle downsampledWorldNormal;
        rg::TextureHandle blueNoise;
        rg::TextureHandle stochasticOffset;
    };

    struct ScreenProbeShortRangeAOHorizonSearchData
    {
        uint32_t viewId;
        uint2 dispatchGroupCount;
        HorizonSearchPushConstants constants;
        rg::TextureHandle downsampledSceneDepth;
        rg::TextureHandle downsampledWorldNormal;
        rg::TextureHandle sceneHZB;
        rg::TextureHandle blueNoise;
        rg::TextureHandle shortRangeAO;
    };

    struct ScreenProbeShortRangeAOStorage
    {
        std::shared_ptr<PassShader> downsampleShader;
        TRef<rhi::BindGroupLayout> downsampleLayout;
        TRef<rhi::Pipeline> downsamplePipeline;
        std::vector<BindGroupCache<5>> downsampleCaches;

        std::shared_ptr<PassShader> upsampleWeightsShader;
        TRef<rhi::BindGroupLayout> upsampleWeightsLayout;
        TRef<rhi::Pipeline> upsampleWeightsPipeline;
        std::vector<BindGroupCache<7>> upsampleWeightsCaches;

        std::shared_ptr<PassShader> horizonSearchShader;
        TRef<rhi::BindGroupLayout> horizonSearchLayout;
        TRef<rhi::Pipeline> horizonSearchPipeline;
        std::vector<BindGroupCache<6>> horizonSearchCaches;
    };

    void ScreenProbeShortRangeAOPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 viewRectMin(static_cast<uint32_t>(viewport.x), static_cast<uint32_t>(viewport.y));
        const uint2 viewExtent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));
        const uint2 downsampledViewExtent(
            (viewExtent.x + kShortRangeAODownsampleFactor - 1u) / kShortRangeAODownsampleFactor,
            (viewExtent.y + kShortRangeAODownsampleFactor - 1u) / kShortRangeAODownsampleFactor);

        const uint2 hzbBaseExtent = HZBPass::getBaseExtent(viewExtent);
        const float2 screenToHZBUVScale(
            float(viewExtent.x) / float(2u * hzbBaseExtent.x),
            float(viewExtent.y) / float(2u * hzbBaseExtent.y));

        rg::TextureHandle blueNoiseHandle = renderGraph.importTexture(
            "ScreenProbeShortRangeAOBlueNoiseVec2Texture"_hs,
            &rendererContext.getRenderBackend().getDefaultResources().getBlueNoiseVec2View(),
            rhi::ResourceState::Shader_Sampled);

        rg::TextureHandle downsampledSceneDepthHandle;
        rg::TextureHandle downsampledWorldNormalHandle;

        renderGraph.addComputePass<ScreenProbeShortRangeAODownsampleData>(
            "ScreenProbeShortRangeAODownsample"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeShortRangeAODownsampleData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = dispatchGroupCountFor8(downsampledViewExtent);
                data.constants.viewRectMin = viewRectMin;
                data.constants.downsampledViewExtent = downsampledViewExtent;

                auto& blackboard = builder.getBlackboard();
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId)));

                rhi::TextureDesc depthDesc;
                depthDesc.type = rhi::TextureType::Texture_2D;
                depthDesc.format = rhi::TextureFormat::R32_SFLOAT;
                depthDesc.extent = { downsampledViewExtent.x, downsampledViewExtent.y, 1u };
                depthDesc.mipCount = 1u;
                depthDesc.layerCount = 1u;
                depthDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto downsampledSceneDepth = builder.createTexture("ScreenProbeShortRangeAODownsampledSceneDepth"_hs(viewId), depthDesc);

                rhi::TextureDesc normalDesc = depthDesc;
                normalDesc.format = rhi::TextureFormat::RGBA8_UNORM;
                auto downsampledWorldNormal = builder.createTexture("ScreenProbeShortRangeAODownsampledWorldNormal"_hs(viewId), normalDesc);

                data.downsampledSceneDepth = builder.storage(downsampledSceneDepth, rg::AccessType::Write_Only);
                data.downsampledWorldNormal = builder.storage(downsampledWorldNormal, rg::AccessType::Write_Only);

                downsampledSceneDepthHandle = data.downsampledSceneDepth;
                downsampledWorldNormalHandle = data.downsampledWorldNormal;
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeShortRangeAODownsampleData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeShortRangeAOPass, ScreenProbeShortRangeAOStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.downsampleShader)
                {
                    storage.downsampleShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeShortRangeAODownsample",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeShortRangeAODownsample, Compute)));
                    storage.downsampleLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeShortRangeAODownsample_Bindings);
                    storage.downsamplePipeline = createComputePipeline(rendererContext, storage.downsampleShader->getVariant(), *storage.downsampleLayout);
                }

                if (storage.downsampleCaches.size() <= data.viewId)
                    storage.downsampleCaches.resize(data.viewId + 1u);
                auto& cache = storage.downsampleCaches[data.viewId];

                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                rhi::TextureView* downsampledSceneDepth = context.getTexture(data.downsampledSceneDepth);
                rhi::TextureView* downsampledWorldNormal = context.getTexture(data.downsampledWorldNormal);

                if (cache.needUpdate(sceneDepth, gbufferA, gbufferB, downsampledSceneDepth, downsampledWorldNormal))
                {
                    cache.update(renderBackend, *storage.downsampleLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeShortRangeAODownsample_Bindings);
                        writer.setTexture("SceneDepthImage", sceneDepth);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("DownsampledSceneDepth", downsampledSceneDepth);
                        writer.setTexture("DownsampledWorldNormal", downsampledWorldNormal);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.downsamplePipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.downsamplePipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.downsamplePipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            });

        renderGraph.addComputePass<ScreenProbeShortRangeAOUpsampleWeightsData>(
            "ScreenProbeShortRangeAOUpsampleWeights"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeShortRangeAOUpsampleWeightsData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = dispatchGroupCountFor8(viewExtent);
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.downsampledViewExtent = downsampledViewExtent;
                data.constants.padding0 = uint2(0u, 0u);

                auto& blackboard = builder.getBlackboard();
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId)));
                data.downsampledSceneDepth = builder.sample(downsampledSceneDepthHandle);
                data.downsampledWorldNormal = builder.sample(downsampledWorldNormalHandle);
                data.blueNoise = builder.sample(blueNoiseHandle);

                rhi::TextureDesc offsetDesc;
                offsetDesc.type = rhi::TextureType::Texture_2D;
                offsetDesc.format = rhi::TextureFormat::R8_UINT;
                offsetDesc.extent = { viewExtent.x, viewExtent.y, 1u };
                offsetDesc.mipCount = 1u;
                offsetDesc.layerCount = 1u;
                offsetDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto stochasticOffset = builder.createTexture("ScreenProbeShortRangeAOStochasticOffset"_hs(viewId), offsetDesc);
                data.stochasticOffset = builder.storage(stochasticOffset, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeShortRangeAOStochasticOffset"_hs(viewId), data.stochasticOffset);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeShortRangeAOUpsampleWeightsData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeShortRangeAOPass, ScreenProbeShortRangeAOStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.upsampleWeightsShader)
                {
                    storage.upsampleWeightsShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeShortRangeAOUpsampleWeights",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeShortRangeAOUpsampleWeights, Compute)));
                    storage.upsampleWeightsLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeShortRangeAOUpsampleWeights_Bindings);
                    storage.upsampleWeightsPipeline = createComputePipeline(rendererContext, storage.upsampleWeightsShader->getVariant(), *storage.upsampleWeightsLayout);
                }

                if (storage.upsampleWeightsCaches.size() <= data.viewId)
                    storage.upsampleWeightsCaches.resize(data.viewId + 1u);
                auto& cache = storage.upsampleWeightsCaches[data.viewId];

                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* downsampledSceneDepth = context.getTexture(data.downsampledSceneDepth);
                const rhi::TextureView* downsampledWorldNormal = context.getTexture(data.downsampledWorldNormal);
                const rhi::TextureView* blueNoise = context.getTexture(data.blueNoise);
                rhi::TextureView* stochasticOffset = context.getTexture(data.stochasticOffset);

                if (cache.needUpdate(sceneDepth, gbufferA, gbufferB, downsampledSceneDepth, downsampledWorldNormal, blueNoise, stochasticOffset))
                {
                    cache.update(renderBackend, *storage.upsampleWeightsLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeShortRangeAOUpsampleWeights_Bindings);
                        writer.setTexture("SceneDepthImage", sceneDepth);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("DownsampledSceneDepth", downsampledSceneDepth);
                        writer.setTexture("DownsampledWorldNormal", downsampledWorldNormal);
                        writer.setTexture("BlueNoiseVec2Texture", blueNoise);
                        writer.setTexture("StochasticOffset", stochasticOffset);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.upsampleWeightsPipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.upsampleWeightsPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.upsampleWeightsPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            });

        renderGraph.addComputePass<ScreenProbeShortRangeAOHorizonSearchData>(
            "ScreenProbeShortRangeAOHorizonSearch"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeShortRangeAOHorizonSearchData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = dispatchGroupCountFor8(downsampledViewExtent);
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.downsampledViewExtent = downsampledViewExtent;
                data.constants.screenToHZBUVScale = screenToHZBUVScale;

                auto& blackboard = builder.getBlackboard();
                data.downsampledSceneDepth = builder.sample(downsampledSceneDepthHandle);
                data.downsampledWorldNormal = builder.sample(downsampledWorldNormalHandle);
                data.sceneHZB = builder.sample(blackboard.get<rg::TextureHandle>("SceneHZB"_hs(viewId)));
                data.blueNoise = builder.sample(blueNoiseHandle);

                rhi::TextureDesc shortRangeAODesc;
                shortRangeAODesc.type = rhi::TextureType::Texture_2D;
                shortRangeAODesc.format = rhi::TextureFormat::R32_UINT;
                shortRangeAODesc.extent = { downsampledViewExtent.x, downsampledViewExtent.y, 1u };
                shortRangeAODesc.mipCount = 1u;
                shortRangeAODesc.layerCount = 1u;
                shortRangeAODesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto shortRangeAO = builder.createTexture("ScreenProbeShortRangeAO"_hs(viewId), shortRangeAODesc);
                data.shortRangeAO = builder.storage(shortRangeAO, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeShortRangeAO"_hs(viewId), data.shortRangeAO);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeShortRangeAOHorizonSearchData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeShortRangeAOPass, ScreenProbeShortRangeAOStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.horizonSearchShader)
                {
                    storage.horizonSearchShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeShortRangeAO",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeShortRangeAO, Compute)));
                    storage.horizonSearchLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeShortRangeAO_Bindings);
                    storage.horizonSearchPipeline = createComputePipeline(rendererContext, storage.horizonSearchShader->getVariant(), *storage.horizonSearchLayout);
                }

                if (storage.horizonSearchCaches.size() <= data.viewId)
                    storage.horizonSearchCaches.resize(data.viewId + 1u);
                auto& cache = storage.horizonSearchCaches[data.viewId];

                const rhi::TextureView* downsampledSceneDepth = context.getTexture(data.downsampledSceneDepth);
                const rhi::TextureView* downsampledWorldNormal = context.getTexture(data.downsampledWorldNormal);
                const rhi::TextureView* sceneHZB = context.getTexture(data.sceneHZB);
                const rhi::TextureView* blueNoise = context.getTexture(data.blueNoise);
                rhi::TextureView* shortRangeAO = context.getTexture(data.shortRangeAO);
                rhi::Sampler* hzbPointClampSampler = renderBackend.getSamplerCache().getPointClampNoMip();

                if (cache.needUpdate(downsampledSceneDepth, downsampledWorldNormal, sceneHZB, hzbPointClampSampler, blueNoise, shortRangeAO))
                {
                    cache.update(renderBackend, *storage.horizonSearchLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeShortRangeAO_Bindings);
                        writer.setTexture("DownsampledSceneDepth", downsampledSceneDepth);
                        writer.setTexture("DownsampledWorldNormal", downsampledWorldNormal);
                        writer.setTexture("SceneHZB", sceneHZB);
                        writer.setSampler("HZBPointClampSampler", hzbPointClampSampler);
                        writer.setTexture("BlueNoiseVec2Texture", blueNoise);
                        writer.setTexture("ShortRangeAO", shortRangeAO);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.horizonSearchPipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.horizonSearchPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.horizonSearchPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            });
    }
}
