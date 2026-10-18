#include "ReflectionDenoiserSpatialPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>

#include <ReflectionDenoiserSpatial.generated.h>

#include <algorithm>

namespace hyp
{
    namespace
    {
        constexpr uint32_t kThreadGroupSize = 8u;

        struct ReflectionDenoiserSpatialPushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
        };
        static_assert(sizeof(ReflectionDenoiserSpatialPushConstants) == 16);
    }

    struct ReflectionDenoiserSpatialData
    {
        uint32_t viewId;
        uint2 dispatchGroupCount;
        ReflectionDenoiserSpatialPushConstants constants;
        rg::TextureHandle specularAndSecondMoment;
        rg::TextureHandle numFramesAccumulated;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle gbufferD;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle specularIndirect;
    };

    struct ReflectionDenoiserSpatialStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> layout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<8>> caches;
    };

    void ReflectionDenoiserSpatialPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 viewRectMin(static_cast<uint32_t>(viewport.x), static_cast<uint32_t>(viewport.y));
        const uint2 viewExtent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));

        renderGraph.addComputePass<ReflectionDenoiserSpatialData>(
            "ReflectionDenoiserSpatial"_hs,
            [&](rg::RenderGraph::Builder& builder, ReflectionDenoiserSpatialData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = {
                    std::max((viewExtent.x + kThreadGroupSize - 1u) / kThreadGroupSize, 1u),
                    std::max((viewExtent.y + kThreadGroupSize - 1u) / kThreadGroupSize, 1u) };
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;

                auto& blackboard = builder.getBlackboard();
                data.specularAndSecondMoment = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionSpecularAndSecondMoment"_hs(viewId)));
                data.numFramesAccumulated = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionNumFramesAccumulated"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId)));
                data.gbufferC = builder.sample(blackboard.get<rg::TextureHandle>("GBufferC"_hs(viewId)));
                data.gbufferD = builder.sample(blackboard.get<rg::TextureHandle>("GBufferD"_hs(viewId)));
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));

                rhi::TextureDesc specularIndirectDesc;
                specularIndirectDesc.type = rhi::TextureType::Texture_2D;
                specularIndirectDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                specularIndirectDesc.extent = { viewExtent.x, viewExtent.y, 1u };
                specularIndirectDesc.mipCount = 1u;
                specularIndirectDesc.layerCount = 1u;
                specularIndirectDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto specularIndirect = builder.createTexture("ReflectionSpecularIndirect"_hs(viewId), specularIndirectDesc);

                data.specularIndirect = builder.storage(specularIndirect, rg::AccessType::Write_Only);
                blackboard.put("ReflectionSpecularIndirect"_hs(viewId), data.specularIndirect);
            },
            [&rendererContext](rg::ExecuteContext& context, ReflectionDenoiserSpatialData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ReflectionDenoiserSpatialPass, ReflectionDenoiserSpatialStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ReflectionDenoiserSpatial",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ReflectionDenoiserSpatial, Compute)));
                    storage.layout = createPassBindGroupLayout(renderBackend, generated::kReflectionDenoiserSpatial_Bindings);
                    storage.pipeline = createComputePipeline(rendererContext, storage.shader->getVariant(), *storage.layout);
                }

                if (storage.caches.size() <= data.viewId)
                    storage.caches.resize(data.viewId + 1u);
                auto& cache = storage.caches[data.viewId];

                const rhi::TextureView* specularAndSecondMoment = context.getTexture(data.specularAndSecondMoment);
                const rhi::TextureView* numFramesAccumulated = context.getTexture(data.numFramesAccumulated);
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* gbufferC = context.getTexture(data.gbufferC);
                const rhi::TextureView* gbufferD = context.getTexture(data.gbufferD);
                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                rhi::TextureView* specularIndirect = context.getTexture(data.specularIndirect);

                if (cache.needUpdate(
                        specularAndSecondMoment, numFramesAccumulated, gbufferA, gbufferB, gbufferC, gbufferD,
                        sceneDepth, specularIndirect))
                {
                    cache.update(renderBackend, *storage.layout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kReflectionDenoiserSpatial_Bindings);
                        writer.setTexture("SpecularAndSecondMoment", specularAndSecondMoment);
                        writer.setTexture("NumFramesAccumulated", numFramesAccumulated);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("GbufferCImage", gbufferC);
                        writer.setTexture("GbufferDImage", gbufferD);
                        writer.setTexture("SceneDepthImage", sceneDepth);
                        writer.setTexture("SpecularIndirect", specularIndirect);
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
