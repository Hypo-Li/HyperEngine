#include "CardCaptureCopyPass.h"
#include "RenderPassUtils.h"
#include "../SurfaceCacheAtlas.h"

#include <RenderBase/RenderBackend.h>

#include <CardCaptureCopy.generated.h>

#include <span>

namespace hyp
{
    namespace
    {
        // 跟 Shader/Material/CardCaptureCopy.slang 里的 CardCaptureCopyConstants 逐字段对应
        struct CardCaptureCopyConstants
        {
            uint32_t captureOriginX;
            uint32_t captureOriginY;
            uint32_t atlasOriginX;
            uint32_t atlasOriginY;
            uint32_t extentX;
            uint32_t extentY;
        };
        static_assert(sizeof(CardCaptureCopyConstants) == 24);

        struct CardCaptureCopyData
        {
            std::vector<CardCaptureCopyTask> tasks;
            CardCaptureTextures captureTextures;
            rg::TextureHandle albedoAtlas;
            rg::TextureHandle normalAtlas;
            rg::TextureHandle emissiveAtlas;
            rg::TextureHandle depthAtlas;
        };

        struct CardCaptureCopyStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> bindGroupLayout;
            TRef<rhi::Pipeline> pipeline;
            BindGroupCache<8> bindGroupCache;
        };
    }

    void CardCaptureCopyPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        SurfaceCacheAtlas& surfaceCacheAtlas,
        const CardCaptureTextures& captureTextures,
        std::vector<CardCaptureCopyTask>&& tasks
    )
    {
        auto albedoAtlasHandle = renderGraph.importTexture("CardAtlasAlbedo"_hs, &surfaceCacheAtlas.getAlbedoAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        auto normalAtlasHandle = renderGraph.importTexture("CardAtlasNormal"_hs, &surfaceCacheAtlas.getNormalAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        auto emissiveAtlasHandle = renderGraph.importTexture("CardAtlasEmissive"_hs, &surfaceCacheAtlas.getEmissiveAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        auto depthAtlasHandle = renderGraph.importTexture("CardAtlasDepth"_hs, &surfaceCacheAtlas.getDepthAtlasTextureView(), rhi::ResourceState::Shader_Sampled);

        renderGraph.addComputePass<CardCaptureCopyData>(
            "CardCaptureCopy"_hs,
            [&](rg::RenderGraph::Builder& builder, CardCaptureCopyData& data)
            {
                builder.markSideEffect();

                data.tasks = std::move(tasks);
                data.captureTextures.albedo = builder.sample(captureTextures.albedo);
                data.captureTextures.normal = builder.sample(captureTextures.normal);
                data.captureTextures.emissive = builder.sample(captureTextures.emissive);
                data.captureTextures.depth = builder.sample(captureTextures.depth);
                data.albedoAtlas = builder.storage(albedoAtlasHandle, rg::AccessType::Write_Only);
                data.normalAtlas = builder.storage(normalAtlasHandle, rg::AccessType::Write_Only);
                data.emissiveAtlas = builder.storage(emissiveAtlasHandle, rg::AccessType::Write_Only);
                data.depthAtlas = builder.storage(depthAtlasHandle, rg::AccessType::Write_Only);
            },
            [&rendererContext](rg::ExecuteContext& context, CardCaptureCopyData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<CardCaptureCopyPass, CardCaptureCopyStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "CardCaptureCopy",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(CardCaptureCopy, Compute))
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kCardCaptureCopy_Bindings);
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createComputePipeline(rendererContext, storage.shader->getVariant(), *storage.bindGroupLayout);
                }

                const rhi::TextureView* captureAlbedo = context.getTexture(data.captureTextures.albedo);
                const rhi::TextureView* captureNormal = context.getTexture(data.captureTextures.normal);
                const rhi::TextureView* captureEmissive = context.getTexture(data.captureTextures.emissive);
                const rhi::TextureView* captureDepth = context.getTexture(data.captureTextures.depth);
                const rhi::TextureView* albedoAtlas = context.getTexture(data.albedoAtlas);
                const rhi::TextureView* normalAtlas = context.getTexture(data.normalAtlas);
                const rhi::TextureView* emissiveAtlas = context.getTexture(data.emissiveAtlas);
                const rhi::TextureView* depthAtlas = context.getTexture(data.depthAtlas);

                if (storage.bindGroupCache.needUpdate(captureAlbedo, captureNormal, captureEmissive, captureDepth, albedoAtlas, normalAtlas, emissiveAtlas, depthAtlas))
                {
                    storage.bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kCardCaptureCopy_Bindings);
                            writer.setTexture("CaptureAlbedo", captureAlbedo);
                            writer.setTexture("CaptureNormal", captureNormal);
                            writer.setTexture("CaptureEmissive", captureEmissive);
                            writer.setTexture("CaptureDepth", captureDepth);
                            writer.setTexture("AlbedoAtlas", albedoAtlas);
                            writer.setTexture("NormalAtlas", normalAtlas);
                            writer.setTexture("EmissiveAtlas", emissiveAtlas);
                            writer.setTexture("DepthAtlas", depthAtlas);
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

                const uint32_t kThreadsPerGroup = 8; // 跟 CardCaptureCopy.slang 的 [numthreads(8,8,1)] 一致
                for (const CardCaptureCopyTask& task : data.tasks)
                {
                    CardCaptureCopyConstants constants;
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
