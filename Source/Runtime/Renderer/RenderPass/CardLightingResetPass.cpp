#include "CardLightingResetPass.h"
#include "RenderPassUtils.h"
#include "../SurfaceCacheAtlas.h"

#include <RenderBase/RenderBackend.h>

#include <CardLightingReset.generated.h>

#include <span>

namespace hyp
{
    namespace
    {
        // 跟 Shader/Material/CardLightingReset.slang 里的 CardLightingResetConstants 逐字段对应
        struct CardLightingResetConstants
        {
            uint32_t captureOriginX;
            uint32_t captureOriginY;
            uint32_t atlasOriginX;
            uint32_t atlasOriginY;
            uint32_t extentX;
            uint32_t extentY;
        };
        static_assert(sizeof(CardLightingResetConstants) == 24);

        struct CardLightingResetData
        {
            std::vector<CardCaptureCopyTask> tasks;
            rg::TextureHandle captureEmissive;
            rg::TextureHandle directLightingAtlas;
            rg::TextureHandle indirectLightingAtlas;
            rg::TextureHandle finalLightingAtlas;
            rg::TextureHandle numFramesAccumulatedAtlas;
        };

        struct CardLightingResetStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> bindGroupLayout;
            TRef<rhi::Pipeline> pipeline;
            BindGroupCache<5> bindGroupCache;
        };
    }

    void CardLightingResetPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        SurfaceCacheAtlas& surfaceCacheAtlas,
        rg::TextureHandle captureEmissive,
        std::vector<CardCaptureCopyTask>&& tasks
    )
    {
        auto directLightingAtlasHandle = renderGraph.importTexture("CardAtlasDirectLighting"_hs, &surfaceCacheAtlas.getDirectLightingAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        auto indirectLightingAtlasHandle = renderGraph.importTexture("CardAtlasIndirectLighting"_hs, &surfaceCacheAtlas.getIndirectLightingAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        auto finalLightingAtlasHandle = renderGraph.importTexture("CardAtlasFinalLighting"_hs, &surfaceCacheAtlas.getFinalLightingAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        auto numFramesAccumulatedAtlasHandle = renderGraph.importTexture("CardAtlasRadiosityNumFramesAccumulated"_hs, &surfaceCacheAtlas.getRadiosityNumFramesAccumulatedAtlasTextureView(), rhi::ResourceState::Shader_Sampled);

        renderGraph.addComputePass<CardLightingResetData>(
            "CardLightingReset"_hs,
            [&](rg::RenderGraph::Builder& builder, CardLightingResetData& data)
            {
                builder.markSideEffect();

                data.tasks = std::move(tasks);
                data.captureEmissive = builder.sample(captureEmissive);
                data.directLightingAtlas = builder.storage(directLightingAtlasHandle, rg::AccessType::Write_Only);
                data.indirectLightingAtlas = builder.storage(indirectLightingAtlasHandle, rg::AccessType::Write_Only);
                data.finalLightingAtlas = builder.storage(finalLightingAtlasHandle, rg::AccessType::Write_Only);
                data.numFramesAccumulatedAtlas = builder.storage(numFramesAccumulatedAtlasHandle, rg::AccessType::Write_Only);
            },
            [&rendererContext](rg::ExecuteContext& context, CardLightingResetData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<CardLightingResetPass, CardLightingResetStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "CardLightingReset",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(CardLightingReset, Compute))
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kCardLightingReset_Bindings);
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createComputePipeline(rendererContext, storage.shader->getVariant(), *storage.bindGroupLayout);
                }

                const rhi::TextureView* captureEmissiveView = context.getTexture(data.captureEmissive);
                const rhi::TextureView* directLightingAtlas = context.getTexture(data.directLightingAtlas);
                const rhi::TextureView* indirectLightingAtlas = context.getTexture(data.indirectLightingAtlas);
                const rhi::TextureView* finalLightingAtlas = context.getTexture(data.finalLightingAtlas);
                const rhi::TextureView* numFramesAccumulatedAtlas = context.getTexture(data.numFramesAccumulatedAtlas);

                if (storage.bindGroupCache.needUpdate(captureEmissiveView, directLightingAtlas, indirectLightingAtlas, finalLightingAtlas, numFramesAccumulatedAtlas))
                {
                    storage.bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kCardLightingReset_Bindings);
                            writer.setTexture("CaptureEmissive", captureEmissiveView);
                            writer.setTexture("DirectLightingAtlas", directLightingAtlas);
                            writer.setTexture("IndirectLightingAtlas", indirectLightingAtlas);
                            writer.setTexture("FinalLightingAtlas", finalLightingAtlas);
                            writer.setTexture("RadiosityNumFramesAccumulatedAtlas", numFramesAccumulatedAtlas);
                            writer.apply(bindGroup);
                        }
                    );
                }

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Compute,
                    &storage.pipeline->getLayout(),
                    storage.bindGroupCache.getBindGroup(),
                    {}
                );

                const uint32_t kThreadsPerGroup = 8; // 跟 CardLightingReset.slang 的 [numthreads(8,8,1)] 一致
                for (const CardCaptureCopyTask& task : data.tasks)
                {
                    CardLightingResetConstants constants;
                    constants.captureOriginX = uint32_t(task.captureOrigin.x);
                    constants.captureOriginY = uint32_t(task.captureOrigin.y);
                    constants.atlasOriginX = uint32_t(task.atlasOrigin.x);
                    constants.atlasOriginY = uint32_t(task.atlasOrigin.y);
                    constants.extentX = uint32_t(task.extent.x);
                    constants.extentY = uint32_t(task.extent.y);
                    cmdList.setImmediateData(
                        &storage.pipeline->getLayout(),
                        0,
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&constants), sizeof(constants))
                    );
                    cmdList.dispatch(
                        (constants.extentX + kThreadsPerGroup - 1) / kThreadsPerGroup,
                        (constants.extentY + kThreadsPerGroup - 1) / kThreadsPerGroup,
                        1
                    );
                }
            }
        );
    }
}
