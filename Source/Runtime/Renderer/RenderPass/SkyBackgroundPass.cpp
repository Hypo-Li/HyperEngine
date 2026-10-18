#include "SkyBackgroundPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/MaterialProxy.h>
#include <RenderBase/RenderTagRegistry.h>

namespace hyp
{
    namespace
    {
        struct SkyBackgroundPushConstants
        {
            float3 mainLight0Direction;
            float intensity;
            float3 mainLight1Direction;
            int32_t captureFace;
            float3 mainLight0Intensity;
            float captureFaceSize;
            float3 mainLight1Intensity;
            float padding;
        };
        static_assert(sizeof(SkyBackgroundPushConstants) == 64);
        static_assert(sizeof(SkyBackgroundPushConstants) <= rhi::kImmediateDataSize);

        struct SkyBackgroundData
        {
            float4 viewport;
            const SceneProxy* scene;
            const SkyBackgroundProxy* skyBackground;
            rg::TextureHandle sceneColor;
            rg::TextureHandle sceneDepth;
        };

        struct SkyBackgroundCaptureData
        {
            uint32_t face;
            uint32_t resolution;
            const SceneProxy* scene;
            const SkyBackgroundProxy* skyBackground;
            rg::TextureHandle color;
            rg::TextureHandle depth;
        };

        constexpr int32_t kScreenCaptureFace = -1;

        VertexLayout getSkyBackgroundVertexLayout()
        {
            return VertexLayout(
                VertexLayoutType::Static_Mesh,
                VertexAttributeMask::Position | VertexAttributeMask::Normal | VertexAttributeMask::Tangent | VertexAttributeMask::TexCoord0
            );
        }

        rhi::Pipeline* createSkyBackgroundPipeline(RendererContext& rendererContext, MaterialProxy& material, VertexLayout vertexLayout)
        {
            ShaderVariant shaderVariant = material.getOrCreateShaderVariant(BuiltinRenderTags::SkyBackground, vertexLayout);
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

            const rhi::BindGroup* parametersBindGroup = material.getParametersBindGroup();
            auto* pipelineLayout = createRenderPassPipelineLayout(
                rendererContext,
                nullptr,
                parametersBindGroup ? &parametersBindGroup->getLayout() : nullptr,
                nullptr
            );

            PipelineCache::GraphicsKey pipelineKey;
            pipelineKey.pipelineState = PipelineStatePresets::fullscreenTriangle();
            pipelineKey.pipelineState.shader = shaderVariant;
            pipelineKey.pipelineState.depthTestEnable = 1;
            pipelineKey.pipelineState.depthWriteEnable = 0;
            pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
                rhi::TextureFormat::D32_SFLOAT_S8_UINT,
                rhi::TextureFormat::RGBA16_SFLOAT
            >();
            pipelineKey.pipelineLayout = pipelineLayout;
            return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
        }

        void drawSkyBackground(
            rg::ExecuteContext& context,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SkyBackgroundProxy& skyBackground,
            float4 viewport,
            int32_t captureFace,
            float captureFaceSize
        )
        {
            MaterialProxy* material = skyBackground.getMaterial();
            if (!material)
                return;

            VertexLayout vertexLayout = getSkyBackgroundVertexLayout();
            rhi::Pipeline* pipeline = material->getCachedPipeline(BuiltinRenderTags::SkyBackground, vertexLayout);
            if (!pipeline)
            {
                pipeline = createSkyBackgroundPipeline(rendererContext, *material, vertexLayout);
                material->cachePipeline(BuiltinRenderTags::SkyBackground, vertexLayout, pipeline);
            }

            SkyBackgroundPushConstants constants = {};
            constants.mainLight0Direction = scene.getMainLightDirection(0);
            constants.mainLight0Intensity = scene.getMainLightIntensity(0);
            constants.mainLight1Direction = scene.getMainLightDirection(1);
            constants.mainLight1Intensity = scene.getMainLightIntensity(1);
            constants.intensity = skyBackground.getIntensity();
            constants.captureFace = captureFace;
            constants.captureFaceSize = captureFaceSize;

            auto& cmdList = context.getCommandList();
            setViewportAndScissor(cmdList, viewport);
            cmdList.bindPipeline(pipeline);

            const rhi::BindGroup* parametersBindGroup = material->getParametersBindGroup();
            if (parametersBindGroup)
            {
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &pipeline->getLayout(),
                    parametersBindGroup,
                    {}
                );
            }

            cmdList.setImmediateData(
                &pipeline->getLayout(),
                0,
                std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&constants), sizeof(constants))
            );
            cmdList.draw(3, 1, 0, 0);
        }
    }

    void SkyBackgroundPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const SkyBackgroundProxy& skyBackground
    )
    {
        renderGraph.addGraphicsPass<SkyBackgroundData>(
            "SkyBackground"_hs,
            [&](rg::RenderGraph::Builder& builder, SkyBackgroundData& data)
            {
                data.viewport = view.getViewport();
                data.scene = &scene;
                data.skyBackground = &skyBackground;

                uint32_t viewId = view.getViewId();
                auto& blackboard = builder.getBlackboard();
                auto sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(viewId));
                data.sceneColor = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Load);
                blackboard.put("SceneColor"_hs(viewId), data.sceneColor);

                auto sceneDepth = blackboard.get<rg::TextureHandle>("SceneDepth"_hs(viewId));
                data.sceneDepth = builder.addDepthStencilAttachment(
                    sceneDepth,
                    rhi::AttachmentLoadOp::Load,
                    false,
                    rhi::AttachmentLoadOp::Load,
                    false
                );
                blackboard.put("SceneDepth"_hs(viewId), data.sceneDepth);
            },
            [&rendererContext](rg::ExecuteContext& context, SkyBackgroundData& data)
            {
                drawSkyBackground(context, rendererContext, *data.scene, *data.skyBackground, data.viewport, kScreenCaptureFace, 0.0f);
            }
        );
    }

    void SkyBackgroundPass::addCaptureToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SkyBackgroundProxy& skyBackground,
        HString cubemapName,
        const rhi::TextureDesc& cubemapDesc
    )
    {
        static const HString faceNames[] = {
            "SkyCubemapFace0"_hs,
            "SkyCubemapFace1"_hs,
            "SkyCubemapFace2"_hs,
            "SkyCubemapFace3"_hs,
            "SkyCubemapFace4"_hs,
            "SkyCubemapFace5"_hs,
        };

        const uint32_t sceneId = scene.getSceneId();
        const HString depthName = "SkyCubemapDepth"_hs(sceneId);
        const uint32_t resolution = cubemapDesc.extent.x;

        for (uint32_t face = 0; face < 6; ++face)
        {
            renderGraph.addGraphicsPass<SkyBackgroundCaptureData>(
                "SkyBackgroundCapture"_hs(face),
                [&, face](rg::RenderGraph::Builder& builder, SkyBackgroundCaptureData& data)
                {
                    builder.markSideEffect();

                    data.face = face;
                    data.resolution = resolution;
                    data.scene = &scene;
                    data.skyBackground = &skyBackground;

                    auto& blackboard = builder.getBlackboard();
                    rg::TextureHandle cubemap;
                    rg::TextureHandle depth;
                    if (face == 0)
                    {
                        cubemap = builder.createTexture(cubemapName, cubemapDesc);

                        rhi::TextureDesc depthDesc;
                        depthDesc.type = rhi::TextureType::Texture_2D;
                        depthDesc.format = rhi::TextureFormat::D32_SFLOAT_S8_UINT;
                        depthDesc.extent = { resolution, resolution, 1 };
                        depthDesc.mipCount = 1;
                        depthDesc.layerCount = 1;
                        depthDesc.usage = rhi::TextureUsage::Depth_Stencil_Attachment;
                        depth = builder.createTexture(depthName, depthDesc);
                    }
                    else
                    {
                        cubemap = blackboard.get<rg::TextureHandle>(cubemapName);
                        depth = blackboard.get<rg::TextureHandle>(depthName);
                    }

                    rhi::TextureViewDesc faceDesc;
                    faceDesc.type = rhi::TextureViewType::Texture_View_2D;
                    faceDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                    faceDesc.range.aspect = rhi::TextureAspect::Color;
                    faceDesc.range.baseMip = 0;
                    faceDesc.range.mipCount = 1;
                    faceDesc.range.baseLayer = face;
                    faceDesc.range.layerCount = 1;
                    auto faceView = builder.createSubTexture(faceNames[face](sceneId), cubemap, faceDesc);

                    data.color = builder.addColorAttachment(faceView, rhi::AttachmentLoadOp::Clear);
                    data.depth = builder.addDepthStencilAttachment(
                        depth,
                        rhi::AttachmentLoadOp::Clear,
                        false,
                        rhi::AttachmentLoadOp::Dont_Care,
                        false
                    );

                    cubemap = builder.syncTextures(data.color, cubemap);
                    blackboard.put(cubemapName, cubemap);
                    blackboard.put(depthName, data.depth);
                },
                [&rendererContext](rg::ExecuteContext& context, SkyBackgroundCaptureData& data)
                {
                    float faceSize = static_cast<float>(data.resolution);
                    drawSkyBackground(
                        context,
                        rendererContext,
                        *data.scene,
                        *data.skyBackground,
                        float4(0.0f, 0.0f, faceSize, faceSize),
                        static_cast<int32_t>(data.face),
                        faceSize
                    );
                }
            );
        }
    }
}
