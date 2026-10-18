#include "TAAPass.h"
#include "RenderPassUtils.h"

#include <TAA.generated.h>

namespace hyp
{
    struct TAAData
    {
        uint32_t viewId;
        float4 viewport;
        uint32_t historyValid;
        rg::TextureHandle sceneColor;
        rg::TextureHandle velocity;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle sceneStencil;
        rg::TextureHandle historyRead;
        rg::TextureHandle historyWrite;
    };

    struct TAAConstants
    {
        uint32_t historyValid;
    };

    struct TAAStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<6>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void TAAPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view,
        PostProcessState& postProcess
    )
    {
        uint32_t viewId = view.getViewId();
        float4 viewport = view.getViewport();
        uint2 extent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));

        // 按当前 viewport 尺寸懒创建/重建两张常驻 ping-pong history 纹理——必须在 import 之前调,
        // 不然尺寸变化那一帧 import 到的还是旧尺寸的纹理
        postProcess.ensureTAAHistory(extent);

        rg::TextureHandle historyReadHandle = renderGraph.importTexture(
            "TAAHistoryRead"_hs(viewId),
            &postProcess.getTAAHistoryReadView(),
            rhi::ResourceState::Shader_Sampled
        );
        rg::TextureHandle historyWriteHandle = renderGraph.importTexture(
            "TAAHistoryWrite"_hs(viewId),
            &postProcess.getTAAHistoryWriteView(),
            rhi::ResourceState::Shader_Sampled
        );

        renderGraph.addGraphicsPass<TAAData>(
            "TAA"_hs,
            [&](rg::RenderGraph::Builder& builder, TAAData& data)
            {
                data.viewId = viewId;
                data.viewport = viewport;
                data.historyValid = postProcess.isTAAHistoryValid() ? 1u : 0u;

                rg::Blackboard& blackboard = builder.getBlackboard();

                auto sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(viewId));
                data.sceneColor = builder.sample(sceneColor);

                auto velocity = blackboard.get<rg::TextureHandle>("Velocity"_hs(viewId));
                data.velocity = builder.sample(velocity);

                auto sceneDepth = blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId));
                data.sceneDepth = builder.sample(sceneDepth);

                auto sceneStencil = blackboard.get<rg::TextureHandle>("SceneStencil"_hs(viewId));
                data.sceneStencil = builder.sample(sceneStencil);

                data.historyRead = builder.sample(historyReadHandle);

                // resolve 的输出直接就是这一帧的 history write 纹理, 不需要额外一张 transient
                // 纹理再拷贝一遍——这份输出同时覆盖 blackboard 的 "SceneColor", 后面 ColorGradingPass
                // 等 pass 不需要知道 TAA 存在, 直接采样这张纹理
                data.historyWrite = builder.addColorAttachment(historyWriteHandle, rhi::AttachmentLoadOp::Dont_Care);
                blackboard.put("SceneColor"_hs(viewId), data.historyWrite);
            },
            [&rendererContext](rg::ExecuteContext& context, TAAData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<TAAPass, TAAStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "TAA",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(TAA, Vertex),
                            GET_PASS_SHADER_BYTECODE(TAA, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kTAA_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                rhi::TextureView* sceneColor = context.getTexture(data.sceneColor);
                rhi::TextureView* velocity = context.getTexture(data.velocity);
                rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                rhi::TextureView* sceneStencil = context.getTexture(data.sceneStencil);
                rhi::TextureView* history = context.getTexture(data.historyRead);

                if (bindGroupCache.needUpdate(linearClampNoMipSampler, sceneColor, velocity, sceneDepth, sceneStencil, history))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kTAA_Bindings);
                            writer.setSampler("LinearClampSampler", linearClampNoMipSampler);
                            writer.setTexture("SceneColor", sceneColor);
                            writer.setTexture("Velocity", velocity);
                            writer.setTexture("SceneDepth", sceneDepth);
                            writer.setTexture("SceneStencil", sceneStencil);
                            writer.setTexture("History", history);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout
                    );
                }

                TAAConstants constants{ data.historyValid };

                auto& cmdList = context.getCommandList();
                setViewportAndScissor(cmdList, data.viewport);
                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&constants), sizeof(constants))
                );
                cmdList.draw(3, 1, 0, 0);

            }
        );

        postProcess.swapTAAHistory();
    }

    rhi::Pipeline* TAAPass::createPipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& bindGroupLayout
    )
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::fullscreenTriangle();
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            rhi::TextureFormat::Undefined,
            rhi::TextureFormat::RGBA16_SFLOAT
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
