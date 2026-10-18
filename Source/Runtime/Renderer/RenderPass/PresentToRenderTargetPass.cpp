#include "PresentToRenderTargetPass.h"
#include "RenderPassUtils.h"

#include <CopyTexture.generated.h>

#include <cassert>
#include <unordered_map>

namespace hyp
{
    struct PresentToRTData
    {
        uint32_t viewId;
        rg::TextureHandle sceneColor;
        rg::TextureHandle dst;
        float4 viewport;
    };

    struct PresentToRTStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<1>> perViewCaches;
        std::unordered_map<uint32_t, TRef<rhi::Pipeline>> pipelines;
    };

    void PresentToRenderTargetPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view
    )
    {
        renderGraph.addGraphicsPass<PresentToRTData>(
            "PresentToRenderTarget"_hs,
            [&](rg::RenderGraph::Builder& builder, PresentToRTData& data)
            {
                builder.markSideEffect();

                data.viewId = view.getViewId();
                data.viewport = view.getViewport();

                auto& blackboard = builder.getBlackboard();
                auto sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(data.viewId));
                data.sceneColor = builder.sample(sceneColor);

                auto dst = blackboard.get<rg::TextureHandle>(view.getFinalRTName());
                data.dst = builder.addColorAttachment(dst, rhi::AttachmentLoadOp::Clear);
            },
            [&rendererContext](rg::ExecuteContext& context, PresentToRTData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<
                    PresentToRenderTargetPass,
                    PresentToRTStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "CopyTexture",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(CopyTexture, Vertex),
                            GET_PASS_SHADER_BYTECODE(CopyTexture, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(
                        renderBackend,
                        generated::kCopyTexture_Bindings);
                }

                const rhi::TextureView* sceneColor = context.getTexture(data.sceneColor);
                const rhi::TextureView* dst = context.getTexture(data.dst);
                const rhi::TextureFormat dstFormat = dst->getTexture().getDesc().format;

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];
                if (bindGroupCache.needUpdate(sceneColor))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kCopyTexture_Bindings);
                            writer.setTexture("SourceTexture", sceneColor);
                            writer.apply(bindGroup);
                        }
                    );
                }

                TRef<rhi::Pipeline>& pipeline =
                    storage.pipelines[static_cast<uint32_t>(dstFormat)];
                if (!pipeline)
                {
                    pipeline = createPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout,
                        dstFormat);
                }

                auto& cmdList = context.getCommandList();
                setViewportAndScissor(cmdList, data.viewport);
                cmdList.bindPipeline(pipeline.get());
                cmdList.setBindGroup(
                    0,
                    rhi::PipelineBindPoint::Graphics,
                    &pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.draw(3, 1, 0, 0);

                // 这个 pass 的 pipeline layout 只有 1 个槽位(自己这份拷贝纹理专用的
                // BindGroupLayout), group 0 不是"全局数据"那份——用完必须换回去, 否则
                // 这一帧后面任何假设 group 0 已经是全局绑定的 pass(比如 Editor 自己在
                // enqueueLambda 里追加的 VisualizationPass)会绑到这个残留状态上
                auto& globalBindings = renderBackend.getGlobalBindings();
                cmdList.setBindGroup(
                    0,
                    rhi::PipelineBindPoint::Graphics,
                    &globalBindings.getPipelineLayout(),
                    &globalBindings.getGlobalDataBindGroup(),
                    {}
                );
            }
        );
    }

    rhi::Pipeline* PresentToRenderTargetPass::createPipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& bindGroupLayout,
        rhi::TextureFormat dstFormat
    )
    {
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
        PipelineCache::PipelineLayoutKey layoutKey{};
        layoutKey.bindGroupLayouts[0] = &bindGroupLayout;
        TRef<rhi::PipelineLayout> pipelineLayout = pipelineCache.getOrCreateLayout(layoutKey);

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::fullscreenTriangle();
        pipelineKey.pipelineState.shader = shaderVariant;

        AttachmentFormats* attachmentFormats = nullptr;
        switch (dstFormat)
        {
        case rhi::TextureFormat::BGRA8_UNORM: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::BGRA8_UNORM>(); break;
        case rhi::TextureFormat::RGBA8_UNORM: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA8_UNORM>(); break;
        case rhi::TextureFormat::BGRA8_SRGB: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::BGRA8_SRGB>(); break;
        case rhi::TextureFormat::RGBA8_SRGB: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA8_SRGB>(); break;
        case rhi::TextureFormat::A2BGR10_UNORM_PACK32: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::A2BGR10_UNORM_PACK32>(); break;
        case rhi::TextureFormat::RGBA16_SFLOAT: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA16_SFLOAT>(); break;
        default: assert(false && "Unsupported destination format for PresentToRenderTargetPass");
        }

        pipelineKey.pipelineState.attachmentFormats = attachmentFormats;
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
