#include "ScreenProbePlacementPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>

#include <ScreenProbeDownsampleUniform.generated.h>
#include <ScreenProbeAdaptivePlacement.generated.h>
#include <ScreenProbeSetupIndirectArgs.generated.h>

#include <algorithm>

namespace hyp
{
    namespace
    {
        // 跟 Shader/ScreenProbe/ScreenProbeCommon.slang 里的同名常量必须保持一致——两边各自
        // 独立一份是这个引擎的既有约定(HZBTrace.slang 的追踪常量也是这么处理的), 不是漏同步
        constexpr uint32_t kScreenProbeDownsampleFactor = 16u;
        constexpr uint32_t kScreenProbeNumAdaptiveSamplesPerTile = 8u;
        constexpr float kScreenProbeAdaptiveProbeAllocationFraction = 0.5f;

        struct ScreenProbeDownsampleUniformPushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            uint2 tileGridSize;
        };
        static_assert(sizeof(ScreenProbeDownsampleUniformPushConstants) == 24);

        struct ScreenProbeAdaptivePlacementPushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            uint2 tileGridSize;
            uint2 atlasViewSize;
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
        };
        static_assert(sizeof(ScreenProbeAdaptivePlacementPushConstants) == 40);

        struct ScreenProbeSetupIndirectArgsPushConstants
        {
            uint32_t atlasWidth;
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
        };
        static_assert(sizeof(ScreenProbeSetupIndirectArgsPushConstants) == 12);

        uint2 dispatchGroupCountFor8(uint2 extent)
        {
            return uint2(
                std::max((extent.x + 7u) / 8u, 1u),
                std::max((extent.y + 7u) / 8u, 1u));
        }

        rhi::Pipeline* createDownsampleUniformPipeline(RendererContext& rendererContext, ShaderVariant shaderVariant, const rhi::BindGroupLayout& layout)
        {
            return createComputePipeline(rendererContext, shaderVariant, layout);
        }

        rhi::Pipeline* createAdaptivePlacementPipeline(RendererContext& rendererContext, ShaderVariant shaderVariant, const rhi::BindGroupLayout& layout)
        {
            return createComputePipeline(rendererContext, shaderVariant, layout);
        }
    }

    struct ScreenProbeDownsampleUniformData
    {
        uint32_t viewId;
        uint2 dispatchGroupCount;
        ScreenProbeDownsampleUniformPushConstants constants;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle velocity;
        rg::TextureHandle probeSceneDepth;
        rg::TextureHandle probeWorldNormal;
        rg::TextureHandle probeWorldSpeed;
    };

    struct ScreenProbeAdaptivePlacementData
    {
        uint32_t viewId;
        uint2 dispatchGroupCount;
        ScreenProbeAdaptivePlacementPushConstants constants;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle velocity;
        rg::TextureHandle probeSceneDepth;
        rg::TextureHandle probeWorldNormal;
        rg::TextureHandle probeWorldSpeed;
        rg::BufferHandle adaptiveProbePosAndTileIndices;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
    };

    struct ScreenProbeSetupIndirectArgsData
    {
        uint32_t viewId;
        ScreenProbeSetupIndirectArgsPushConstants constants;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
        rg::BufferHandle indirectArgs;
    };

    struct ScreenProbePlacementStorage
    {
        std::shared_ptr<PassShader> downsampleShader;
        TRef<rhi::BindGroupLayout> downsampleLayout;
        TRef<rhi::Pipeline> downsamplePipeline;
        std::vector<BindGroupCache<7>> downsampleCaches;

        std::shared_ptr<PassShader> adaptiveShader;
        TRef<rhi::BindGroupLayout> adaptiveLayout;
        TRef<rhi::Pipeline> adaptivePipeline;
        std::vector<BindGroupCache<9>> adaptiveCaches;

        std::shared_ptr<PassShader> setupIndirectArgsShader;
        TRef<rhi::BindGroupLayout> setupIndirectArgsLayout;
        TRef<rhi::Pipeline> setupIndirectArgsPipeline;
        std::vector<BindGroupCache<2>> setupIndirectArgsCaches;
    };

    uint2 ScreenProbePlacementPass::getTileGridSize(uint2 viewExtent)
    {
        return uint2(
            std::max((viewExtent.x + kScreenProbeDownsampleFactor - 1u) / kScreenProbeDownsampleFactor, 1u),
            std::max((viewExtent.y + kScreenProbeDownsampleFactor - 1u) / kScreenProbeDownsampleFactor, 1u));
    }

    uint32_t ScreenProbePlacementPass::getMaxNumAdaptiveProbes(uint2 tileGridSize)
    {
        uint32_t numUniformProbes = tileGridSize.x * tileGridSize.y;
        return static_cast<uint32_t>(float(numUniformProbes) * kScreenProbeAdaptiveProbeAllocationFraction);
    }

    uint2 ScreenProbePlacementPass::getAtlasViewSize(uint2 tileGridSize)
    {
        uint32_t maxNumAdaptiveProbes = getMaxNumAdaptiveProbes(tileGridSize);
        // 自适应探针接在统一探针后面, 追加的整行数按图集宽度(=tileGridSize.x)round up——
        // 跟 UE ScreenProbeAtlasBufferSize 同一个公式
        uint32_t extraRows = (maxNumAdaptiveProbes + std::max(tileGridSize.x, 1u) - 1u) / std::max(tileGridSize.x, 1u);
        return uint2(tileGridSize.x, tileGridSize.y + extraRows);
    }

    void ScreenProbePlacementPass::addToRenderGraph(
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

        const uint2 tileGridSize = getTileGridSize(viewExtent);
        const uint32_t numUniformProbes = tileGridSize.x * tileGridSize.y;
        const uint32_t maxNumAdaptiveProbes = getMaxNumAdaptiveProbes(tileGridSize);
        const uint2 atlasViewSize = getAtlasViewSize(tileGridSize);
        const uint32_t numTiles = tileGridSize.x * tileGridSize.y;

        const uint32_t viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(viewId);
        const rhi::BindGroup* viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();

        rg::TextureHandle probeSceneDepthHandle;
        rg::TextureHandle probeWorldNormalHandle;
        rg::TextureHandle probeWorldSpeedHandle;

        renderGraph.addComputePass<ScreenProbeDownsampleUniformData>(
            "ScreenProbeDownsampleUniform"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeDownsampleUniformData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = dispatchGroupCountFor8(tileGridSize);
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.tileGridSize = tileGridSize;
                data.viewBindGroup = viewBindGroup;
                data.viewDataDynamicOffset = viewDataDynamicOffset;

                auto& blackboard = builder.getBlackboard();
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId)));
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.velocity = builder.sample(blackboard.get<rg::TextureHandle>("Velocity"_hs(viewId)));

                rhi::TextureDesc depthDesc;
                depthDesc.type = rhi::TextureType::Texture_2D;
                depthDesc.format = rhi::TextureFormat::R32_SFLOAT;
                depthDesc.extent = { atlasViewSize.x, atlasViewSize.y, 1u };
                depthDesc.mipCount = 1u;
                depthDesc.layerCount = 1u;
                depthDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto probeSceneDepth = builder.createTexture("ScreenProbeSceneDepth"_hs(viewId), depthDesc);

                rhi::TextureDesc normalDesc = depthDesc;
                normalDesc.format = rhi::TextureFormat::RG8_UNORM;
                auto probeWorldNormal = builder.createTexture("ScreenProbeWorldNormal"_hs(viewId), normalDesc);

                rhi::TextureDesc speedDesc = depthDesc;
                speedDesc.format = rhi::TextureFormat::R16_SFLOAT;
                auto probeWorldSpeed = builder.createTexture("ScreenProbeWorldSpeed"_hs(viewId), speedDesc);

                data.probeSceneDepth = builder.storage(probeSceneDepth, rg::AccessType::Write_Only);
                data.probeWorldNormal = builder.storage(probeWorldNormal, rg::AccessType::Write_Only);
                data.probeWorldSpeed = builder.storage(probeWorldSpeed, rg::AccessType::Write_Only);

                probeSceneDepthHandle = data.probeSceneDepth;
                probeWorldNormalHandle = data.probeWorldNormal;
                probeWorldSpeedHandle = data.probeWorldSpeed;
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeDownsampleUniformData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbePlacementPass, ScreenProbePlacementStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.downsampleShader)
                {
                    storage.downsampleShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeDownsampleUniform",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeDownsampleUniform, Compute)));
                    storage.downsampleLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeDownsampleUniform_Bindings);
                    storage.downsamplePipeline = createDownsampleUniformPipeline(rendererContext, storage.downsampleShader->getVariant(), *storage.downsampleLayout);
                }

                if (storage.downsampleCaches.size() <= data.viewId)
                    storage.downsampleCaches.resize(data.viewId + 1u);
                auto& cache = storage.downsampleCaches[data.viewId];

                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                const rhi::TextureView* velocity = context.getTexture(data.velocity);
                rhi::TextureView* probeWorldNormal = context.getTexture(data.probeWorldNormal);
                rhi::TextureView* probeWorldSpeed = context.getTexture(data.probeWorldSpeed);

                if (cache.needUpdate(gbufferA, gbufferB, sceneDepth, velocity, probeSceneDepth, probeWorldNormal, probeWorldSpeed))
                {
                    cache.update(renderBackend, *storage.downsampleLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeDownsampleUniform_Bindings);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("SceneDepthImage", sceneDepth);
                        writer.setTexture("VelocityImage", velocity);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        writer.setTexture("ScreenProbeWorldNormal", probeWorldNormal);
                        writer.setTexture("ScreenProbeWorldSpeed", probeWorldSpeed);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.downsamplePipeline.get());
                cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.downsamplePipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.downsamplePipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.downsamplePipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            });

        renderGraph.addComputePass<ScreenProbeAdaptivePlacementData>(
            "ScreenProbeAdaptivePlacement"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeAdaptivePlacementData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = tileGridSize;
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.tileGridSize = tileGridSize;
                data.constants.atlasViewSize = atlasViewSize;
                data.constants.numUniformProbes = numUniformProbes;
                data.constants.maxNumAdaptiveProbes = maxNumAdaptiveProbes;
                data.viewBindGroup = viewBindGroup;
                data.viewDataDynamicOffset = viewDataDynamicOffset;

                auto& blackboard = builder.getBlackboard();
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId)));
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.velocity = builder.sample(blackboard.get<rg::TextureHandle>("Velocity"_hs(viewId)));

                // 跟统一探针共用同一张图集(Pass A 刚写完的那份句柄)——UE 也是这么做的
                // (ScreenProbeAtlasBufferSize 一张图集装下统一+自适应两种探针), 之前拆成两张
                // 独立纹理是误判 WGSL 报错原因导致的弯路, 真正的病根是 InterlockedAdd 的
                // 三参数重载, 已经改用 Atomic<uint>::add() 解决, 见 ScreenProbeAdaptivePlacement.slang
                data.probeSceneDepth = builder.storage(probeSceneDepthHandle, rg::AccessType::Read_Write);
                data.probeWorldNormal = builder.storage(probeWorldNormalHandle, rg::AccessType::Write_Only);
                data.probeWorldSpeed = builder.storage(probeWorldSpeedHandle, rg::AccessType::Write_Only);

                // Write_Only, 不是 Read_Write: 这两张 buffer 在这个 pass 里现建, 渲染图的依赖
                // 追踪角度看"之前没有任何 pass 写过它", Read_Write 会踩中 RenderGraph::access()
                // 里"未写先读只允许 imported 资源"的断言——尽管 shader 内部确实会原子读改写,
                // 但那是这个 pass 独占访问期间的事, 不需要渲染图在 pass 间调度层面知道。跟
                // GSplatPass.cpp 里 globalHistogram 首次创建时的 Write_Only 用法同一个道理
                auto adaptiveProbePosAndTileIndices = builder.createBuffer(
                    "AdaptiveScreenProbePosAndTileIndices"_hs(viewId),
                    rhi::BufferDesc{ sizeof(uint32_t) * (maxNumAdaptiveProbes + numTiles * kScreenProbeNumAdaptiveSamplesPerTile), rhi::BufferUsage::Storage });
                data.adaptiveProbePosAndTileIndices = builder.accessBuffer(adaptiveProbePosAndTileIndices, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);

                auto adaptiveProbeTotalAndTileCounts = builder.createBuffer(
                    "AdaptiveScreenProbeTotalAndTileCounts"_hs(viewId),
                    rhi::BufferDesc{ sizeof(uint32_t) * (1u + numTiles), rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst });
                data.adaptiveProbeTotalAndTileCounts = builder.accessBuffer(adaptiveProbeTotalAndTileCounts, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);

                builder.getBlackboard().put("ScreenProbeSceneDepth"_hs(viewId), data.probeSceneDepth);
                builder.getBlackboard().put("ScreenProbeWorldNormal"_hs(viewId), data.probeWorldNormal);
                builder.getBlackboard().put("ScreenProbeWorldSpeed"_hs(viewId), data.probeWorldSpeed);
                builder.getBlackboard().put("AdaptiveScreenProbePosAndTileIndices"_hs(viewId), data.adaptiveProbePosAndTileIndices);
                builder.getBlackboard().put("AdaptiveScreenProbeTotalAndTileCounts"_hs(viewId), data.adaptiveProbeTotalAndTileCounts);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeAdaptivePlacementData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbePlacementPass, ScreenProbePlacementStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.adaptiveShader)
                {
                    storage.adaptiveShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeAdaptivePlacement",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeAdaptivePlacement, Compute)));
                    storage.adaptiveLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeAdaptivePlacement_Bindings);
                    storage.adaptivePipeline = createAdaptivePlacementPipeline(rendererContext, storage.adaptiveShader->getVariant(), *storage.adaptiveLayout);
                }

                if (storage.adaptiveCaches.size() <= data.viewId)
                    storage.adaptiveCaches.resize(data.viewId + 1u);
                auto& cache = storage.adaptiveCaches[data.viewId];

                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                const rhi::TextureView* velocity = context.getTexture(data.velocity);
                rhi::TextureView* probeWorldNormal = context.getTexture(data.probeWorldNormal);
                rhi::TextureView* probeWorldSpeed = context.getTexture(data.probeWorldSpeed);
                rhi::Buffer* adaptiveProbePosAndTileIndices = context.getBuffer(data.adaptiveProbePosAndTileIndices);
                rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);

                if (cache.needUpdate(gbufferA, gbufferB, sceneDepth, velocity, probeSceneDepth, probeWorldNormal, probeWorldSpeed, adaptiveProbePosAndTileIndices, adaptiveProbeTotalAndTileCounts))
                {
                    cache.update(renderBackend, *storage.adaptiveLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeAdaptivePlacement_Bindings);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("SceneDepthImage", sceneDepth);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        writer.setTexture("VelocityImage", velocity);
                        writer.setTexture("ScreenProbeWorldNormal", probeWorldNormal);
                        writer.setTexture("ScreenProbeWorldSpeed", probeWorldSpeed);
                        writer.setBuffer("AdaptiveScreenProbePosAndTileIndices", adaptiveProbePosAndTileIndices);
                        writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();

                // TotalAndTileCounts 是原子自增目标, 这一帧开始前必须清零——瞬态 buffer 池复用时会
                // 残留上一次使用它的那个 pass 写下的值。clearBuffer 底层是 vkCmdFillBuffer, 属于
                // transfer 写, RenderGraph 自动推导的 barrier 覆盖不到这次写, 需要手动插,
                // 跟 GSplatPass.cpp 清 GlobalHistogram 同一个套路
                cmdList.clearBuffer(adaptiveProbeTotalAndTileCounts, 0, rhi::kWholeBuffer);

                rhi::BufferBarrier countsBarrier;
                countsBarrier.buffer = adaptiveProbeTotalAndTileCounts;
                countsBarrier.offset = 0;
                countsBarrier.size = rhi::kWholeBuffer;
                countsBarrier.oldState = rhi::ResourceState::Transfer_Dst;
                countsBarrier.newState = rhi::ResourceState::Compute_Shader_Storage;
                cmdList.insertBarriers({ &countsBarrier, 1 });
                context.beginPass();

                cmdList.bindPipeline(storage.adaptivePipeline.get());
                cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.adaptivePipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.adaptivePipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.adaptivePipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            },
            rg::PassFlags::Skip_Begin_Pass);

        renderGraph.addComputePass<ScreenProbeSetupIndirectArgsData>(
            "ScreenProbeSetupIndirectArgs"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeSetupIndirectArgsData& data)
            {
                data.viewId = viewId;
                data.constants.atlasWidth = atlasViewSize.x;
                data.constants.numUniformProbes = numUniformProbes;
                data.constants.maxNumAdaptiveProbes = maxNumAdaptiveProbes;

                auto& blackboard = builder.getBlackboard();
                auto counts = blackboard.get<rg::BufferHandle>("AdaptiveScreenProbeTotalAndTileCounts"_hs(viewId));
                data.adaptiveProbeTotalAndTileCounts = builder.accessBuffer(counts, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

                auto indirectArgs = builder.createBuffer(
                    "ScreenProbeIndirectArgs"_hs(viewId),
                    rhi::BufferDesc{ sizeof(uint32_t) * 3u, rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect });
                data.indirectArgs = builder.accessBuffer(indirectArgs, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);

                blackboard.put("ScreenProbeIndirectArgs"_hs(viewId), data.indirectArgs);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeSetupIndirectArgsData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbePlacementPass, ScreenProbePlacementStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.setupIndirectArgsShader)
                {
                    storage.setupIndirectArgsShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeSetupIndirectArgs",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeSetupIndirectArgs, Compute)));
                    storage.setupIndirectArgsLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeSetupIndirectArgs_Bindings);
                    storage.setupIndirectArgsPipeline = createComputePipeline(rendererContext, storage.setupIndirectArgsShader->getVariant(), *storage.setupIndirectArgsLayout);
                }

                if (storage.setupIndirectArgsCaches.size() <= data.viewId)
                    storage.setupIndirectArgsCaches.resize(data.viewId + 1u);
                auto& cache = storage.setupIndirectArgsCaches[data.viewId];

                rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);
                rhi::Buffer* indirectArgs = context.getBuffer(data.indirectArgs);

                if (cache.needUpdate(adaptiveProbeTotalAndTileCounts, indirectArgs))
                {
                    cache.update(renderBackend, *storage.setupIndirectArgsLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeSetupIndirectArgs_Bindings);
                        writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                        writer.setBuffer("ScreenProbeIndirectArgs", indirectArgs);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.setupIndirectArgsPipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.setupIndirectArgsPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.setupIndirectArgsPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatch(1u, 1u, 1u);
            });
    }
}
