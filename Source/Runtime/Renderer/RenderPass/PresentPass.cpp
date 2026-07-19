#include "PresentPass.h"
#include "RenderPassUtils.h"

#include <Present.generated.h>

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
                        "Present",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(Present, Vertex),
                            GET_PASS_SHADER_BYTECODE(Present, Fragment)
                        )
                    );
                }

                const rhi::TextureView* sceneColor = context.getTexture(data.sceneColor);

                if (bindGroupCache.needUpdate(sceneColor))
                {
                    if (!storage.bindGroupLayout)
                    {
                        // kPresent_Bindings 是 CompilePassShader.cmake 从 Present.slang 的
                        // [[vk::binding(0, 2)]] Texture2D<float4> SceneColor 反射生成的,
                        // 不需要在这里再手写一遍 entries[]。
                        storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kPresent_Bindings);
                    }

                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kPresent_Bindings);
                            writer.setTexture("SceneColor", sceneColor);
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
                        context.getSwapchain().getSwapchainTextureFormat()
                    );
                }

                auto& cmd = context.getCommandList();
                uint2 swapchainExtent = context.getSwapchain().getExtent();
                setViewportAndScissor(cmd, float4(0.0f, 0.0f, static_cast<float>(swapchainExtent.x), static_cast<float>(swapchainExtent.y)));

                cmd.bindPipeline(storage.pipeline.get());
                cmd.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );

                cmd.draw(3, 1, 0, 0);
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
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

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
