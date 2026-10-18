#include "CombineFinalLightingPass.h"
#include "RenderPassUtils.h"
#include "../SurfaceCacheAtlas.h"
#include "../LumenScene.h"

#include <RenderBase/RenderBackend.h>
#include <RenderBase/MeshShapeProxy.h>

#include <CombineFinalLighting.generated.h>

#include <span>
#include <vector>

namespace hyp
{
    namespace
    {
        // 跟 Shader/Material/CombineFinalLighting.slang 里的 CombineFinalLightingConstants
        // 逐字段对应
        struct CombineFinalLightingConstants
        {
            // mCardPageTable 里存的打包值本身——shader 端用 decodeCardPageAddress() 解出像素
            // 原点+实际尺寸, 子页分配的卡需要知道实际尺寸才能正确设边界检查, 不能再假设
            // 整页
            uint32_t packedAddress;
        };
        static_assert(sizeof(CombineFinalLightingConstants) == 4);

        struct CombineFinalLightingPageTask
        {
            CombineFinalLightingConstants constants;
            uint32_t groupsX;
            uint32_t groupsY;
        };

        struct CombineFinalLightingData
        {
            std::vector<CombineFinalLightingPageTask> tasks;
            rg::TextureHandle albedoAtlas;
            rg::TextureHandle emissiveAtlas;
            rg::TextureHandle directLightingAtlas;
            rg::TextureHandle indirectLightingAtlas;
            rg::TextureHandle finalLightingAtlas;
        };

        struct CombineFinalLightingStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> bindGroupLayout;
            TRef<rhi::Pipeline> pipeline;
            BindGroupCache<5> bindGroupCache;
        };

        rhi::Pipeline* createCombineFinalLightingPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout)
        {
            return createComputePipeline(rendererContext, shaderVariant, bindGroupLayout);
        }
    }

    void CombineFinalLightingPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene
    )
    {
        LumenScene* lumenScene = scene.getLumenScene();
        if (!lumenScene)
            return;
        SurfaceCacheAtlas& surfaceCacheAtlas = lumenScene->getSurfaceCacheAtlas();

        // "CardAtlasDirectLighting"_hs/"CardAtlasIndirectLighting"_hs 这一帧只有
        // CardDirectLightingPass/RadiosityPass 真正跑过(没有早退)才会被写入——调用方必须
        // 保证这个 pass 排在它们之后, 这里只读, 不重新分配/重新 import。拿不到就说明上游
        // 早退了(数据没就绪), 跟着跳过, 下一帧自然追上, 理由跟 CardDirectLightingPass.cpp/
        // RadiosityPass.cpp 本身的早退一致
        auto directLightingAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>("CardAtlasDirectLighting"_hs);
        auto indirectLightingAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>("CardAtlasIndirectLighting"_hs);
        if (!directLightingAtlasHandle.isValid() || !indirectLightingAtlasHandle.isValid())
            return;

        std::vector<CombineFinalLightingPageTask> tasks;
        for (const SurfaceCacheLightingPage& page : lumenScene->getLightingSchedule().combine)
        {
            CombineFinalLightingPageTask task;
            task.constants.packedAddress = page.packedAddress;
            task.groupsX = (uint32_t(page.extent.x) + 7) / 8;
            task.groupsY = (uint32_t(page.extent.y) + 7) / 8;
            tasks.push_back(task);
        }

        if (tasks.empty())
            return;

        // Albedo/Emissive 全程只读、这一帧不会再被写(CardCapturePass 早就画完了)——独立
        // import 一份只读句柄即可, 不需要 getOrImportResource 去复用调用方那份, 两份独立
        // 句柄各自声明"已经处于 Shader_Sampled"互不冲突, 跟 CardDirectLightingPass.cpp 对
        // CardAtlasNormal 的处理是同一个道理
        auto albedoAtlasHandle = renderGraph.importTexture(
            "CardAtlasAlbedo"_hs,
            &surfaceCacheAtlas.getAlbedoAtlasTextureView(),
            rhi::ResourceState::Shader_Sampled
        );
        auto emissiveAtlasHandle = renderGraph.importTexture(
            "CardAtlasEmissive"_hs,
            &surfaceCacheAtlas.getEmissiveAtlasTextureView(),
            rhi::ResourceState::Shader_Sampled
        );
        // FinalLightingAtlas 是这一帧第一个写它的 pass——用 getOrImportResource, 万一调用方
        // (GlobalDistanceFieldVisualizerPass)已经为了采样它先导入过一次, 必须复用那一份句柄,
        // 这次写完之后的新版本它的 builder.sample() 才会被识别成读后写依赖, 跟
        // CardDirectLightingPass.cpp 对 CardAtlasDirectLighting 的处理同一个理由
        auto finalLightingAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "CardAtlasFinalLighting"_hs, [&]
        {
            return renderGraph.importTexture(
                "CardAtlasFinalLighting"_hs,
                &surfaceCacheAtlas.getFinalLightingAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
        });

        renderGraph.addComputePass<CombineFinalLightingData>(
            "CombineFinalLighting"_hs,
            [&](rg::RenderGraph::Builder& builder, CombineFinalLightingData& data)
            {
                builder.markSideEffect();

                data.tasks = std::move(tasks);
                data.albedoAtlas = builder.sample(albedoAtlasHandle);
                data.emissiveAtlas = builder.sample(emissiveAtlasHandle);
                data.directLightingAtlas = builder.sample(directLightingAtlasHandle);
                data.indirectLightingAtlas = builder.sample(indirectLightingAtlasHandle);
                data.finalLightingAtlas = builder.storage(finalLightingAtlasHandle, rg::AccessType::Write_Only);
                finalLightingAtlasHandle = data.finalLightingAtlas;
            },
            [&rendererContext](rg::ExecuteContext& context, CombineFinalLightingData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<CombineFinalLightingPass, CombineFinalLightingStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "CombineFinalLighting",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(CombineFinalLighting, Compute))
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kCombineFinalLighting_Bindings);
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createCombineFinalLightingPipeline(rendererContext, storage.shader->getVariant(), *storage.bindGroupLayout);
                }

                const rhi::TextureView* albedoAtlas = context.getTexture(data.albedoAtlas);
                const rhi::TextureView* emissiveAtlas = context.getTexture(data.emissiveAtlas);
                const rhi::TextureView* directLightingAtlas = context.getTexture(data.directLightingAtlas);
                const rhi::TextureView* indirectLightingAtlas = context.getTexture(data.indirectLightingAtlas);
                const rhi::TextureView* finalLightingAtlas = context.getTexture(data.finalLightingAtlas);

                if (storage.bindGroupCache.needUpdate(albedoAtlas, emissiveAtlas, directLightingAtlas, indirectLightingAtlas, finalLightingAtlas))
                {
                    storage.bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kCombineFinalLighting_Bindings);
                            writer.setTexture("CardAlbedoAtlas", albedoAtlas);
                            writer.setTexture("CardEmissiveAtlas", emissiveAtlas);
                            writer.setTexture("DirectLightingAtlas", directLightingAtlas);
                            writer.setTexture("IndirectLightingAtlas", indirectLightingAtlas);
                            writer.setTexture("FinalLightingAtlas", finalLightingAtlas);
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

                for (const CombineFinalLightingPageTask& task : data.tasks)
                {
                    cmdList.setImmediateData(
                        &storage.pipeline->getLayout(),
                        0,
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&task.constants), sizeof(task.constants))
                    );
                    cmdList.dispatch(task.groupsX, task.groupsY, 1);
                }
            }
        );

        // 存回去的是写完之后的新版本——调用方从 blackboard 里查到的必须是这个版本, 它对这张
        // 纹理的 builder.sample() 才会被识别成读后写依赖, 跟 CardDirectLightingPass.cpp 末尾
        // 同一个理由
        renderGraph.getBlackboard().put("CardAtlasFinalLighting"_hs, finalLightingAtlasHandle);
    }
}
