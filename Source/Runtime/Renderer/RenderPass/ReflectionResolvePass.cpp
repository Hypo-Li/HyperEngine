#include "ReflectionResolvePass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>

#include <ReflectionResolve.generated.h>

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

        struct ReflectionResolvePushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
        };
        static_assert(sizeof(ReflectionResolvePushConstants) == 16);
    }

    struct ReflectionResolveData
    {
        uint32_t viewId;
        uint2 dispatchGroupCount;
        ReflectionResolvePushConstants constants;
        rg::TextureHandle rayBuffer;
        rg::TextureHandle rayDepth;
        rg::TextureHandle traceRadiance;
        rg::TextureHandle traceHit;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle gbufferD;
        rg::TextureHandle resolvedSpecularIndirect;
        rg::TextureHandle resolvedSpecularIndirectDepth;
    };

    struct ReflectionResolveStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> layout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<10>> caches;
    };

    void ReflectionResolvePass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 viewRectMin(static_cast<uint32_t>(viewport.x), static_cast<uint32_t>(viewport.y));
        const uint2 viewExtent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));

        renderGraph.addComputePass<ReflectionResolveData>(
            "ReflectionResolve"_hs,
            [&](rg::RenderGraph::Builder& builder, ReflectionResolveData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = dispatchGroupCountFor8(viewExtent);
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;

                auto& blackboard = builder.getBlackboard();
                data.rayBuffer = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionRayBuffer"_hs(viewId)));
                data.rayDepth = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionRayDepth"_hs(viewId)));
                data.traceRadiance = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionTraceRadiance"_hs(viewId)));
                data.traceHit = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionTraceHit"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId)));
                data.gbufferC = builder.sample(blackboard.get<rg::TextureHandle>("GBufferC"_hs(viewId)));
                data.gbufferD = builder.sample(blackboard.get<rg::TextureHandle>("GBufferD"_hs(viewId)));

                rhi::TextureDesc resolvedDesc;
                resolvedDesc.type = rhi::TextureType::Texture_2D;
                resolvedDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                resolvedDesc.extent = { viewExtent.x, viewExtent.y, 1u };
                resolvedDesc.mipCount = 1u;
                resolvedDesc.layerCount = 1u;
                resolvedDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto resolvedSpecularIndirect = builder.createTexture("ReflectionResolvedSpecularIndirect"_hs(viewId), resolvedDesc);

                rhi::TextureDesc resolvedDepthDesc = resolvedDesc;
                resolvedDepthDesc.format = rhi::TextureFormat::R16_SFLOAT;
                auto resolvedSpecularIndirectDepth = builder.createTexture("ReflectionResolvedSpecularIndirectDepth"_hs(viewId), resolvedDepthDesc);

                data.resolvedSpecularIndirect = builder.storage(resolvedSpecularIndirect, rg::AccessType::Write_Only);
                data.resolvedSpecularIndirectDepth = builder.storage(resolvedSpecularIndirectDepth, rg::AccessType::Write_Only);
                blackboard.put("ReflectionResolvedSpecularIndirect"_hs(viewId), data.resolvedSpecularIndirect);
                blackboard.put("ReflectionResolvedSpecularIndirectDepth"_hs(viewId), data.resolvedSpecularIndirectDepth);
            },
            [&rendererContext](rg::ExecuteContext& context, ReflectionResolveData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ReflectionResolvePass, ReflectionResolveStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ReflectionResolve",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ReflectionResolve, Compute)));
                    storage.layout = createPassBindGroupLayout(renderBackend, generated::kReflectionResolve_Bindings);
                    storage.pipeline = createComputePipeline(rendererContext, storage.shader->getVariant(), *storage.layout);
                }

                if (storage.caches.size() <= data.viewId)
                    storage.caches.resize(data.viewId + 1u);
                auto& cache = storage.caches[data.viewId];

                const rhi::TextureView* rayBuffer = context.getTexture(data.rayBuffer);
                const rhi::TextureView* rayDepth = context.getTexture(data.rayDepth);
                const rhi::TextureView* traceRadiance = context.getTexture(data.traceRadiance);
                const rhi::TextureView* traceHit = context.getTexture(data.traceHit);
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* gbufferC = context.getTexture(data.gbufferC);
                const rhi::TextureView* gbufferD = context.getTexture(data.gbufferD);
                rhi::TextureView* resolvedSpecularIndirect = context.getTexture(data.resolvedSpecularIndirect);
                rhi::TextureView* resolvedSpecularIndirectDepth = context.getTexture(data.resolvedSpecularIndirectDepth);

                if (cache.needUpdate(
                        rayBuffer, rayDepth, traceRadiance, traceHit, gbufferA, gbufferB, gbufferC, gbufferD,
                        resolvedSpecularIndirect, resolvedSpecularIndirectDepth))
                {
                    cache.update(renderBackend, *storage.layout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kReflectionResolve_Bindings);
                        writer.setTexture("RayBuffer", rayBuffer);
                        writer.setTexture("RayDepth", rayDepth);
                        writer.setTexture("TraceRadiance", traceRadiance);
                        writer.setTexture("TraceHit", traceHit);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("GbufferCImage", gbufferC);
                        writer.setTexture("GbufferDImage", gbufferD);
                        writer.setTexture("ResolvedSpecularIndirect", resolvedSpecularIndirect);
                        writer.setTexture("ResolvedSpecularIndirectDepth", resolvedSpecularIndirectDepth);
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
