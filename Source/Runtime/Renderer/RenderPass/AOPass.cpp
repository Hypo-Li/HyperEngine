#include "AOPass.h"
#include "HZBPass.h"
#include "RenderPassUtils.h"

#include <cmath>

#include <SSAOSetup.generated.h>
#include <SSAOHalfRes.generated.h>
#include <SSAOFullRes.generated.h>
#include <AOGenerateGTAO.generated.h>
#include <AOSpatialFilterGTAO.generated.h>
#include <AOTemporalFilterGTAO.generated.h>

namespace hyp
{
    namespace
    {
        uint2 dispatchGroupCountFor(uint2 resolution)
        {
            return uint2(
                std::max((resolution.x + 7u) / 8u, 1u),
                std::max((resolution.y + 7u) / 8u, 1u));
        }

        rhi::TextureDesc makeAOTextureDesc(uint2 extent)
        {
            rhi::TextureDesc desc;
            desc.type = rhi::TextureType::Texture_2D;
            desc.format = rhi::TextureFormat::R8_UNORM;
            desc.extent = { extent.x, extent.y, 1 };
            desc.mipCount = 1;
            desc.layerCount = 1;
            desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
            return desc;
        }

        struct AOGenerateGTAOConstants
        {
            float radius;
            float fadeDistance;
            float fadeRadius;
            uint32_t quality;
            uint32_t hzbMipCount;
            uint32_t padding0; // keeps screenToHZBUVScale at offset 24 under WGSL's std430 rules too
            float2 screenToHZBUVScale;
        };
        static_assert(sizeof(AOGenerateGTAOConstants) == 32);

        struct AOGenerateGTAOData
        {
            uint32_t viewId;
            uint2 dispatchGroupCount;
            AOGenerateGTAOConstants constants;
            rg::TextureHandle sceneDepth;
            rg::TextureHandle sceneHZB;
            rg::TextureHandle aoRaw;
        };

        struct AOSpatialFilterGTAOData
        {
            uint32_t viewId;
            uint2 dispatchGroupCount;
            float intensity;
            float power;
            rg::TextureHandle sceneDepth;
            rg::TextureHandle aoRaw;
            rg::TextureHandle aoFiltered;
        };

        struct AOTemporalFilterGTAOConstants
        {
            uint32_t historyValid;
            float temporalBlendWeight;
        };

        struct AOTemporalFilterGTAOData
        {
            uint32_t viewId;
            uint2 dispatchGroupCount;
            AOTemporalFilterGTAOConstants constants;
            rg::TextureHandle velocity;
            rg::TextureHandle aoFiltered;
            rg::TextureHandle historyRead;
            rg::TextureHandle historyWrite;
        };

        struct AOPassGTAOStorage
        {
            std::shared_ptr<PassShader> generateShader;
            TRef<rhi::BindGroupLayout> generateLayout;
            TRef<rhi::Pipeline> generatePipeline;
            std::vector<BindGroupCache<4>> generateCaches;

            std::shared_ptr<PassShader> spatialShader;
            TRef<rhi::BindGroupLayout> spatialLayout;
            TRef<rhi::Pipeline> spatialPipeline;
            std::vector<BindGroupCache<3>> spatialCaches;

            std::shared_ptr<PassShader> temporalShader;
            TRef<rhi::BindGroupLayout> temporalLayout;
            TRef<rhi::Pipeline> temporalPipeline;
            std::vector<BindGroupCache<5>> temporalCaches;
        };

        // 严格对照 UE PostProcessAmbientOcclusion.usf 在 AmbientOcclusionQuality=50(ShaderQuality=2,
        // Levels=2)下的三 pass 结构重写: Setup(半分辨率法线/深度)-> 半分辨率 AO -> 全分辨率 AO
        // (读半分辨率结果做 upsample 混合)。公式细节见 Shader/PostProcess/SSAOCommon.slang 顶部说明
        // 与 Document/SSAO_Rewrite_Plan.md。

        struct SSAOSetupConstants
        {
            float thresholdInverse;
            float padding0;
            float padding1;
            float padding2;
        };
        static_assert(sizeof(SSAOSetupConstants) == 16);

        struct SSAOSetupData
        {
            uint32_t viewId;
            float4 viewport;
            SSAOSetupConstants constants;
            rg::TextureHandle sceneDepth;
            rg::TextureHandle gbufferA;
        };

        struct SSAOHalfResConstants
        {
            float radius;
            float scaleToFullRes;
            uint32_t hzbMipCount;
            uint32_t padding0;
            float2 screenToHZBUVScale;
        };
        static_assert(sizeof(SSAOHalfResConstants) == 24);

        struct SSAOHalfResData
        {
            uint32_t viewId;
            float4 viewport;
            SSAOHalfResConstants constants;
            rg::TextureHandle setupTexture;
            rg::TextureHandle sceneHZB;
        };

        struct SSAOFullResConstants
        {
            float radius;
            float scaleToFullRes;
            uint32_t hzbMipCount;
            uint32_t padding0;
            float2 screenToHZBUVScale;
            float2 downsampledAOInvSize;
            float2 downsampledAOUVMin;
            float2 downsampledAOUVMax;
            float intensity;
            float power;
            float fadeDistance;
            float fadeRadius;
        };
        static_assert(sizeof(SSAOFullResConstants) == 64);

        struct SSAOFullResData
        {
            uint32_t viewId;
            float4 viewport;
            SSAOFullResConstants constants;
            rg::TextureHandle sceneDepth;
            rg::TextureHandle gbufferA;
            rg::TextureHandle sceneHZB;
            rg::TextureHandle setupTexture;
            rg::TextureHandle halfResAO;
        };

        struct AOPassSSAOStorage
        {
            std::shared_ptr<PassShader> setupShader;
            TRef<rhi::BindGroupLayout> setupLayout;
            TRef<rhi::Pipeline> setupPipeline;
            std::vector<BindGroupCache<2>> setupCaches;

            std::shared_ptr<PassShader> halfResShader;
            TRef<rhi::BindGroupLayout> halfResLayout;
            TRef<rhi::Pipeline> halfResPipeline;
            std::vector<BindGroupCache<3>> halfResCaches;

            std::shared_ptr<PassShader> fullResShader;
            TRef<rhi::BindGroupLayout> fullResLayout;
            TRef<rhi::Pipeline> fullResPipeline;
            std::vector<BindGroupCache<6>> fullResCaches;
        };

        template <rhi::TextureFormat ColorFormat>
        rhi::Pipeline* createSSAOPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout)
        {
            auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr);
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

            PipelineCache::GraphicsKey pipelineKey;
            pipelineKey.pipelineState = PipelineStatePresets::fullscreenTriangle();
            pipelineKey.pipelineState.shader = shaderVariant;
            pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
                rhi::TextureFormat::Undefined,
                ColorFormat
            >();
            pipelineKey.pipelineLayout = pipelineLayout;
            return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
        }

        // UE AORadiusInShader(PostProcessAmbientOcclusion.cpp:370-383), AmbientOcclusionRadiusInWS=false
        // 分支(view space 相对半径, /400cm 换算成米制是 /4m), 叠加对多级 mip 无条件生效的 /4 与
        // MipScale(1.7)^log2(scaleToFullRes) 调整
        float computeAORadiusInShader(float radiusMeters, float scaleToFullRes)
        {
            float f = std::log2(scaleToFullRes);
            return (radiusMeters / 4.0f) * std::pow(1.7f, f) / 4.0f;
        }
    }

    // SSAO 路径: Setup(半分辨率法线/深度) -> 半分辨率 AO -> 全分辨率 AO(读半分辨率结果做 upsample
    // 混合), 严格对照 UE PostProcessAmbientOcclusion.usf 在 AmbientOcclusionQuality=50 下的实际
    // 展开重写, 公式细节见 Shader/PostProcess/SSAOCommon.slang 顶部说明与
    // Document/SSAO_Rewrite_Plan.md。UE 的 AddAmbientOcclusionSmoothPass 只在没开 TAA/TSR 时才跑
    // (CompositionLighting.cpp:139 的 bNeedSmoothingPass 判断里有 !IsTemporalAccumulationBasedMethod(...)),
    // 开了 TAA 之后 UE 自己也不加这道 smooth, 完全指望 TAA 把 SSAO 每帧的旋转噪声磨掉——这里跟 UE
    // 保持一致, 不额外加 pass。
    static void addSSAOToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view,
        PostProcessState& postProcess
    )
    {
        uint32_t viewId = view.getViewId();
        float4 viewport = view.getViewport();
        uint2 extent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));
        uint2 halfExtent((extent.x + 1u) / 2u, (extent.y + 1u) / 2u);

        // UE 的 SSAO HZB 重映射始终按完整 View.ViewRect 计算(GetHZBParametersForAO 的
        // AOTechnique::SSAO 分支), 不区分半分辨率/全分辨率子 pass, 两个 pass 共用同一份
        const uint2 hzbBaseExtent = HZBPass::getBaseExtent(extent);
        const uint32_t hzbMipCount = HZBPass::getMipCount(hzbBaseExtent);
        const float2 screenToHZBUVScale(
            float(extent.x) / float(2u * hzbBaseExtent.x),
            float(extent.y) / float(2u * hzbBaseExtent.y));

        const AmbientOcclusionSettings& settings = postProcess.getPostProcessSettings().ambientOcclusion;

        const float4 halfViewport(0.0f, 0.0f, float(halfExtent.x), float(halfExtent.y));

        // Pass 1: Setup —— 半分辨率法线/深度, 供后两个 pass 当深度/法线源(UE MainSetupPS)
        rg::TextureHandle setupTexture;
        renderGraph.addGraphicsPass<SSAOSetupData>(
            "SSAOSetup"_hs,
            [&](rg::RenderGraph::Builder& builder, SSAOSetupData& data)
            {
                data.viewId = viewId;
                data.viewport = halfViewport;
                // UE ThresholdInverseValue = AmbientOcclusionMipThreshold(米制 1.0) *
                // (halfExtent.x / extent.x)(PostProcessAmbientOcclusion.cpp:576)
                float thresholdInverse = 1.0f * (float(halfExtent.x) / float(extent.x));
                data.constants = { thresholdInverse, 0.0f, 0.0f, 0.0f };

                auto& blackboard = builder.getBlackboard();
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));

                rhi::TextureDesc setupDesc;
                setupDesc.type = rhi::TextureType::Texture_2D;
                setupDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                setupDesc.extent = { halfExtent.x, halfExtent.y, 1 };
                setupDesc.mipCount = 1;
                setupDesc.layerCount = 1;
                setupDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                setupTexture = builder.createTexture("SSAOSetup"_hs(viewId), setupDesc);
                setupTexture = builder.addColorAttachment(setupTexture, rhi::AttachmentLoadOp::Dont_Care);
            },
            [&rendererContext](rg::ExecuteContext& context, SSAOSetupData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<AOPass, AOPassSSAOStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.setupShader)
                {
                    storage.setupShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "SSAOSetup",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(SSAOSetup, Vertex),
                            GET_PASS_SHADER_BYTECODE(SSAOSetup, Fragment)
                        )
                    );
                    storage.setupLayout = createPassBindGroupLayout(renderBackend, generated::kSSAOSetup_Bindings);
                    storage.setupPipeline = createSSAOPipeline<rhi::TextureFormat::RGBA16_SFLOAT>(
                        rendererContext, storage.setupShader->getVariant(), *storage.setupLayout);
                }

                if (storage.setupCaches.size() <= data.viewId)
                    storage.setupCaches.resize(data.viewId + 1u);
                auto& cache = storage.setupCaches[data.viewId];

                rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                if (cache.needUpdate(sceneDepth, gbufferA))
                {
                    cache.update(renderBackend, *storage.setupLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kSSAOSetup_Bindings);
                        writer.setTexture("SceneDepth", sceneDepth);
                        writer.setTexture("GBufferA", gbufferA);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                setViewportAndScissor(cmdList, data.viewport);
                cmdList.bindPipeline(storage.setupPipeline.get());
                cmdList.setBindGroup(
                    2, rhi::PipelineBindPoint::Graphics,
                    &storage.setupPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.setupPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.draw(3, 1, 0, 0);
            }
        );

        // Pass 2: 半分辨率 AO(UE MainPSandCS, USE_AO_SETUP_AS_INPUT=1)
        rg::TextureHandle halfResAO;
        renderGraph.addGraphicsPass<SSAOHalfResData>(
            "SSAOHalfRes"_hs,
            [&](rg::RenderGraph::Builder& builder, SSAOHalfResData& data)
            {
                data.viewId = viewId;
                data.viewport = halfViewport;
                data.constants = {
                    computeAORadiusInShader(settings.radius, 2.0f),
                    2.0f,
                    hzbMipCount,
                    0u,
                    screenToHZBUVScale
                };

                auto& blackboard = builder.getBlackboard();
                data.setupTexture = builder.sample(setupTexture);
                data.sceneHZB = builder.sample(blackboard.get<rg::TextureHandle>("SceneHZB"_hs(viewId)));

                rhi::TextureDesc aoDesc;
                aoDesc.type = rhi::TextureType::Texture_2D;
                aoDesc.format = rhi::TextureFormat::R8_UNORM;
                aoDesc.extent = { halfExtent.x, halfExtent.y, 1 };
                aoDesc.mipCount = 1;
                aoDesc.layerCount = 1;
                aoDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                halfResAO = builder.createTexture("SSAOHalfRes"_hs(viewId), aoDesc);
                halfResAO = builder.addColorAttachment(halfResAO, rhi::AttachmentLoadOp::Dont_Care);
            },
            [&rendererContext](rg::ExecuteContext& context, SSAOHalfResData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<AOPass, AOPassSSAOStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.halfResShader)
                {
                    storage.halfResShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "SSAOHalfRes",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(SSAOHalfRes, Vertex),
                            GET_PASS_SHADER_BYTECODE(SSAOHalfRes, Fragment)
                        )
                    );
                    storage.halfResLayout = createPassBindGroupLayout(renderBackend, generated::kSSAOHalfRes_Bindings);
                    storage.halfResPipeline = createSSAOPipeline<rhi::TextureFormat::R8_UNORM>(
                        rendererContext, storage.halfResShader->getVariant(), *storage.halfResLayout);
                }

                if (storage.halfResCaches.size() <= data.viewId)
                    storage.halfResCaches.resize(data.viewId + 1u);
                auto& cache = storage.halfResCaches[data.viewId];

                rhi::Sampler* pointClampSampler = renderBackend.getSamplerCache().getPointClampNoMip();
                rhi::TextureView* setupTextureView = context.getTexture(data.setupTexture);
                rhi::TextureView* sceneHZB = context.getTexture(data.sceneHZB);
                if (cache.needUpdate(setupTextureView, pointClampSampler, sceneHZB))
                {
                    cache.update(renderBackend, *storage.halfResLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kSSAOHalfRes_Bindings);
                        writer.setTexture("SSAOSetupTexture", setupTextureView);
                        writer.setSampler("PointClampSampler", pointClampSampler);
                        writer.setTexture("SceneHZB", sceneHZB);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                setViewportAndScissor(cmdList, data.viewport);
                cmdList.bindPipeline(storage.halfResPipeline.get());
                cmdList.setBindGroup(
                    2, rhi::PipelineBindPoint::Graphics,
                    &storage.halfResPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.halfResPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.draw(3, 1, 0, 0);
            }
        );

        // Pass 3: 全分辨率 AO + upsample 混合(UE MainPSandCS, USE_AO_SETUP_AS_INPUT=0, USE_UPSAMPLE=1)
        renderGraph.addGraphicsPass<SSAOFullResData>(
            "SSAOFullRes"_hs,
            [&](rg::RenderGraph::Builder& builder, SSAOFullResData& data)
            {
                data.viewId = viewId;
                data.viewport = viewport;

                float2 downsampledAOInvSize(1.0f / float(halfExtent.x), 1.0f / float(halfExtent.y));
                data.constants = {
                    computeAORadiusInShader(settings.radius, 1.0f),
                    1.0f,
                    hzbMipCount,
                    0u,
                    screenToHZBUVScale,
                    downsampledAOInvSize,
                    downsampledAOInvSize * 0.5f,
                    float2(1.0f, 1.0f) - downsampledAOInvSize * 0.5f,
                    settings.intensity,
                    settings.power,
                    settings.fadeDistance,
                    settings.fadeRadius
                };

                auto& blackboard = builder.getBlackboard();
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.sceneHZB = builder.sample(blackboard.get<rg::TextureHandle>("SceneHZB"_hs(viewId)));
                data.setupTexture = builder.sample(setupTexture);
                data.halfResAO = builder.sample(halfResAO);

                rhi::TextureDesc aoDesc;
                aoDesc.type = rhi::TextureType::Texture_2D;
                aoDesc.format = rhi::TextureFormat::R8_UNORM;
                aoDesc.extent = { extent.x, extent.y, 1 };
                aoDesc.mipCount = 1;
                aoDesc.layerCount = 1;
                aoDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto ao = builder.createTexture("AmbientOcclusionSSAO"_hs(viewId), aoDesc);
                ao = builder.addColorAttachment(ao, rhi::AttachmentLoadOp::Dont_Care);

                // 和 GTAO 路径最终写进 blackboard 的是同一个 key——IndirectLightingPass 不需要
                // 知道背后走的是哪条路径
                blackboard.put("AmbientOcclusion"_hs(viewId), ao);
            },
            [&rendererContext](rg::ExecuteContext& context, SSAOFullResData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<AOPass, AOPassSSAOStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.fullResShader)
                {
                    storage.fullResShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "SSAOFullRes",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(SSAOFullRes, Vertex),
                            GET_PASS_SHADER_BYTECODE(SSAOFullRes, Fragment)
                        )
                    );
                    storage.fullResLayout = createPassBindGroupLayout(renderBackend, generated::kSSAOFullRes_Bindings);
                    storage.fullResPipeline = createSSAOPipeline<rhi::TextureFormat::R8_UNORM>(
                        rendererContext, storage.fullResShader->getVariant(), *storage.fullResLayout);
                }

                if (storage.fullResCaches.size() <= data.viewId)
                    storage.fullResCaches.resize(data.viewId + 1u);
                auto& cache = storage.fullResCaches[data.viewId];

                rhi::Sampler* pointClampSampler = renderBackend.getSamplerCache().getPointClampNoMip();
                rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                rhi::TextureView* sceneHZB = context.getTexture(data.sceneHZB);
                rhi::TextureView* setupTextureView = context.getTexture(data.setupTexture);
                rhi::TextureView* halfResAOView = context.getTexture(data.halfResAO);
                if (cache.needUpdate(sceneDepth, gbufferA, pointClampSampler, sceneHZB, setupTextureView, halfResAOView))
                {
                    cache.update(renderBackend, *storage.fullResLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kSSAOFullRes_Bindings);
                        writer.setTexture("SceneDepth", sceneDepth);
                        writer.setTexture("GBufferA", gbufferA);
                        writer.setSampler("PointClampSampler", pointClampSampler);
                        writer.setTexture("SceneHZB", sceneHZB);
                        writer.setTexture("SSAOSetupTexture", setupTextureView);
                        writer.setTexture("SSAOHalfResAO", halfResAOView);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                setViewportAndScissor(cmdList, data.viewport);
                cmdList.bindPipeline(storage.fullResPipeline.get());
                cmdList.setBindGroup(
                    2, rhi::PipelineBindPoint::Graphics,
                    &storage.fullResPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.fullResPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.draw(3, 1, 0, 0);
            }
        );
    }

    // GTAO 路径: Generate -> SpatialFilter -> TemporalFilter, 见 AOGenerateGTAO.slang 等三个
    // *GTAO.slang 文件顶部的说明。
    static void addGTAOToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view,
        PostProcessState& postProcess
    )
    {
        uint32_t viewId = view.getViewId();
        float4 viewport = view.getViewport();
        uint2 extent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));
        uint2 dispatchGroupCount = dispatchGroupCountFor(extent);
        const uint2 hzbBaseExtent = HZBPass::getBaseExtent(extent);
        const uint32_t hzbMipCount = HZBPass::getMipCount(hzbBaseExtent);
        const float2 screenToHZBUVScale(
            float(extent.x) / float(2u * hzbBaseExtent.x),
            float(extent.y) / float(2u * hzbBaseExtent.y));

        // 按当前 viewport 尺寸懒创建/重建两张常驻 ping-pong AO history 纹理——必须在 import 之前
        // 调, 语义跟 TAAPass::addToRenderGraph 里 postProcess.ensureTAAHistory() 的注意事项完全一样
        postProcess.ensureAOHistory(extent);

        const AmbientOcclusionSettings& settings = postProcess.getPostProcessSettings().ambientOcclusion;

        rg::TextureHandle historyReadHandle = renderGraph.importTexture(
            "AOHistoryRead"_hs(viewId),
            &postProcess.getAOHistoryReadView(),
            rhi::ResourceState::Shader_Sampled
        );
        rg::TextureHandle historyWriteHandle = renderGraph.importTexture(
            "AOHistoryWrite"_hs(viewId),
            &postProcess.getAOHistoryWriteView(),
            rhi::ResourceState::Shader_Sampled
        );

        rg::TextureHandle aoRaw;
        renderGraph.addComputePass<AOGenerateGTAOData>(
            "AOGenerateGTAO"_hs,
            [&](rg::RenderGraph::Builder& builder, AOGenerateGTAOData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = dispatchGroupCount;
                data.constants = {
                    settings.radius,
                    settings.fadeDistance,
                    settings.fadeRadius,
                    static_cast<uint32_t>(settings.quality),
                    hzbMipCount,
                    0u,
                    screenToHZBUVScale
                };

                auto& blackboard = builder.getBlackboard();
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.sceneHZB = builder.sample(blackboard.get<rg::TextureHandle>("SceneHZB"_hs(viewId)));

                // storage() 返回的是这次写入之后的新版本 handle——AOSpatialFilterPass 在下一个
                // pass 里要 sample 这张纹理, 必须用这个新 handle, 不能继续用 createTexture() 刚
                // 拿到的、还没被任何 pass 写过的旧 handle(RenderGraph 只允许 import 进来的资源
                // 未写先读, "AORaw" 是本帧新建的 transient 资源, 用旧 handle 读会直接触发
                // access() 里的断言), 见 BloomPass.cpp 里 `pyramid = builder.syncTextures(...)`
                // 的同一个道理
                aoRaw = builder.createTexture("AORaw"_hs(viewId), makeAOTextureDesc(extent));
                aoRaw = builder.storage(aoRaw, rg::AccessType::Write_Only);
                data.aoRaw = aoRaw;
            },
            [&rendererContext](rg::ExecuteContext& context, AOGenerateGTAOData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<AOPass, AOPassGTAOStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.generateShader)
                {
                    storage.generateShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "AOGenerateGTAO",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(AOGenerateGTAO, Compute))
                    );
                    storage.generateLayout = createPassBindGroupLayout(renderBackend, generated::kAOGenerateGTAO_Bindings);
                    storage.generatePipeline = createComputePipeline(
                        rendererContext,
                        storage.generateShader->getVariant(),
                        *storage.generateLayout
                    );
                }

                if (storage.generateCaches.size() <= data.viewId)
                    storage.generateCaches.resize(data.viewId + 1u);
                auto& cache = storage.generateCaches[data.viewId];

                rhi::Sampler* hzbPointClampSampler = renderBackend.getSamplerCache().getPointClampNoMip();
                rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                rhi::TextureView* sceneHZB = context.getTexture(data.sceneHZB);
                rhi::TextureView* aoRawView = context.getTexture(data.aoRaw);
                if (cache.needUpdate(hzbPointClampSampler, sceneDepth, sceneHZB, aoRawView))
                {
                    cache.update(renderBackend, *storage.generateLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kAOGenerateGTAO_Bindings);
                        writer.setSampler("HZBPointClampSampler", hzbPointClampSampler);
                        writer.setTexture("SceneDepth", sceneDepth);
                        writer.setTexture("SceneHZB", sceneHZB);
                        writer.setTexture("AORawImage", aoRawView);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.generatePipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Compute,
                    &storage.generatePipeline->getLayout(),
                    cache.getBindGroup(),
                    {}
                );
                cmdList.setImmediateData(
                    &storage.generatePipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants))
                );
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            }
        );

        rg::TextureHandle aoFiltered;
        renderGraph.addComputePass<AOSpatialFilterGTAOData>(
            "AOSpatialFilterGTAO"_hs,
            [&](rg::RenderGraph::Builder& builder, AOSpatialFilterGTAOData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = dispatchGroupCount;
                data.intensity = settings.intensity;
                data.power = settings.power;

                auto& blackboard = builder.getBlackboard();
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.aoRaw = builder.sample(aoRaw);

                // 同上: AOTemporalFilterPass 要 sample "AOFiltered", 这里也必须把 storage()
                // 返回的写后新 handle 传回外层的 aoFiltered 变量
                aoFiltered = builder.createTexture("AOFiltered"_hs(viewId), makeAOTextureDesc(extent));
                aoFiltered = builder.storage(aoFiltered, rg::AccessType::Write_Only);
                data.aoFiltered = aoFiltered;
            },
            [&rendererContext](rg::ExecuteContext& context, AOSpatialFilterGTAOData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<AOPass, AOPassGTAOStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.spatialShader)
                {
                    storage.spatialShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "AOSpatialFilterGTAO",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(AOSpatialFilterGTAO, Compute))
                    );
                    storage.spatialLayout = createPassBindGroupLayout(renderBackend, generated::kAOSpatialFilterGTAO_Bindings);
                    storage.spatialPipeline = createComputePipeline(
                        rendererContext,
                        storage.spatialShader->getVariant(),
                        *storage.spatialLayout
                    );
                }

                if (storage.spatialCaches.size() <= data.viewId)
                    storage.spatialCaches.resize(data.viewId + 1u);
                auto& cache = storage.spatialCaches[data.viewId];

                rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                rhi::TextureView* aoRawView = context.getTexture(data.aoRaw);
                rhi::TextureView* aoFilteredView = context.getTexture(data.aoFiltered);
                if (cache.needUpdate(sceneDepth, aoRawView, aoFilteredView))
                {
                    cache.update(renderBackend, *storage.spatialLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kAOSpatialFilterGTAO_Bindings);
                        writer.setTexture("SceneDepth", sceneDepth);
                        writer.setTexture("AORawImage", aoRawView);
                        writer.setTexture("AOFilteredImage", aoFilteredView);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.spatialPipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Compute,
                    &storage.spatialPipeline->getLayout(),
                    cache.getBindGroup(),
                    {}
                );
                const float2 filterConstants(data.intensity, data.power);
                cmdList.setImmediateData(
                    &storage.spatialPipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(&filterConstants),
                        sizeof(filterConstants))
                );
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            }
        );

        bool historyValid = postProcess.isAOHistoryValid();
        renderGraph.addComputePass<AOTemporalFilterGTAOData>(
            "AOTemporalFilterGTAO"_hs,
            [&](rg::RenderGraph::Builder& builder, AOTemporalFilterGTAOData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = dispatchGroupCount;
                // 对应 UE AmbientOcclusionTemporalBlendWeight 默认值——低权重换更干净的收敛,
                // 代价是收敛更慢, 只有 GTAO 用到, 不是用户可调参数
                data.constants = { historyValid ? 1u : 0u, 0.1f };

                auto& blackboard = builder.getBlackboard();
                data.velocity = builder.sample(blackboard.get<rg::TextureHandle>("Velocity"_hs(viewId)));
                data.aoFiltered = builder.sample(aoFiltered);
                data.historyRead = builder.sample(historyReadHandle);
                data.historyWrite = builder.storage(historyWriteHandle, rg::AccessType::Write_Only);

                // 这一帧收敛好的 AO 就是本帧发布进 blackboard 的 "AmbientOcclusion"——
                // IndirectLightingPass 直接采样, 不需要知道背后这三个 pass 的存在
                blackboard.put("AmbientOcclusion"_hs(viewId), data.historyWrite);
            },
            [&rendererContext](rg::ExecuteContext& context, AOTemporalFilterGTAOData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<AOPass, AOPassGTAOStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.temporalShader)
                {
                    storage.temporalShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "AOTemporalFilterGTAO",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(AOTemporalFilterGTAO, Compute))
                    );
                    storage.temporalLayout = createPassBindGroupLayout(renderBackend, generated::kAOTemporalFilterGTAO_Bindings);
                    storage.temporalPipeline = createComputePipeline(
                        rendererContext,
                        storage.temporalShader->getVariant(),
                        *storage.temporalLayout
                    );
                }

                if (storage.temporalCaches.size() <= data.viewId)
                    storage.temporalCaches.resize(data.viewId + 1u);
                auto& cache = storage.temporalCaches[data.viewId];

                rhi::Sampler* pointClampNoMipSampler = renderBackend.getSamplerCache().getPointClampNoMip();
                rhi::TextureView* velocity = context.getTexture(data.velocity);
                rhi::TextureView* aoFilteredView = context.getTexture(data.aoFiltered);
                rhi::TextureView* historyRead = context.getTexture(data.historyRead);
                rhi::TextureView* historyWrite = context.getTexture(data.historyWrite);
                if (cache.needUpdate(pointClampNoMipSampler, velocity, aoFilteredView, historyRead, historyWrite))
                {
                    cache.update(renderBackend, *storage.temporalLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kAOTemporalFilterGTAO_Bindings);
                        writer.setSampler("PointClampSampler", pointClampNoMipSampler);
                        writer.setTexture("Velocity", velocity);
                        writer.setTexture("AOFilteredImage", aoFilteredView);
                        writer.setTexture("AOHistoryRead", historyRead);
                        writer.setTexture("AOHistoryWrite", historyWrite);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.temporalPipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Compute,
                    &storage.temporalPipeline->getLayout(),
                    cache.getBindGroup(),
                    {}
                );
                cmdList.setImmediateData(
                    &storage.temporalPipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants))
                );
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            }
        );

        postProcess.swapAOHistory();
    }

    void AOPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view,
        PostProcessState& postProcess
    )
    {
        const AmbientOcclusionSettings& settings = postProcess.getPostProcessSettings().ambientOcclusion;
        switch (settings.method)
        {
        case AmbientOcclusionMethod::SSAO:
            addSSAOToRenderGraph(renderGraph, rendererContext, view, postProcess);
            break;
        case AmbientOcclusionMethod::GTAO:
        default:
            addGTAOToRenderGraph(renderGraph, rendererContext, view, postProcess);
            break;
        }
    }
}
