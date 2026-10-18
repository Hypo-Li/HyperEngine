#include "ScreenProbeIntegratePass.h"
#include "RenderPassUtils.h"
#include "ScreenProbeConstants.h"
#include "ScreenProbePlacementPass.h"

#include <RenderBase/RenderBackend.h>

#include <ScreenProbeIntegrate.generated.h>

namespace hyp
{
    namespace
    {
        // 跟 Shader/ScreenProbe/ScreenProbeIntegrate.slang 里的 ScreenProbeIntegratePushConstants 逐字段对应
        struct ScreenProbeIntegratePushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            uint2 tileGridSize;
            uint2 atlasViewSize;
            uint32_t maxNumAdaptiveProbes;
        };
        static_assert(sizeof(ScreenProbeIntegratePushConstants) == 36);

        constexpr uint32_t kThreadGroupSize = 8u;
    }

    struct ScreenProbeIntegrateData
    {
        uint32_t viewId;
        ScreenProbeIntegratePushConstants constants;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle gbufferD;
        rg::TextureHandle probeSceneDepth;
        rg::TextureHandle probeIrradiance;
        rg::TextureHandle probeRadianceSHAmbient;
        rg::TextureHandle probeRadianceSHDirectional;
        rg::TextureHandle probeRadianceWithBorder;
        rg::BufferHandle adaptiveProbePosAndTileIndices;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
        rg::TextureHandle blueNoise;
        rg::TextureHandle diffuseIndirect;
        rg::TextureHandle roughSpecularIndirect;
        rg::TextureHandle lightingValid;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
    };

    struct ScreenProbeIntegrateStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> layout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<14 + kScreenProbeIrradianceTextureCount>> caches;
    };

    void ScreenProbeIntegratePass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 viewRectMin(static_cast<uint32_t>(viewport.x), static_cast<uint32_t>(viewport.y));
        const uint2 viewExtent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));

        const uint2 tileGridSize = ScreenProbePlacementPass::getTileGridSize(viewExtent);
        const uint2 atlasViewSize = ScreenProbePlacementPass::getAtlasViewSize(tileGridSize);

        rg::TextureHandle blueNoiseHandle = renderGraph.importTexture(
            "ScreenProbeIntegrateBlueNoiseVec2Texture"_hs,
            &rendererContext.getRenderBackend().getDefaultResources().getBlueNoiseVec2View(),
            rhi::ResourceState::Shader_Sampled);

        const uint32_t viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(viewId);
        const rhi::BindGroup* viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();

        renderGraph.addComputePass<ScreenProbeIntegrateData>(
            "ScreenProbeIntegrate"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeIntegrateData& data)
            {
                data.viewId = viewId;
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.tileGridSize = tileGridSize;
                data.constants.atlasViewSize = atlasViewSize;
                data.constants.maxNumAdaptiveProbes = ScreenProbePlacementPass::getMaxNumAdaptiveProbes(tileGridSize);
                data.viewBindGroup = viewBindGroup;
                data.viewDataDynamicOffset = viewDataDynamicOffset;

                auto& blackboard = builder.getBlackboard();
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId)));
                data.gbufferC = builder.sample(blackboard.get<rg::TextureHandle>("GBufferC"_hs(viewId)));
                data.gbufferD = builder.sample(blackboard.get<rg::TextureHandle>("GBufferD"_hs(viewId)));
                data.probeSceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeSceneDepth"_hs(viewId)));
                if constexpr (kScreenProbeIrradianceFormat == ScreenProbeIrradianceFormat::SH3)
                {
                    data.probeRadianceSHAmbient = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRadianceSHAmbient"_hs(viewId)));
                    data.probeRadianceSHDirectional = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRadianceSHDirectional"_hs(viewId)));
                }
                else
                {
                    data.probeIrradiance = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeIrradianceWithBorder"_hs(viewId)));
                }
                data.probeRadianceWithBorder = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRadianceWithBorder"_hs(viewId)));
                data.adaptiveProbePosAndTileIndices = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("AdaptiveScreenProbePosAndTileIndices"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.adaptiveProbeTotalAndTileCounts = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("AdaptiveScreenProbeTotalAndTileCounts"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.blueNoise = builder.sample(blueNoiseHandle);

                rhi::TextureDesc diffuseIndirectDesc;
                diffuseIndirectDesc.type = rhi::TextureType::Texture_2D;
                diffuseIndirectDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                diffuseIndirectDesc.extent = { viewExtent.x, viewExtent.y, 1u };
                diffuseIndirectDesc.mipCount = 1u;
                diffuseIndirectDesc.layerCount = 1u;
                diffuseIndirectDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto diffuseIndirect = builder.createTexture("ScreenProbeDiffuseIndirect"_hs(viewId), diffuseIndirectDesc);
                data.diffuseIndirect = builder.storage(diffuseIndirect, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeDiffuseIndirect"_hs(viewId), data.diffuseIndirect);

                auto roughSpecularIndirect = builder.createTexture("ScreenProbeRoughSpecularIndirect"_hs(viewId), diffuseIndirectDesc);
                data.roughSpecularIndirect = builder.storage(roughSpecularIndirect, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeRoughSpecularIndirect"_hs(viewId), data.roughSpecularIndirect);

                rhi::TextureDesc lightingValidDesc;
                lightingValidDesc.type = rhi::TextureType::Texture_2D;
                lightingValidDesc.format = rhi::TextureFormat::R8_UNORM;
                lightingValidDesc.extent = { viewExtent.x, viewExtent.y, 1u };
                lightingValidDesc.mipCount = 1u;
                lightingValidDesc.layerCount = 1u;
                lightingValidDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto lightingValid = builder.createTexture("ScreenProbeLightingValid"_hs(viewId), lightingValidDesc);
                data.lightingValid = builder.storage(lightingValid, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeLightingValid"_hs(viewId), data.lightingValid);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeIntegrateData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeIntegratePass, ScreenProbeIntegrateStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeIntegrate",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeIntegrate, Compute)));
                    storage.layout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeIntegrate_Bindings);
                    storage.pipeline = createComputePipeline(rendererContext, storage.shader->getVariant(), *storage.layout);
                }

                if (storage.caches.size() <= data.viewId)
                    storage.caches.resize(data.viewId + 1u);
                auto& cache = storage.caches[data.viewId];

                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* gbufferC = context.getTexture(data.gbufferC);
                const rhi::TextureView* gbufferD = context.getTexture(data.gbufferD);
                const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                const rhi::TextureView* probeIrradiance = nullptr;
                const rhi::TextureView* probeRadianceSHAmbient = nullptr;
                const rhi::TextureView* probeRadianceSHDirectional = nullptr;
                if constexpr (kScreenProbeIrradianceFormat == ScreenProbeIrradianceFormat::SH3)
                {
                    probeRadianceSHAmbient = context.getTexture(data.probeRadianceSHAmbient);
                    probeRadianceSHDirectional = context.getTexture(data.probeRadianceSHDirectional);
                }
                else
                {
                    probeIrradiance = context.getTexture(data.probeIrradiance);
                }
                const rhi::TextureView* probeRadianceWithBorder = context.getTexture(data.probeRadianceWithBorder);
                rhi::Buffer* adaptiveProbePosAndTileIndices = context.getBuffer(data.adaptiveProbePosAndTileIndices);
                rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);
                const rhi::TextureView* blueNoise = context.getTexture(data.blueNoise);
                rhi::Sampler* linearClampSampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                rhi::TextureView* diffuseIndirect = context.getTexture(data.diffuseIndirect);
                rhi::TextureView* roughSpecularIndirect = context.getTexture(data.roughSpecularIndirect);
                rhi::TextureView* lightingValid = context.getTexture(data.lightingValid);

                auto needUpdate = [&](auto... irradianceHandles)
                {
                    return cache.needUpdate(
                        sceneDepth, gbufferA, gbufferB, gbufferC, gbufferD, probeSceneDepth, probeRadianceWithBorder,
                        adaptiveProbePosAndTileIndices, adaptiveProbeTotalAndTileCounts, blueNoise, linearClampSampler,
                        diffuseIndirect, roughSpecularIndirect, lightingValid, irradianceHandles...);
                };

                bool bindingsChanged;
                if constexpr (kScreenProbeIrradianceFormat == ScreenProbeIrradianceFormat::SH3)
                    bindingsChanged = needUpdate(probeRadianceSHAmbient, probeRadianceSHDirectional);
                else
                    bindingsChanged = needUpdate(probeIrradiance);

                if (bindingsChanged)
                {
                    cache.update(renderBackend, *storage.layout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeIntegrate_Bindings);
                        writer.setTexture("SceneDepthImage", sceneDepth);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("GbufferCImage", gbufferC);
                        writer.setTexture("GbufferDImage", gbufferD);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        if constexpr (kScreenProbeIrradianceFormat == ScreenProbeIrradianceFormat::SH3)
                        {
                            writer.setTexture("ScreenProbeRadianceSHAmbient", probeRadianceSHAmbient);
                            writer.setTexture("ScreenProbeRadianceSHDirectional", probeRadianceSHDirectional);
                        }
                        else
                        {
                            writer.setTexture("ScreenProbeIrradianceWithBorder", probeIrradiance);
                        }
                        writer.setTexture("ScreenProbeRadianceWithBorder", probeRadianceWithBorder);
                        writer.setBuffer("AdaptiveScreenProbePosAndTileIndices", adaptiveProbePosAndTileIndices);
                        writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                        writer.setTexture("BlueNoiseVec2Texture", blueNoise);
                        writer.setSampler("LinearSampler", linearClampSampler);
                        writer.setTexture("DiffuseIndirect", diffuseIndirect);
                        writer.setTexture("RoughSpecularIndirect", roughSpecularIndirect);
                        writer.setTexture("LightingValid", lightingValid);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatch(
                    (data.constants.viewExtent.x + kThreadGroupSize - 1u) / kThreadGroupSize,
                    (data.constants.viewExtent.y + kThreadGroupSize - 1u) / kThreadGroupSize,
                    1u);
            });
    }
}
