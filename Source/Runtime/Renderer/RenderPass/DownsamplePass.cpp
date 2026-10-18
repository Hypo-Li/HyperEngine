#include "DownsamplePass.h"
#include "RenderPassUtils.h"

#include <Downsample.generated.h>

namespace hyp
{
    namespace
    {
        struct DownsampleConstants
        {
            uint2 inputResolution;
            uint2 outputResolution;
        };
        static_assert(sizeof(DownsampleConstants) == 16);

        struct DownsampleData
        {
            uint32_t viewId;
            float4 viewport;
            DownsampleConstants constants;
            rg::TextureHandle sceneColor;
            rg::TextureHandle sceneColorHalfRes;
        };

        struct DownsamplePassStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> layout;
            TRef<rhi::Pipeline> pipeline;
            std::vector<BindGroupCache<1>> caches;
        };

        rhi::Pipeline* createDownsamplePipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout)
        {
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
            rhi::PipelineLayout* pipelineLayout = createRenderPassPipelineLayout(
                rendererContext,
                &bindGroupLayout,
                nullptr);

            PipelineCache::GraphicsKey pipelineKey;
            pipelineKey.pipelineState = PipelineStatePresets::fullscreenTriangle();
            pipelineKey.pipelineState.shader = shaderVariant;
            pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
                rhi::TextureFormat::Undefined,
                rhi::TextureFormat::RGBA16_SFLOAT>();
            pipelineKey.pipelineLayout = pipelineLayout;
            return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
        }
    }

    void DownsamplePass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view)
    {
        renderGraph.addGraphicsPass<DownsampleData>(
            "Downsample"_hs(view.getViewId()),
            [&](rg::RenderGraph::Builder& builder, DownsampleData& data)
            {
                const uint32_t viewId = view.getViewId();
                const float4 viewport = view.getViewport();
                const uint2 resolution(
                    static_cast<uint32_t>(viewport.z),
                    static_cast<uint32_t>(viewport.w));
                const uint2 halfResolution(
                    (resolution.x + 1u) / 2u,
                    (resolution.y + 1u) / 2u);

                data.viewId = viewId;
                data.viewport = float4(0.0f, 0.0f, halfResolution.x, halfResolution.y);
                data.constants = { resolution, halfResolution };
                rg::TextureHandle sceneColor = builder.getBlackboard().get<rg::TextureHandle>(
                    "SceneColor"_hs(viewId));
                data.sceneColor = builder.sample(sceneColor);

                rg::TextureHandle sceneColorHalfRes = builder.getBlackboard().get<rg::TextureHandle>(
                    "SceneColorHalfRes"_hs(viewId));
                if (!sceneColorHalfRes.isValid())
                {
                    rhi::TextureDesc halfResDesc;
                    halfResDesc.type = rhi::TextureType::Texture_2D;
                    halfResDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                    halfResDesc.extent = { halfResolution.x, halfResolution.y, 1u };
                    halfResDesc.mipCount = 1u;
                    halfResDesc.layerCount = 1u;
                    halfResDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                    sceneColorHalfRes = builder.createTexture(
                        "SceneColorHalfRes"_hs(viewId),
                        halfResDesc);
                }
                data.sceneColorHalfRes = builder.addColorAttachment(
                    sceneColorHalfRes,
                    rhi::AttachmentLoadOp::Dont_Care);
                builder.getBlackboard().put(
                    "SceneColorHalfRes"_hs(viewId),
                    data.sceneColorHalfRes);
            },
            [&rendererContext](rg::ExecuteContext& context, DownsampleData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<
                    DownsamplePass, DownsamplePassStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "Downsample",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(Downsample, Vertex),
                            GET_PASS_SHADER_BYTECODE(Downsample, Fragment)));
                    storage.layout = createPassBindGroupLayout(
                        renderBackend,
                        generated::kDownsample_Bindings);
                    storage.pipeline = createDownsamplePipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.layout);
                }

                if (storage.caches.size() <= data.viewId)
                    storage.caches.resize(data.viewId + 1u);
                auto& cache = storage.caches[data.viewId];

                rhi::TextureView* sceneColor = context.getTexture(data.sceneColor);
                if (cache.needUpdate(sceneColor))
                {
                    cache.update(renderBackend, *storage.layout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kDownsample_Bindings);
                        writer.setTexture("SceneColor", sceneColor);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                setViewportAndScissor(cmdList, data.viewport);
                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    cache.getBindGroup(),
                    {});
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(&data.constants),
                        sizeof(data.constants)));
                cmdList.draw(3u, 1u, 0u, 0u);
            }
        );
    }
}
