#include "LumenSceneUpdatePass.h"
#include "RenderPassUtils.h"
#include "CardCapturePass.h"
#include "CardDirectLightingPass.h"
#include "CombineFinalLightingPass.h"
#include "RadiosityPass.h"
#include "GlobalDistanceFieldCompositePass.h"

#include <RenderBase/DistanceFieldBrickAtlas.h>
#include "../CameraProxy.h"
#include "../SurfaceCacheAtlas.h"
#include "../GlobalDistanceField.h"
#include "../LumenScene.h"

namespace hyp
{
    LumenSceneUpdateResult LumenSceneUpdatePass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const CameraProxy& camera,
        bool requireMeshDistanceFieldAtlas)
    {
        LumenSceneUpdateResult result;

        LumenScene* lumenScene = scene.getLumenScene();
        if (!lumenScene)
            return result;

        RenderBackend& renderBackend = rendererContext.getRenderBackend();
        GlobalDistanceField& gdf = camera.getGlobalDistanceField();

        DistanceFieldBrickAtlas& gdfAtlasPool = lumenScene->getGlobalDistanceFieldBrickAtlas();
        DistanceFieldBrickAtlas& mdfAtlasPool = renderBackend.getMeshDistanceFieldBrickAtlas();
        MeshShapeDataTable& objectTable = renderBackend.getMeshShapeDataTable();
        SurfaceCacheAtlas& surfaceCacheAtlas = lumenScene->getSurfaceCacheAtlas();

        // (a) 每帧只需要跑一次的 CardCapturePass/CardDirectLightingPass——用 blackboard 记录
        // 这一帧是否已经跑过, 避免同一帧多个相机视图各自重复捕获/重复算光照。key 按场景 id
        // 区分, 不区分相机: Blackboard 是整个 render graph 共用一份(不是按 pass/view 各自
        // 隔离), 第一个跑到这里的相机视图负责捕获+光照+导入, 后面的直接复用已经导入的 handle
        HString cardFinalLightingKey = "GlobalDistanceFieldVisualizerCardFinalLighting"_hs(scene.getSceneId());
        HString cardIndirectLightingKey = "GlobalDistanceFieldVisualizerCardIndirectLighting"_hs(scene.getSceneId());
        HString cardDirectLightingKey = "GlobalDistanceFieldVisualizerCardDirectLighting"_hs(scene.getSceneId());
        HString cardAlbedoKey = "GlobalDistanceFieldVisualizerCardAlbedo"_hs(scene.getSceneId());
        HString cardNormalKey = "GlobalDistanceFieldVisualizerCardNormal"_hs(scene.getSceneId());
        HString cardEmissiveKey = "GlobalDistanceFieldVisualizerCardEmissive"_hs(scene.getSceneId());
        HString cardDepthKey = "GlobalDistanceFieldVisualizerCardDepth"_hs(scene.getSceneId());
        HString cardPagesKey = "GlobalDistanceFieldVisualizerCardPages"_hs(scene.getSceneId());

        rg::TextureHandle cardFinalLightingAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>(cardFinalLightingKey);
        rg::TextureHandle cardIndirectLightingAtlasHandle;
        rg::TextureHandle cardDirectLightingAtlasHandle;
        rg::TextureHandle cardAlbedoAtlasHandle;
        rg::TextureHandle cardNormalAtlasHandle;
        rg::TextureHandle cardEmissiveAtlasHandle;
        rg::TextureHandle cardDepthAtlasHandle;
        rg::BufferHandle cardPagesHandle;
        if (cardFinalLightingAtlasHandle.isValid())
        {
            cardIndirectLightingAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>(cardIndirectLightingKey);
            cardDirectLightingAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>(cardDirectLightingKey);
            cardAlbedoAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>(cardAlbedoKey);
            cardNormalAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>(cardNormalKey);
            cardEmissiveAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>(cardEmissiveKey);
            cardDepthAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>(cardDepthKey);
            cardPagesHandle = renderGraph.getBlackboard().get<rg::BufferHandle>(cardPagesKey);
        }
        else
        {
            CardCapturePass::addToRenderGraph(renderGraph, rendererContext, scene);
            lumenScene->updateLightingSchedule(scene.getMeshRenderProxies());

            cardFinalLightingAtlasHandle = renderGraph.importTexture(
                "CardAtlasFinalLighting"_hs,
                &surfaceCacheAtlas.getFinalLightingAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
            cardIndirectLightingAtlasHandle = renderGraph.importTexture(
                "CardAtlasIndirectLighting"_hs,
                &surfaceCacheAtlas.getIndirectLightingAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
            cardDirectLightingAtlasHandle = renderGraph.importTexture(
                "CardAtlasDirectLighting"_hs,
                &surfaceCacheAtlas.getDirectLightingAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
            cardAlbedoAtlasHandle = renderGraph.importTexture(
                "CardAtlasAlbedo"_hs,
                &surfaceCacheAtlas.getAlbedoAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
            cardNormalAtlasHandle = renderGraph.importTexture(
                "CardAtlasNormal"_hs,
                &surfaceCacheAtlas.getNormalAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
            cardEmissiveAtlasHandle = renderGraph.importTexture(
                "CardAtlasEmissive"_hs,
                &surfaceCacheAtlas.getEmissiveAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
            cardDepthAtlasHandle = renderGraph.importTexture(
                "CardAtlasDepth"_hs,
                &surfaceCacheAtlas.getDepthAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
            cardPagesHandle = renderGraph.importBuffer(
                "MeshSDFCardPages"_hs,
                surfaceCacheAtlas.getCardPageTableBuffer()
            );

            renderGraph.getBlackboard().put(cardFinalLightingKey, cardFinalLightingAtlasHandle);
            renderGraph.getBlackboard().put(cardIndirectLightingKey, cardIndirectLightingAtlasHandle);
            renderGraph.getBlackboard().put(cardDirectLightingKey, cardDirectLightingAtlasHandle);
            renderGraph.getBlackboard().put(cardAlbedoKey, cardAlbedoAtlasHandle);
            renderGraph.getBlackboard().put(cardNormalKey, cardNormalAtlasHandle);
            renderGraph.getBlackboard().put(cardEmissiveKey, cardEmissiveAtlasHandle);
            renderGraph.getBlackboard().put(cardDepthKey, cardDepthAtlasHandle);
            renderGraph.getBlackboard().put(cardPagesKey, cardPagesHandle);
        }

        // (b) 这个相机自己的 GDF atlas 合成——候选剔选/brick 分配(GlobalDistanceField::
        // update())和真正往 atlas 里写距离值的 compute dispatch 都在这一次调用内部完成。
        // 内部会自己检查有没有东西要合成, 没有就不添加任何 pass, 不需要在这里重复判断
        GlobalDistanceFieldCompositePass::addToRenderGraph(renderGraph, rendererContext, scene, camera);

        // 数据没就绪就静默跳过这一帧——这是异步流式上传架构里预期之内、下一帧自动追上的正常状态,
        // 不构成需要报警的问题。gdfIndirectionBuffer/gdfObjectGridBuffer 为空说明这个相机的 GDF
        // 还没有任何一层真正合成过(比如场景里还没有任何 SDF 就绪的物体)
        rhi::Buffer* gdfIndirectionBuffer = gdf.getIndirectionBuffer();
        rhi::Buffer* gdfObjectGridBuffer = gdf.getObjectGridBuffer();
        if (gdf.hasPendingComposite()
            || objectTable.hasPendingUploads()
            || !gdfAtlasPool.isInitialized()
            || !gdfIndirectionBuffer
            || !gdfObjectGridBuffer
            || (requireMeshDistanceFieldAtlas && !mdfAtlasPool.isInitialized()))
        {
            return result;
        }

        // CardDirectLightingPass 的点/聚光阴影固定用主相机的 GDF(见 Lumen_GI_Roadmap.md
        // 对 UE PrimaryView 用法的调研结论)——只有当前处理的这个 view 的相机正好是主相机时
        // 才在这里触发它, 这样上面(b)刚合成完的 gdf 就正是它需要的那一份, 不需要额外判断
        // "主相机的 GDF 这一帧合成过没有"。正常场景只有一个主相机对应一个 view, 每帧至多
        // 命中一次, 不需要再叠一层 blackboard 去重
        std::span<CameraProxy* const> cameraProxies = scene.getCameraProxies();
        if (!cameraProxies.empty() && &camera == cameraProxies[0])
        {
            CardDirectLightingPass::addToRenderGraph(renderGraph, rendererContext, scene, camera, scene.getActiveAtmosphereProxy());
            // RadiosityPass(间接光 [A]Trace/[C]ConvertToSH/[D]Integrate)排在
            // CardDirectLightingPass 之后(Trace 要采上一帧 FinalLighting, 那份数据参与合成用的
            // DirectLighting 刚写完)、CombineFinalLightingPass 之前(Combine 要把这个 pass 写的
            // Indirect 项加进去), 见 Document/Lumen_Radiosity_Plan.md M3。同样只在主相机跑一次
            RadiosityPass::addToRenderGraph(renderGraph, rendererContext, scene, camera);
            // CombineFinalLightingPass 排在 CardDirectLightingPass/RadiosityPass 之后——它自己
            // 读 "CardAtlasDirectLighting"_hs/"CardAtlasIndirectLighting"_hs 拿到刚写完的新
            // 版本, 上游提前 return 时不会写这些 key, CombineFinalLightingPass 内部同样会跟着早退
            CombineFinalLightingPass::addToRenderGraph(renderGraph, rendererContext, scene);
            // 上面本地变量是 CombineFinalLightingPass 运行之前查到的旧版本, 必须按
            // "CardAtlasFinalLighting"_hs 重新查一次, 后面 builder.sample() 才会被识别成读后写依赖
            rg::TextureHandle litAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>("CardAtlasFinalLighting"_hs);
            if (litAtlasHandle.isValid())
                cardFinalLightingAtlasHandle = litAtlasHandle;
            rg::TextureHandle indirectAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>("CardAtlasIndirectLighting"_hs);
            if (indirectAtlasHandle.isValid())
                cardIndirectLightingAtlasHandle = indirectAtlasHandle;
            rg::TextureHandle directAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>("CardAtlasDirectLighting"_hs);
            if (directAtlasHandle.isValid())
                cardDirectLightingAtlasHandle = directAtlasHandle;
        }

        result.ready = true;
        result.cardFinalLightingAtlas = cardFinalLightingAtlasHandle;
        result.cardIndirectLightingAtlas = cardIndirectLightingAtlasHandle;
        result.cardDirectLightingAtlas = cardDirectLightingAtlasHandle;
        result.cardAlbedoAtlas = cardAlbedoAtlasHandle;
        result.cardNormalAtlas = cardNormalAtlasHandle;
        result.cardEmissiveAtlas = cardEmissiveAtlasHandle;
        result.cardDepthAtlas = cardDepthAtlasHandle;
        result.cardPages = cardPagesHandle;
        return result;
    }
}
