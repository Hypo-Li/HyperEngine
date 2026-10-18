#include "ScreenProbeVisualizerPass.h"
#include "RenderPassUtils.h"
#include "ScreenProbeConstants.h"
#include "ScreenProbePlacementPass.h"

#include <RenderBase/RenderBackend.h>

#include <ScreenProbeBuildDrawIndirect.generated.h>
#include <ScreenProbeVisualizer.generated.h>

namespace hyp
{
    namespace
    {
        struct DrawIndirectCommand
        {
            uint32_t vertexCount;
            uint32_t instanceCount;
            uint32_t firstVertex;
            uint32_t firstInstance;
        };

        struct ScreenProbeBuildDrawIndirectPushConstants
        {
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
        };
        static_assert(sizeof(ScreenProbeBuildDrawIndirectPushConstants) == 8);

        struct ScreenProbeVisualizerPushConstants
        {
            uint2 atlasViewSize;
            uint32_t numUniformProbes;
            float markerHalfSizePixels;
            uint2 viewRectMin;
            uint2 viewExtent;
            uint32_t colorMode;
            uint32_t padding;
        };
        static_assert(sizeof(ScreenProbeVisualizerPushConstants) == 40);

        // 3x3 像素的标记——半径 1.5px, 用户已经确认这个尺寸("画一个 3x3 的十字")
        constexpr float kMarkerHalfSizePixels = 1.5f;

        rhi::Pipeline* createVisualizerPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout
        )
        {
            auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr);
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

            // 调试标记: 不测深度(不管标记对应的探针深度是否被其它物体遮挡都要看得见), 不裁剪
            // (标记本身就是要盖在已有画面上), 不透明直接覆盖(默认 ColorBlendState, 不开混合)
            GraphicsPipelineState pipelineState;
            pipelineState.topology = static_cast<uint32_t>(rhi::PrimitiveTopology::Triangle_Strip);
            pipelineState.samples = 0;
            pipelineState.cullMode = static_cast<uint32_t>(rhi::CullMode::None);
            pipelineState.frontFace = static_cast<uint32_t>(rhi::FrontFace::Counter_Clockwise);
            pipelineState.depthTestEnable = 0;
            pipelineState.depthWriteEnable = 0;
            pipelineState.depthCompareOp = static_cast<uint32_t>(rhi::CompareOp::Always);
            pipelineState.stencilTestEnable = 0;
            pipelineState.stencilState = StencilState::get();
            pipelineState.colorBlendState = ColorBlendState::get();
            pipelineState.shader = shaderVariant;
            pipelineState.attachmentFormats = AttachmentFormats::get<
                rhi::TextureFormat::Undefined,
                rhi::TextureFormat::RGBA16_SFLOAT
            >();

            PipelineCache::GraphicsKey pipelineKey;
            pipelineKey.pipelineState = pipelineState;
            pipelineKey.pipelineLayout = pipelineLayout;
            return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
        }
    }

    struct ScreenProbeVisualizerPrepareData
    {
        uint32_t viewId;
        ScreenProbeBuildDrawIndirectPushConstants constants;
        rg::BufferHandle numAdaptiveScreenProbesCounter;
        rg::BufferHandle drawIndirect;
    };

    struct ScreenProbeVisualizerDrawData
    {
        uint32_t viewId;
        float4 viewport;
        ScreenProbeVisualizerPushConstants constants;
        rg::TextureHandle sceneColor;
        rg::TextureHandle probeSceneDepth;
        rg::TextureHandle probeRadiance;
        rg::TextureHandle probeIrradiance;
        rg::TextureHandle probeRadianceSHAmbient;
        rg::TextureHandle probeRadianceSHDirectional;
        rg::BufferHandle adaptiveProbePosAndTileIndices;
        rg::BufferHandle probeBRDFPDFSH;
        rg::BufferHandle drawIndirect;
    };

    struct ScreenProbeVisualizerStorage
    {
        std::shared_ptr<PassShader> prepareShader;
        TRef<rhi::BindGroupLayout> prepareLayout;
        TRef<rhi::Pipeline> preparePipeline;
        std::vector<BindGroupCache<2>> prepareCaches;

        std::shared_ptr<PassShader> drawShader;
        TRef<rhi::BindGroupLayout> drawLayout;
        TRef<rhi::Pipeline> drawPipeline;
        std::vector<BindGroupCache<4 + kScreenProbeIrradianceTextureCount>> drawCaches;
    };

    void ScreenProbeVisualizerPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view
    )
    {
        const uint32_t viewId = view.getViewId();
        const SceneVisualizeMode visualizeMode = scene.getSceneVisualizeMode();
        const uint32_t colorMode = visualizeMode == SceneVisualizeMode::ScreenProbeRadiance ? 1u
            : visualizeMode == SceneVisualizeMode::ScreenProbeIrradiance ? 2u
            : visualizeMode == SceneVisualizeMode::ScreenProbeBRDFPDF ? 3u
            : 0u;
        const float4 viewport = view.getViewport();
        const uint2 viewRectMin(static_cast<uint32_t>(viewport.x), static_cast<uint32_t>(viewport.y));
        const uint2 viewExtent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));

        const uint2 tileGridSize = ScreenProbePlacementPass::getTileGridSize(viewExtent);
        const uint32_t numUniformProbes = tileGridSize.x * tileGridSize.y;
        const uint32_t maxNumAdaptiveProbes = ScreenProbePlacementPass::getMaxNumAdaptiveProbes(tileGridSize);
        const uint2 atlasViewSize = ScreenProbePlacementPass::getAtlasViewSize(tileGridSize);

        rg::BufferHandle drawIndirectHandle;

        renderGraph.addComputePass<ScreenProbeVisualizerPrepareData>(
            "ScreenProbeVisualizerPrepareIndirect"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeVisualizerPrepareData& data)
            {
                data.viewId = viewId;
                data.constants.numUniformProbes = numUniformProbes;
                data.constants.maxNumAdaptiveProbes = maxNumAdaptiveProbes;

                auto& blackboard = builder.getBlackboard();
                auto counter = blackboard.get<rg::BufferHandle>("AdaptiveScreenProbeTotalAndTileCounts"_hs(viewId));
                data.numAdaptiveScreenProbesCounter = builder.accessBuffer(counter, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

                auto drawIndirect = builder.createBuffer(
                    "ScreenProbeDrawIndirect"_hs(viewId),
                    rhi::BufferDesc{ sizeof(DrawIndirectCommand), rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect | rhi::BufferUsage::Transfer_Dst });
                data.drawIndirect = builder.accessBuffer(drawIndirect, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);
                drawIndirectHandle = data.drawIndirect;
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeVisualizerPrepareData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeVisualizerPass, ScreenProbeVisualizerStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.prepareShader)
                {
                    storage.prepareShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeBuildDrawIndirect",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeBuildDrawIndirect, Compute)));
                    storage.prepareLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeBuildDrawIndirect_Bindings);
                    storage.preparePipeline = createComputePipeline(rendererContext, storage.prepareShader->getVariant(), *storage.prepareLayout);
                }

                if (storage.prepareCaches.size() <= data.viewId)
                    storage.prepareCaches.resize(data.viewId + 1u);
                auto& cache = storage.prepareCaches[data.viewId];

                rhi::Buffer* counter = context.getBuffer(data.numAdaptiveScreenProbesCounter);
                rhi::Buffer* drawIndirect = context.getBuffer(data.drawIndirect);

                if (cache.needUpdate(counter, drawIndirect))
                {
                    cache.update(renderBackend, *storage.prepareLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeBuildDrawIndirect_Bindings);
                        writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", counter);
                        writer.setBuffer("ScreenProbeDrawIndirect", drawIndirect);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();

                // vertexCount/firstVertex/firstInstance 是常量, instanceCount 这里先占位填 0,
                // 真正的值由下面 dispatch 的 compute shader 按实际分配出的自适应探针数算出。
                // 瞬态 buffer 每帧都是新分配的, 不能像 GSplat 那样只在"第一次"拷贝一次
                DrawIndirectCommand initialCommand{ 4u, 0u, 0u, 0u };
                auto allocation = renderBackend.getStagingAllocator().forceAllocate(sizeof(DrawIndirectCommand));
                std::memcpy(allocation.mappedData, &initialCommand, sizeof(DrawIndirectCommand));
                rhi::BufferCopyRegion region;
                region.srcOffset = allocation.offset;
                region.dstOffset = 0;
                region.size = sizeof(DrawIndirectCommand);
                cmdList.copyBuffer(allocation.buffer, drawIndirect, { &region, 1 });

                // clearBuffer/copyBuffer 底层是 transfer 写, RenderGraph 自动推导的 barrier
                // 覆盖不到这次写, 需要手动插, 跟 GSplatPass.cpp 清 GlobalHistogram 同一个套路
                rhi::BufferBarrier barrier;
                barrier.buffer = drawIndirect;
                barrier.offset = 0;
                barrier.size = rhi::kWholeBuffer;
                barrier.oldState = rhi::ResourceState::Transfer_Dst;
                barrier.newState = rhi::ResourceState::Compute_Shader_Storage;
                cmdList.insertBarriers({ &barrier, 1 });
                context.beginPass();

                cmdList.bindPipeline(storage.preparePipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.preparePipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.preparePipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatch(1u, 1u, 1u);
            },
            rg::PassFlags::Skip_Begin_Pass);

        renderGraph.addGraphicsPass<ScreenProbeVisualizerDrawData>(
            "ScreenProbeVisualizerDraw"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeVisualizerDrawData& data)
            {
                data.viewId = viewId;
                data.viewport = viewport;
                data.constants.atlasViewSize = atlasViewSize;
                data.constants.numUniformProbes = numUniformProbes;
                data.constants.markerHalfSizePixels = kMarkerHalfSizePixels;
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.colorMode = colorMode;
                data.constants.padding = 0u;

                auto& blackboard = builder.getBlackboard();
                auto sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(viewId));
                data.sceneColor = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Load);
                blackboard.put("SceneColor"_hs(viewId), data.sceneColor);

                auto probeSceneDepth = blackboard.get<rg::TextureHandle>("ScreenProbeSceneDepth"_hs(viewId));
                data.probeSceneDepth = builder.sample(probeSceneDepth);
                blackboard.put("ScreenProbeSceneDepth"_hs(viewId), data.probeSceneDepth);

                if (colorMode == 1u)
                    data.probeRadiance = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRadianceFiltered"_hs(viewId)));
                if (colorMode == 2u)
                {
                    if constexpr (kScreenProbeIrradianceFormat == ScreenProbeIrradianceFormat::SH3)
                    {
                        data.probeRadianceSHAmbient = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRadianceSHAmbient"_hs(viewId)));
                        data.probeRadianceSHDirectional = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRadianceSHDirectional"_hs(viewId)));
                    }
                    else
                    {
                        data.probeIrradiance = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeIrradianceWithBorder"_hs(viewId)));
                    }
                }

                // ScreenProbeImportanceSamplingPass 现在跟 Placement 一起无条件跑(见 RenderPipeline.cpp),
                // 不用像上面两个纹理那样按 colorMode 条件采样
                data.probeBRDFPDFSH = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("ScreenProbeBRDFPDFSH"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

                auto adaptiveProbePosAndTileIndices = blackboard.get<rg::BufferHandle>("AdaptiveScreenProbePosAndTileIndices"_hs(viewId));
                data.adaptiveProbePosAndTileIndices = builder.accessBuffer(adaptiveProbePosAndTileIndices, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

                data.drawIndirect = builder.accessBuffer(drawIndirectHandle, rhi::BufferUsage::Indirect, rg::AccessType::Read_Only);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeVisualizerDrawData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeVisualizerPass, ScreenProbeVisualizerStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.drawShader)
                {
                    storage.drawShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeVisualizer",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(ScreenProbeVisualizer, Vertex),
                            GET_PASS_SHADER_BYTECODE(ScreenProbeVisualizer, Fragment)));
                    storage.drawLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeVisualizer_Bindings);
                    storage.drawPipeline = createVisualizerPipeline(rendererContext, storage.drawShader->getVariant(), *storage.drawLayout);
                }

                if (storage.drawCaches.size() <= data.viewId)
                    storage.drawCaches.resize(data.viewId + 1u);
                auto& cache = storage.drawCaches[data.viewId];

                const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                const rhi::TextureView* probeRadiance = data.constants.colorMode == 1u
                    ? context.getTexture(data.probeRadiance)
                    : &renderBackend.getDefaultResources().getBlackTextureView();
                const rhi::TextureView* blackTexture = &renderBackend.getDefaultResources().getBlackTextureView();
                const rhi::TextureView* probeIrradiance = blackTexture;
                const rhi::TextureView* probeRadianceSHAmbient = blackTexture;
                const rhi::TextureView* probeRadianceSHDirectional = blackTexture;
                if (data.constants.colorMode == 2u)
                {
                    if constexpr (kScreenProbeIrradianceFormat == ScreenProbeIrradianceFormat::SH3)
                    {
                        probeRadianceSHAmbient = context.getTexture(data.probeRadianceSHAmbient);
                        probeRadianceSHDirectional = context.getTexture(data.probeRadianceSHDirectional);
                    }
                    else
                    {
                        probeIrradiance = context.getTexture(data.probeIrradiance);
                    }
                }
                rhi::Buffer* adaptiveProbePosAndTileIndices = context.getBuffer(data.adaptiveProbePosAndTileIndices);
                rhi::Buffer* probeBRDFPDFSH = context.getBuffer(data.probeBRDFPDFSH);

                auto needUpdate = [&](auto... irradianceHandles)
                {
                    return cache.needUpdate(
                        probeSceneDepth, adaptiveProbePosAndTileIndices, probeRadiance, probeBRDFPDFSH, irradianceHandles...);
                };

                bool bindingsChanged;
                if constexpr (kScreenProbeIrradianceFormat == ScreenProbeIrradianceFormat::SH3)
                    bindingsChanged = needUpdate(probeRadianceSHAmbient, probeRadianceSHDirectional);
                else
                    bindingsChanged = needUpdate(probeIrradiance);

                if (bindingsChanged)
                {
                    cache.update(renderBackend, *storage.drawLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeVisualizer_Bindings);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        writer.setBuffer("AdaptiveScreenProbePosAndTileIndices", adaptiveProbePosAndTileIndices);
                        writer.setTexture("ScreenProbeRadiance", probeRadiance);
                        if constexpr (kScreenProbeIrradianceFormat == ScreenProbeIrradianceFormat::SH3)
                        {
                            writer.setTexture("ScreenProbeRadianceSHAmbient", probeRadianceSHAmbient);
                            writer.setTexture("ScreenProbeRadianceSHDirectional", probeRadianceSHDirectional);
                        }
                        else
                        {
                            writer.setTexture("ScreenProbeIrradianceWithBorder", probeIrradiance);
                        }
                        writer.setBuffer("ScreenProbeBRDFPDFSH", probeBRDFPDFSH);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                setViewportAndScissor(cmdList, data.viewport);
                cmdList.bindPipeline(storage.drawPipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Graphics, &storage.drawPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.drawPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));

                const rhi::Buffer* drawIndirect = context.getBuffer(data.drawIndirect);
                cmdList.drawIndirect(drawIndirect, 0, 1, sizeof(DrawIndirectCommand));
            });
    }
}
