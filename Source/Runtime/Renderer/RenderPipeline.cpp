#include "RenderPipeline.h"
#include "DirectionalLightProxy.h"

#include "RenderPass/CascadeShadowDepthPass.h"
#include "RenderPass/GbufferPass.h"
#include "RenderPass/HZBPass.h"
#include "RenderPass/AOPass.h"
#include "RenderPass/ShadowMaskPass.h"
#include "RenderPass/DirectLightingPass.h"
#include "RenderPass/AtmospherePass.h"
#include "RenderPass/SkyBackgroundPass.h"
#include "RenderPass/SkyLightPass.h"
#include "RenderPass/IndirectLightingPass.h"
#include "RenderPass/GSplatPass.h"
#include "RenderPass/TranslucentPass.h"
#include "RenderPass/TAAPass.h"
#include "RenderPass/DownsamplePass.h"
#include "RenderPass/AutoExposurePass.h"
#include "RenderPass/BloomPass.h"
#include "RenderPass/ColorGradingPass.h"
#include "RenderPass/PresentPass.h"
#include "RenderPass/PresentToRenderTargetPass.h"
#include "RenderPass/CopyToRenderTargetPass.h"
#include "RenderPass/CardCapturePass.h"
#include "RenderPass/GlobalDistanceFieldVisualizerPass.h"
#include "RenderPass/ScreenTraceVisualizerPass.h"
#include "RenderPass/ScreenProbeVisualizerPass.h"
#include "RenderPass/ScreenProbePlacementPass.h"
#include "RenderPass/ScreenProbeTracePass.h"
#include "RenderPass/ScreenProbeImportanceSamplingPass.h"
#include "RenderPass/ScreenProbeFilteringPass.h"
#include "RenderPass/ScreenProbeIntegratePass.h"
#include "RenderPass/ScreenProbeShortRangeAOPass.h"
#include "RenderPass/ScreenProbeTemporalFilterPass.h"
#include "RenderPass/ScreenProbeDiffuseIndirectCompositePass.h"
#include "RenderPass/ReflectionGenerateRaysPass.h"
#include "RenderPass/ReflectionTracePass.h"
#include "RenderPass/ReflectionResolvePass.h"
#include "RenderPass/ReflectionDenoiserTemporalPass.h"
#include "RenderPass/ReflectionDenoiserSpatialPass.h"

namespace hyp
{
    struct PrepareData
    {
        const rhi::PipelineLayout* globalPipelineLayout;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
    };

    void DeferredPipeline::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const RenderData& renderData,
        const CameraProxy& camera
    )
    {
        // setBindGroup 不需要处于任何 pass 的 begin/end 范围内: Vulkan 的 vkCmdBindDescriptorSets
        // 本就无条件立即执行, 不看当前是否在 render/compute pass 里; WebGPU 后端的
        // setBindGroupBackend 专门实现了延迟绑定 —— pass 外调用会缓存进
        // mPendingGraphicsBindGroups/mPendingComputeBindGroups, 等下一次 beginRenderPass/
        // beginComputePass 时自动重放到新开的 pass encoder 上(见 WebGPU/CommandList.cpp)。
        // 换成 TransferPass 后, 这个 pass 不再需要包一层从未真正被用到的空 compute pass。
        renderGraph.addTransferPass<PrepareData>(
            "Prepare"_hs,
            [&](rg::RenderGraph::Builder& builder, PrepareData& data)
            {
                builder.markSideEffect();
                data.globalPipelineLayout = &rendererContext.getGlobalPipelineLayout();
                data.viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();

                auto& viewDataManager = rendererContext.getViewDataManager();
                data.viewDataDynamicOffset = viewDataManager.getViewDataDynamicOffset(view.getViewId());
            },
            [](rg::ExecuteContext& context, PrepareData& data)
            {
                rhi::CommandList& cmdList = context.getCommandList();

                rhi::PipelineBindPoint bindPoints[] = {
                    rhi::PipelineBindPoint::Graphics,
                    rhi::PipelineBindPoint::Compute
                };
                for (auto bindPoint : bindPoints)
                {
                    cmdList.setBindGroup(
                        1,
                        bindPoint,
                        data.globalPipelineLayout,
                        data.viewBindGroup,
                        { &data.viewDataDynamicOffset, 1 }
                    );
                }
            }
        );

        // Card 捕获跟场景相机视角无关(它自己的"相机"是每张 card 的正交投影), 不属于任何
        // 一个 view 的产物, 理论上整帧只需要跑一次——但 DeferredPipeline::addToRenderGraph
        // 目前是逐 view 调用的, 还没有一个"每帧一次"的外层钩子可以插。跟这个 Pass 本身
        // "每次调用都重新捕获所有 card, 没有脏标记"的 v1 简化一样, 先接受多 view 场景下
        // 会重复捕获同样内容这个已知浪费, 等接增量捕获的时候一起解决
        // CardCapturePass::addToRenderGraph(renderGraph, rendererContext, scene);

        // GBuffer/大气/天光捕获这条链路不受 Lumen 开关约束, 提前到分支前无条件声明——
        // RenderGraph 有基于引用计数的自动裁剪(RenderGraph.cpp: compile() 里的 DAG::cull()),
        // 没有调用 builder.markSideEffect() 的 pass, 只要它的产出这一帧没人读就会被整体砍掉
        // 不执行。GBufferPass 和 AtmosphereLutPass/AtmospherePass 的 5 个子 pass 都没有调用它, Lumen 分支下这
        // 一整条链(包括 AtmospherePass 依赖的 GBufferPass::SceneColor/SceneDepthD)不会
        // 被任何东西读到, 会被砍成零开销;只有 SkyLightPass 自己的子 pass 调用了它, 真正需要
        // 的 SkyLightData/SkyLightProxy::getSpecularPrefilter() 才会照常写入。这样
        // 提前跑单纯是为了让 GlobalDistanceFieldVisualizerPass(Radiosity Trace pass, 见
        // Document/Lumen_Radiosity_Plan.md §6.1)也能拿到方向性天光数据, 不需要给这三个
        // pass 各自加 getLumenEnabled() 判断
        GBufferPass::addToRenderGraph(renderGraph, rendererContext, scene, view, renderData);

        // HZB(Furthest+Closest 双通道)同样不受 Lumen 开关约束, 理由跟上面 GBuffer 一致——
        // Lumen 分支下屏幕空间追踪(Document/Lumen_ScreenProbeGather_Plan.md)要读它, 非
        // Lumen 分支下 AOPass 也要读它, 没人读的那一路会被 DAG::cull() 自动砍掉, 不需要
        // 在这里按 getLumenEnabled() 分别声明一次
        HZBPass::addToRenderGraph(renderGraph, rendererContext, view);

        SkyBackgroundProxy* activeSkyBackground = scene.getActiveSkyBackgroundProxy();
        if (activeSkyBackground)
        {
            SkyBackgroundPass::addToRenderGraph(renderGraph, rendererContext, scene, view, *activeSkyBackground);
        }

        // DirectLightingPass 现在要按太阳光穿过大气的透过率衰减直接光照, 需要读 AtmosphereLutPass
        // 产出的 TransmittanceLut(通过 blackboard), 必须排在 AtmosphereLutPass 后面
        AtmosphereProxy* activeAtmosphere = scene.getActiveAtmosphereProxy();
        if (activeAtmosphere)
        {
            AtmosphereLutPass::addToRenderGraph(renderGraph, rendererContext, scene, view, *activeAtmosphere);
        }

        SkyLightProxy* activeSkyLight = scene.getActiveSkyLightProxy();
        if (activeSkyLight)
        {
            SkyLightPass::addToRenderGraph(
                renderGraph,
                rendererContext,
                scene,
                *activeSkyLight,
                activeAtmosphere,
                activeSkyBackground
            );
        }

        // 分派: ScreenTraceHitState 只用得到这一帧的 GBuffer/HZB, 不需要真的产出光照结果,
        // 保持"全替换"最简单; ScreenTraceRadiance/ScreenProbePlacement 都要叠在一张真实存在的
        // 画面上(前者要采样真实颜色, 后者要看探针跟物体轮廓的空间关系), 必须让正常光照链路
        // (含 TAA)照常跑完, 不能替换掉——不然采到的要么是空的要么是冻在切模式前那一帧的旧
        // TAA history(踩过这个坑, 见下面正常管线分支末尾的调用点)。None 同样走正常管线。剩下的情况才看 Lumen
        // 是否开启, 决定是替换成 GDF 可视化还是走正常延迟渲染链路
        const SceneVisualizeMode visualizeMode = scene.getSceneVisualizeMode();
        if (visualizeMode == SceneVisualizeMode::ScreenTraceHitState)
        {
            // 屏幕空间 HZB 追踪的临时调试可视化, 见 ScreenTraceVisualizerPass 的注释——
            // 对真实光栅化出来的 GBuffer/HZB 求交, 不是 GDF/Surface Cache 那一套
            ScreenTraceVisualizerPass::addToRenderGraph(renderGraph, rendererContext, scene, view);
        }
        else if (visualizeMode != SceneVisualizeMode::None
            && visualizeMode != SceneVisualizeMode::ScreenTraceRadiance
            && visualizeMode != SceneVisualizeMode::ScreenProbePlacement
            && visualizeMode != SceneVisualizeMode::ScreenProbeRadiance
            && visualizeMode != SceneVisualizeMode::ScreenProbeIrradiance
            && visualizeMode != SceneVisualizeMode::ScreenProbeBRDFPDF
            && scene.getLumenEnabled())
        {
            // GDF 粗测距 / MDF 精确求交是这个 Pass 内部按 scene.getUseMeshDistanceField()
            // 二选一的正交开关, 不是分派到两个不同的 Pass——两条路径共用同一套命中着色逻辑,
            // 见 GlobalDistanceFieldVisualizerPass 的注释
            GlobalDistanceFieldVisualizerPass::addToRenderGraph(renderGraph, rendererContext, scene, view, camera);
        }
        else
        {
            // 后处理相关的持久 GPU 资源(TAA/AO history、Color Grading LUT、AutoExposure buffer)
            // 都在这个对象上, 不在 CameraProxy 上——见 PostProcessState 类注释
            PostProcessState& postProcess = camera.getPostProcessState();

            if (postProcess.getPostProcessSettings().ambientOcclusion.enabled)
            {
                AOPass::addToRenderGraph(renderGraph, rendererContext, view, postProcess);
            }

            // 每盏投影的方向光各生成一张独立的 ShadowMask(命名已经按 light+camera 分开)——这次只
            // 生成纹理, 不做多光合并, 也还没接进 DirectLightingPass 的实际光照计算
            for (DirectionalLightProxy* directionalLight : scene.getDirectionalLightProxies())
            {
                if (!directionalLight->isVisible() || !directionalLight->isCastShadows())
                    continue;
                ShadowMaskPass::addToRenderGraph(renderGraph, rendererContext, scene, view, camera, *directionalLight);
            }

            DirectLightingPass::addToRenderGraph(renderGraph, rendererContext, scene, view, renderData, activeAtmosphere, camera);

            // Lumen 间接光生效: Lumen 开启, 并且可视化模式是 None 或者叠在正常画面上的屏幕探针调试模式
            const bool screenProbeDebugTrace = visualizeMode == SceneVisualizeMode::ScreenProbeRadiance
                || visualizeMode == SceneVisualizeMode::ScreenProbeIrradiance;
            const bool lumenGIActive = scene.getLumenEnabled()
                && (visualizeMode == SceneVisualizeMode::None
                    || visualizeMode == SceneVisualizeMode::ScreenProbePlacement
                    || visualizeMode == SceneVisualizeMode::ScreenProbeBRDFPDF
                    || screenProbeDebugTrace);

            // Lumen GI 生效时天光漫反射和镜面反射都由 Lumen 提供
            if (activeSkyLight && !lumenGIActive)
            {
                IndirectLightingPass::addToRenderGraph(renderGraph, rendererContext, scene, view, *activeSkyLight, true);
            }

            // 必须排在 TranslucentPass 和 TAA 之前(TAAPass 在 builder 阶段就翻转 history 读写角色, 排在它后面读到的是这一帧刚写的内容)
            if (lumenGIActive || screenProbeDebugTrace || visualizeMode == SceneVisualizeMode::ScreenProbePlacement
                || visualizeMode == SceneVisualizeMode::ScreenProbeBRDFPDF)
            {
                // 提到这个作用域(而不是紧邻的 if 块内部), 这样下面 if (lumenGIActive) 那个
                // 兄弟分支(反射追踪要用它读 GDF/Card 图集)也能看到——lumenGIActive 蕴含
                // lumenGIActive || screenProbeDebugTrace, 走到下面分支时这里必然已经赋过值
                LumenSceneUpdateResult lumenUpdate;
                ScreenProbePlacementPass::addToRenderGraph(renderGraph, rendererContext, scene, view);
                // 跟 Placement 一起无条件跑, 让 ScreenProbeVisualizerPass 能无条件绑定这个 buffer, 不用另开 dummy storage buffer
                ScreenProbeImportanceSamplingPass::addToRenderGraph(renderGraph, rendererContext, scene, view, postProcess);
                if (lumenGIActive || screenProbeDebugTrace)
                {
                    if (scene.getLumenEnabled())
                    {
                        // true: 屏幕探针追踪需要 per-mesh MDF 图集做 Mesh SDF 近场(M3c), 见
                        // ScreenProbeTrace.slang 的 traceMeshSDFNearField() 调用点
                        lumenUpdate = LumenSceneUpdatePass::addToRenderGraph(renderGraph, rendererContext, scene, camera, true);
                    }
                    ScreenProbeTracePass::addToRenderGraph(renderGraph, rendererContext, scene, view, camera, postProcess, lumenUpdate);
                    ScreenProbeFilteringPass::addToRenderGraph(renderGraph, rendererContext, scene, view);
                    // Lighting PDF(M7.2)历史资源写入, 必须排在这一帧产出 ScreenProbeRadianceFiltered
                    // 的 Filtering pass 之后, 见 ScreenProbeImportanceSamplingPass::addLightingHistoryCopyToRenderGraph 的注释
                    ScreenProbeImportanceSamplingPass::addLightingHistoryCopyToRenderGraph(renderGraph, rendererContext, view, postProcess);
                }
                if (lumenGIActive)
                {
                    ScreenProbeShortRangeAOPass::addToRenderGraph(renderGraph, rendererContext, view);
                    ScreenProbeIntegratePass::addToRenderGraph(renderGraph, rendererContext, scene, view);
                    ScreenProbeTemporalFilterPass::addToRenderGraph(renderGraph, rendererContext, view, postProcess);
                    // Lumen 镜面反射(R1-R5), 见 Document/Lumen_Reflections_Plan.md
                    ReflectionGenerateRaysPass::addToRenderGraph(renderGraph, rendererContext, view);
                    ReflectionTracePass::addToRenderGraph(renderGraph, rendererContext, scene, view, camera, postProcess, lumenUpdate);
                    ReflectionResolvePass::addToRenderGraph(renderGraph, rendererContext, view);
                    ReflectionDenoiserTemporalPass::addToRenderGraph(renderGraph, rendererContext, view, postProcess);
                    ReflectionDenoiserSpatialPass::addToRenderGraph(renderGraph, rendererContext, view);
                    // 合成读专属反射(R5)和粗糙镜面(Integrate), 必须排在它们之后
                    ScreenProbeDiffuseIndirectCompositePass::addToRenderGraph(renderGraph, rendererContext, view);
                }
                // 必须排在这一帧所有读 SceneDepthHistory 的 pass(上面的 Trace pass、TemporalFilterPass)之后,
                // 否则读到的是这一帧刚写的深度而不是上一帧的, 见 ScreenProbeTracePass::addSceneDepthHistoryCopyToRenderGraph 的注释
                if (lumenGIActive || screenProbeDebugTrace)
                {
                    ScreenProbeTracePass::addSceneDepthHistoryCopyToRenderGraph(renderGraph, rendererContext, view, postProcess);
                }
            }

            // 大气合成是 SceneColor = SceneColor * transmittance + inscattering, 乘法部分要等所有光照
            // 都写进 SceneColor 之后才能衰减到它们
            if (activeAtmosphere)
            {
                AtmospherePass::addToRenderGraph(renderGraph, rendererContext, scene, view, *activeAtmosphere);
            }

            if (!renderData.translucentBucket->empty())
            {
                TranslucentPass::addToRenderGraph(renderGraph, rendererContext, scene, view, renderData, activeSkyLight, activeAtmosphere);
            }

            // 在 tonemap 之前(线性 HDR 空间)做时域抗锯齿, 邻域颜色包围盒在线性空间夹得更准。只有
            // camera.isJitterEnabled() 时才有意义——没开 TAA 的相机不产生抖动, PostProcessState 的
            // history 纹理也不会被创建, 硬插这个 pass 只会白白多一次全屏采样
            if (camera.isJitterEnabled())
            {
                TAAPass::addToRenderGraph(renderGraph, rendererContext, view, postProcess);
            }

            // "SceneColorHalfRes" 是给后面 AutoExposurePass(直方图统计)和 BloomPass(降采样金字塔
            // 起点)共用的半分辨率图, 在这里统一算一次、存进 blackboard, 这两个 pass 都不知道
            // DownsamplePass 的存在, 只从 blackboard 里取现成的
            DownsamplePass::addToRenderGraph(renderGraph, rendererContext, view);

            // 关闭时整个不调用——ColorGradingPass 读到的是 PostProcessState::prepareAutoExposureData()
            // 写的初始值 float4(1,1,0,1), exposureCorrection 天然等于 1, 等价于零曝光修正,
            // 不需要额外的开关分支
            if (postProcess.getPostProcessSettings().exposure.enabled)
            {
                AutoExposurePass::addToRenderGraph(renderGraph, rendererContext, scene, view, postProcess);
            }

            // BloomPass 直接从 blackboard 读 DownsamplePass 已经算好的 "SceneColorHalfRes",
            // 不重复降采样。关闭时整个不调用——ColorGradingPass 那边会在 "BloomTexture" 缺失时
            // 退回默认黑纹理, 天然等价于零强度, 不需要额外的开关分支
            if (postProcess.getPostProcessSettings().bloom.enabled)
            {
                BloomPass::addToRenderGraph(renderGraph, rendererContext, view, postProcess);
            }

            ColorGradingPass::addToRenderGraph(renderGraph, rendererContext, view, postProcess);

            GSplatScene* gsplatScene = scene.getGSplatScene();
            if (gsplatScene && gsplatScene->getTotalSplatCount())
            {
                GSplatPass::addToRenderGraph(renderGraph, rendererContext, scene, view, *gsplatScene);
            }

            // 整条链路正常跑完之后, 最后一步把 "SceneColor" 换成屏幕追踪调试可视化——上面
            // 这些 pass(尤其 TAA)必须真的执行过, 采样到的才是这一帧真实的最终画面, 不是
            // 空数据或者旧帧冻结的残留
            if (visualizeMode == SceneVisualizeMode::ScreenTraceRadiance)
            {
                ScreenTraceVisualizerPass::addToRenderGraph(renderGraph, rendererContext, scene, view);
            }
            else if (visualizeMode == SceneVisualizeMode::ScreenProbePlacement
                || visualizeMode == SceneVisualizeMode::ScreenProbeRadiance
                || visualizeMode == SceneVisualizeMode::ScreenProbeIrradiance
                || visualizeMode == SceneVisualizeMode::ScreenProbeBRDFPDF)
            {
                // 探针标记要叠在真实画面上才能看出跟物体轮廓的空间关系, 同一个理由跟
                // ScreenTraceRadiance 一样必须等正常管线跑完
                ScreenProbeVisualizerPass::addToRenderGraph(renderGraph, rendererContext, scene, view);
            }
        }

        // 默认 RT 由 swapchain 的 PresentPass 消费。自定义 RT 可以直接呈现，
        // 或先呈现到瞬时纹理，再通过隔离的 CopyTexture pass 写入目标。

        if (view.getFinalRTName() == gDefaultRTName)
        {
            PresentPass::addToRenderGraph(renderGraph, rendererContext, view);
        }
        else if (view.getOutputRouting() == OutputRouting::Direct_Write)
        {
            // for Editor RenderTarget
            PresentToRenderTargetPass::addToRenderGraph(renderGraph, rendererContext, view);
        }
        else if (view.getOutputRouting() == OutputRouting::Copy_Resolve)
        {
            // for Camera RenderTarget
            CopyToRenderTargetPass::addToRenderGraph(renderGraph, rendererContext, view);
        }
    }

    void CascadeShadowPipeline::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        std::span<const SceneView> views,
        std::span<const RenderData> renderData,
        const DirectionalLightProxy& directionalLight,
        const CameraProxy& camera
    )
    {
        size_t totalBucketCount = 0;
        for (auto& rd : renderData)
        {
            totalBucketCount += rd.shadowDepthBucket->size();
        }
        if (!totalBucketCount)
            return;
        CascadeShadowDepthPass::addToRenderGraph(renderGraph, rendererContext, scene, views, renderData, directionalLight, camera);
    }

}
