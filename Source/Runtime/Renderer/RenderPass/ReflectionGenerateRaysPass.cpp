#include "ReflectionGenerateRaysPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>

#include <ReflectionGenerateRays.generated.h>

#include <algorithm>

namespace hyp
{
    namespace
    {
        constexpr uint32_t kThreadGroupSize = 8u;

        uint2 dispatchGroupCountFor8(uint2 extent)
        {
            return uint2(
                std::max((extent.x + kThreadGroupSize - 1u) / kThreadGroupSize, 1u),
                std::max((extent.y + kThreadGroupSize - 1u) / kThreadGroupSize, 1u));
        }

        struct ReflectionGenerateRaysPushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
        };
        static_assert(sizeof(ReflectionGenerateRaysPushConstants) == 16);
    }

    struct ReflectionGenerateRaysData
    {
        uint32_t viewId;
        uint2 dispatchGroupCount;
        ReflectionGenerateRaysPushConstants constants;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle gbufferD;
        rg::TextureHandle blueNoise;
        rg::TextureHandle rayBuffer;
        rg::TextureHandle rayDepth;
    };

    struct ReflectionGenerateRaysStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> layout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<8>> caches;
    };

    void ReflectionGenerateRaysPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 viewRectMin(static_cast<uint32_t>(viewport.x), static_cast<uint32_t>(viewport.y));
        const uint2 viewExtent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));

        rg::TextureHandle blueNoiseHandle = renderGraph.importTexture(
            "ReflectionGenerateRaysBlueNoiseVec2Texture"_hs,
            &rendererContext.getRenderBackend().getDefaultResources().getBlueNoiseVec2View(),
            rhi::ResourceState::Shader_Sampled);

        renderGraph.addComputePass<ReflectionGenerateRaysData>(
            "ReflectionGenerateRays"_hs,
            [&](rg::RenderGraph::Builder& builder, ReflectionGenerateRaysData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = dispatchGroupCountFor8(viewExtent);
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;

                auto& blackboard = builder.getBlackboard();
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId)));
                data.gbufferC = builder.sample(blackboard.get<rg::TextureHandle>("GBufferC"_hs(viewId)));
                data.gbufferD = builder.sample(blackboard.get<rg::TextureHandle>("GBufferD"_hs(viewId)));
                data.blueNoise = builder.sample(blueNoiseHandle);

                rhi::TextureDesc rayBufferDesc;
                rayBufferDesc.type = rhi::TextureType::Texture_2D;
                rayBufferDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                rayBufferDesc.extent = { viewExtent.x, viewExtent.y, 1u };
                rayBufferDesc.mipCount = 1u;
                rayBufferDesc.layerCount = 1u;
                rayBufferDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto rayBuffer = builder.createTexture("ReflectionRayBuffer"_hs(viewId), rayBufferDesc);

                rhi::TextureDesc rayDepthDesc = rayBufferDesc;
                rayDepthDesc.format = rhi::TextureFormat::R32_SFLOAT;
                auto rayDepth = builder.createTexture("ReflectionRayDepth"_hs(viewId), rayDepthDesc);

                data.rayBuffer = builder.storage(rayBuffer, rg::AccessType::Write_Only);
                data.rayDepth = builder.storage(rayDepth, rg::AccessType::Write_Only);
                blackboard.put("ReflectionRayBuffer"_hs(viewId), data.rayBuffer);
                blackboard.put("ReflectionRayDepth"_hs(viewId), data.rayDepth);
            },
            [&rendererContext](rg::ExecuteContext& context, ReflectionGenerateRaysData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ReflectionGenerateRaysPass, ReflectionGenerateRaysStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ReflectionGenerateRays",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ReflectionGenerateRays, Compute)));
                    storage.layout = createPassBindGroupLayout(renderBackend, generated::kReflectionGenerateRays_Bindings);
                    storage.pipeline = createComputePipeline(rendererContext, storage.shader->getVariant(), *storage.layout);
                }

                if (storage.caches.size() <= data.viewId)
                    storage.caches.resize(data.viewId + 1u);
                auto& cache = storage.caches[data.viewId];

                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* gbufferC = context.getTexture(data.gbufferC);
                const rhi::TextureView* gbufferD = context.getTexture(data.gbufferD);
                const rhi::TextureView* blueNoise = context.getTexture(data.blueNoise);
                rhi::TextureView* rayBuffer = context.getTexture(data.rayBuffer);
                rhi::TextureView* rayDepth = context.getTexture(data.rayDepth);

                if (cache.needUpdate(sceneDepth, gbufferA, gbufferB, gbufferC, gbufferD, blueNoise, rayBuffer, rayDepth))
                {
                    cache.update(renderBackend, *storage.layout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kReflectionGenerateRays_Bindings);
                        writer.setTexture("SceneDepthImage", sceneDepth);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("GbufferCImage", gbufferC);
                        writer.setTexture("GbufferDImage", gbufferD);
                        writer.setTexture("BlueNoiseVec2Texture", blueNoise);
                        writer.setTexture("RayBuffer", rayBuffer);
                        writer.setTexture("RayDepth", rayDepth);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            });
    }
}
