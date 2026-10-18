#include "PresentPass.h"
#include "RenderPassUtils.h"

#include <CopyTexture.generated.h>

#include <cassert>

namespace hyp
{
    struct PresentData
    {
        uint32_t viewId;
        rg::TextureHandle sceneColor;
    };

    struct PresentStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<1>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void PresentPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view
    )
    {
        renderGraph.addPresentPass<PresentData>(
            "Present"_hs,
            [&](rg::RenderGraph::Builder& builder, PresentData& data)
            {
                data.viewId = view.getViewId();
                auto& blackboard = builder.getBlackboard();
                rg::TextureHandle sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(data.viewId));
                data.sceneColor = builder.sample(sceneColor);
            },
            [&rendererContext](rg::ExecuteContext& context, PresentData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<PresentPass, PresentStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

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

                const rhi::TextureView* sceneColor = context.getTexture(data.sceneColor);

                if (bindGroupCache.needUpdate(sceneColor))
                {
                    if (!storage.bindGroupLayout)
                    {
                        storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kCopyTexture_Bindings);
                    }

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

                if (!storage.pipeline)
                {
                    storage.pipeline = createPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout,
                        context.getSwapchain().getSurfaceTextureFormat()
                    );
                }

                auto& cmdList = context.getCommandList();
                uint2 swapchainExtent = context.getSwapchain().getExtent();
                setViewportAndScissor(cmdList, float4(0.0f, 0.0f, static_cast<float>(swapchainExtent.x), static_cast<float>(swapchainExtent.y)));

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    0,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );

                cmdList.draw(3, 1, 0, 0);
            }
        );
    }

    rhi::Pipeline* PresentPass::createPipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& bindGroupLayout,
        rhi::TextureFormat swapchainFormat
    )
    {
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
        // 纯拷贝 pass, 不需要引擎全局的 material/view 绑定表, 只用 set0 的独立 layout
        PipelineCache::PipelineLayoutKey layoutKey{};
        layoutKey.bindGroupLayouts[0] = &bindGroupLayout;
        TRef<rhi::PipelineLayout> pipelineLayout = pipelineCache.getOrCreateLayout(layoutKey);

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::fullscreenTriangle();
        pipelineKey.pipelineState.shader = shaderVariant;

        AttachmentFormats* attachmentFormats = nullptr;
        switch (swapchainFormat)
        {
        case rhi::TextureFormat::BGRA8_UNORM: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::BGRA8_UNORM>(); break;
        case rhi::TextureFormat::RGBA8_UNORM: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA8_UNORM>(); break;
        case rhi::TextureFormat::BGRA8_SRGB: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::BGRA8_SRGB>(); break;
        case rhi::TextureFormat::RGBA8_SRGB: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA8_SRGB>(); break;
        case rhi::TextureFormat::A2BGR10_UNORM_PACK32: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::A2BGR10_UNORM_PACK32>(); break;
        default: assert(false && "Unsupported swapchain format for PresentPass");
        }

        pipelineKey.pipelineState.attachmentFormats = attachmentFormats;
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
