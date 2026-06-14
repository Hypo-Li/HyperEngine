#include "DirectLightingPass.h"
#include "RenderPassUtils.h"

namespace hyp
{
    struct DirectLightingData
    {
        SceneRendererContext* rendererContext;
        const SceneProxy* scene;
        uint32_t viewId;
        float4 viewport;
        rg::TextureHandle sceneColor;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle sceneDepthD;
    };

    struct DirectLightingStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<vk::Pipeline> pipeline;
        std::vector<DescriptorCache<4>> perViewCaches;
    };

    void DirectLightingPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view
    )
    {
        renderGraph.addGraphicsPass<DirectLightingData>(
            "DirectLighting"_hs,
            [&](rg::RenderGraph::Builder& builder, DirectLightingData& data)
            {
                data.rendererContext = &rendererContext;
                data.scene = &scene;
                uint32_t viewId = view.getViewId();
                data.viewId = viewId;
                data.viewport = view.getViewport();
                auto& blackboard = builder.getBlackboard();
                rg::TextureHandle sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(viewId));
                rg::TextureHandle gbufferA = blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId));
                rg::TextureHandle gbufferB = blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId));
                rg::TextureHandle gbufferC = blackboard.get<rg::TextureHandle>("GBufferC"_hs(viewId));
                rg::TextureHandle sceneDepthD = blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId));
                data.sceneColor = builder.addColorAttachment(sceneColor, VK_ATTACHMENT_LOAD_OP_LOAD);
                data.gbufferA = builder.sample(gbufferA);
                data.gbufferB = builder.sample(gbufferB);
                data.gbufferC = builder.sample(gbufferC);
                data.sceneDepthD = builder.sample(sceneDepthD);
                blackboard.put("SceneColor"_hs(viewId), data.sceneColor);
            },
            [](rg::ExecuteContext& context, DirectLightingData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<DirectLightingPass, DirectLightingStorage>();

                auto& renderBackend = context.getRenderBackend();

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& descriptorCache = storage.perViewCaches[data.viewId];

                VkImageView gbufferA = context.getTexture(data.gbufferA)->getHandle();
                VkImageView gbufferB = context.getTexture(data.gbufferB)->getHandle();
                VkImageView gbufferC = context.getTexture(data.gbufferC)->getHandle();
                VkImageView sceneDepthD = context.getTexture(data.sceneDepthD)->getHandle();
                if (descriptorCache.needUpdate(gbufferA, gbufferB, gbufferC, sceneDepthD))
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                        { 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                        { 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                        { 2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
                        { 3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }
                    };
                    descriptorCache.update(
                        renderBackend,
                        bindings,
                        [&](vk::DescriptorWriter& writer)
                        {
                            writer.writeImage(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, gbufferA, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, gbufferB, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, gbufferC, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, sceneDepthD, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                        }
                    );
                }

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "DirectLighting",
                        SHADER_DIR "/Material/DirectLighting.slang"
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createPipeline(
                        *data.rendererContext,
                        storage.shader->getOrCreateVariant({}),
                        descriptorCache.getDescriptorSet()->getLayout()
                    );
                }
                auto& cmd = context.getCommandBuffer();

                setViewportAndScissor(cmd, data.viewport);

                cmd.bindPipeline(storage.pipeline->getBindPoint(), storage.pipeline->getHandle());
                VkDescriptorSet descriptorSets[] = {
                    descriptorCache.getDescriptorSet()->getHandle()
                };
                cmd.bindDescriptorSets(
                    storage.pipeline->getBindPoint(),
                    storage.pipeline->getLayout().getHandle(),
                    2,
                    descriptorSets,
                    {}
                );

                auto directionalLightProxies = data.scene->getDirectionalLightProxies();
                for (auto directionalLightProxy : directionalLightProxies)
                {
                    uint32_t lightIndex = directionalLightProxy->getLightIndex();
                    cmd.pushConstants(
                        storage.pipeline->getLayout().getHandle(),
                        VK_SHADER_STAGE_ALL,
                        0,
                        sizeof(uint32_t),
                        &lightIndex
                    );
                    cmd.draw(3, 1, 0, 0);
                }
            }
        );
    }

    vk::Pipeline* DirectLightingPass::createPipeline(
        SceneRendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const vk::DescriptorSetLayout& descriptorSetLayout
    )
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, descriptorSetLayout.getHandle());
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.topology = static_cast<uint32_t>(Topology::Triangle_List);
        pipelineKey.pipelineState.samples = static_cast<uint32_t>(SampleCount::Count_1);
        pipelineKey.pipelineState.polygonMode = static_cast<uint32_t>(PolygonMode::Fill);
        pipelineKey.pipelineState.cullMode = static_cast<uint32_t>(CullMode::Back);
        pipelineKey.pipelineState.frontFace = static_cast<uint32_t>(FrontFace::Counter_Clockwise);
        pipelineKey.pipelineState.depthTestEnable = 0;
        pipelineKey.pipelineState.depthWriteEnable = 0;
        pipelineKey.pipelineState.depthCompareOp = static_cast<uint32_t>(CompareOp::Greater_Or_Equal);
        pipelineKey.pipelineState.stencilTestEnable = 0;
        pipelineKey.pipelineState.stencilState = StencilState::get();
        pipelineKey.pipelineState.colorBlendState = ColorBlendState::get<
            AttachmentBlendState{ VK_TRUE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, vk::kColorComponentAll }
        >();
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_R16G16B16A16_SFLOAT
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

}
