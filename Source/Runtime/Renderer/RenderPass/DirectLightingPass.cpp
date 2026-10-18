#include "DirectLightingPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>
#include <RenderBase/VertexLayout.h>

#include <DirectLighting.generated.h>
#include <LightVolume.generated.h>
#include <CopyDepth.generated.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace hyp
{
    struct DirectLightingData
    {
        const SceneProxy* scene;
        uint32_t sceneId;
        uint32_t viewId;
        float4 viewport;
        bool hasAtmosphere;
        // 本帧场景里是否存在点光源/聚光灯——决定了这个 pass 有没有挂 lightVolumeDepth 附件, 从而决定
        // 要用 DirectLightingStorage::pipelines 里的哪一份缓存管线(两者附件集合不同)
        bool hasLocalLightVolume;
        rg::TextureHandle sceneColor;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle gbufferD;
        rg::TextureHandle sceneDepthD;
        // 点光源体积几何专用的深度测试附件——SceneDepth 拷贝出来的独立副本(见
        // addLightVolumeDepthCopyPass), 不是 SceneDepth/SceneDepthD 本身。SceneDepthD 是
        // SceneDepth 的子视图(同一张 VkImage), 这个 pass 里方向光那几个 draw 全程在采样
        // SceneDepthD——如果直接把 SceneDepth 自己绑成这个 pass 的深度附件, 会跟 SceneDepthD
        // 的采样用途在同一个 pass 里对同一张图产生 layout 冲突(实测触发过 Vulkan 校验层报错)。
        // 拷贝出来的这张是完全独立的资源, 只有光源体积球用得到, 没有这个问题
        rg::TextureHandle lightVolumeDepth;
        rg::TextureHandle transmittanceLut;
        // 按方向光的 getIndex() 下标存, 不是按循环位置——和 ShadowMaskPass 用同一个身份维度。
        // 没有有效遮罩的下标(没开 Cast Shadows, 或者数组没覆盖到)保持默认(无效)handle
        std::vector<rg::TextureHandle> shadowMasks;
        // SceneRenderer::cull() 按这个 view 的视锥体剔过一遍的点光源/聚光灯, 不是
        // scene->getLocalLightProxies() 的全量——包围球不在视锥体内的灯不需要画
        std::shared_ptr<std::vector<LocalLightProxy*>> localLights;
    };

    struct DirectLightingStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        // 外层按 viewId、内层按光源 getIndex() 各自 resize-on-demand——ShadowMask 是每盏光
        // 各自不同的纹理, 不能再像其它 6 个资源那样全场景共用一个 bind group
        std::vector<std::vector<BindGroupCache<10>>> perViewPerLightCaches;
        // 按 hasLocalLightVolume 分两份缓存——两种状态下这个 pass 挂的附件集合不同(有没有
        // lightVolumeDepth), pipeline 的 attachmentFormats 必须跟当帧 render pass 实际绑定的
        // 附件集合一致, 不能只缓存一份跨帧复用
        std::array<TRef<rhi::Pipeline>, 2> pipelines;

        // 点光源/聚光灯专用: 光源体积球/锥几何 + 相机在体积内部时退化用的全屏三角形。两套 cache 各自
        // 按 getLightIndex() 存(全局光源索引, 跨光源类型唯一)——不能用 getIndex(), 那是按
        // ProxyType 各自分配的池, Point/Spot 这类不同类型的灯完全可能撞上相同的 getIndex()
        std::shared_ptr<PassShader> lightVolumeShader;
        TRef<rhi::BindGroupLayout> lightVolumeBindGroupLayout;
        TRef<rhi::Pipeline> lightVolumePipeline;
        std::vector<std::vector<BindGroupCache<6>>> perViewPerLocalLightVolumeCaches;
        std::vector<std::vector<BindGroupCache<10>>> perViewPerLocalLightFallbackCaches;
    };

    struct CopyDepthData
    {
        const SceneProxy* scene;
        uint32_t viewId;
        rg::TextureHandle src;
        rg::TextureHandle dst;
        float4 viewport;
    };

    struct CopyDepthStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<1>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    namespace
    {
        rhi::Pipeline* createCopyDepthPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout
        )
        {
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
            // CopyDepth 只读一张纹理, 不需要 ViewData/场景数据, 跟 PresentToRenderTargetPass
            // 用 CopyTexture 的方式一样, 只用一个槽位放自己这份 BindGroupLayout
            PipelineCache::PipelineLayoutKey layoutKey{};
            layoutKey.bindGroupLayouts[0] = &bindGroupLayout;
            TRef<rhi::PipelineLayout> pipelineLayout = pipelineCache.getOrCreateLayout(layoutKey);

            GraphicsPipelineState pipelineState = PipelineStatePresets::fullscreenTriangle();
            pipelineState.shader = shaderVariant;
            // depthTestEnable 必须是 1——Vulkan 规定 depthWriteEnable 只在 depthTestEnable=true
            // 时才生效, 关掉深度测试的话深度写入根本不会发生。compareOp=Always 让测试恒通过,
            // 这里要的只是"写入"这个副作用, 不是真的要做比较
            pipelineState.depthTestEnable = 1;
            pipelineState.depthWriteEnable = 1;
            pipelineState.depthCompareOp = static_cast<uint32_t>(rhi::CompareOp::Always);
            pipelineState.attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::D32_SFLOAT>();

            PipelineCache::GraphicsKey pipelineKey;
            pipelineKey.pipelineState = pipelineState;
            pipelineKey.pipelineLayout = pipelineLayout;
            return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
        }

        // LightVolumeDepth 用远大于默认冷却期(TransientResources::kDefaultMaxUnusedFrames = 3
        // 帧)的冷却时间——它是否需要存在跟着场景里点光源的有无逐帧变化, 而不是分辨率/画质设置这种
        // 帧间稳定的东西, 玩家进出一个有点光源的房间这种时间尺度上很容易连续好几百帧不需要它,
        // 默认冷却期扛不住这种波动、会导致反复创建销毁, 这里给够量级避免那种显存分配抖动
        constexpr uint32_t kLightVolumeDepthMaxUnusedFrames = 300;

        // 把 SceneDepthD 整张拷进一份独立的深度纹理, 专给光源体积球当硬件深度测试附件用——见
        // DirectLightingData::lightVolumeDepth 的注释。只有场景里有点光源时才需要这份拷贝
        rg::TextureHandle addLightVolumeDepthCopyPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view
        )
        {
            uint32_t viewId = view.getViewId();
            rg::TextureHandle result;
            renderGraph.addGraphicsPass<CopyDepthData>(
                "CopyLightVolumeDepth"_hs,
                [&](rg::RenderGraph::Builder& builder, CopyDepthData& data)
                {
                    data.scene = &scene;
                    data.viewId = viewId;
                    data.viewport = view.getViewport();
                    auto& blackboard = builder.getBlackboard();
                    rg::TextureHandle sceneDepthD = blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId));
                    data.src = builder.sample(sceneDepthD);

                    rhi::TextureDesc depthDesc;
                    depthDesc.type = rhi::TextureType::Texture_2D;
                    depthDesc.format = rhi::TextureFormat::D32_SFLOAT;
                    depthDesc.extent = { static_cast<uint32_t>(data.viewport.z), static_cast<uint32_t>(data.viewport.w), 1 };
                    depthDesc.mipCount = 1;
                    depthDesc.layerCount = 1;
                    depthDesc.usage = rhi::TextureUsage::Depth_Stencil_Attachment | rhi::TextureUsage::Sampled;
                    auto dst = builder.createTexture("LightVolumeDepth"_hs(viewId), depthDesc, kLightVolumeDepthMaxUnusedFrames);
                    data.dst = builder.addDepthStencilAttachment(dst, rhi::AttachmentLoadOp::Dont_Care, true, rhi::AttachmentLoadOp::Dont_Care, false);
                    result = data.dst;
                },
                [&rendererContext](rg::ExecuteContext& context, CopyDepthData& data)
                {
                    static auto& storage = rendererContext.getPassStorage().getOrCreate<DirectLightingPass, CopyDepthStorage>();
                    auto& renderBackend = context.getRenderBackend();

                    if (!storage.shader)
                    {
                        storage.shader = std::make_shared<PassShader>(
                            renderBackend.getDevice(),
                            "CopyDepth",
                            makeVertexFragmentBytecode(
                                renderBackend.getRHIBackend(),
                                GET_PASS_SHADER_BYTECODE(CopyDepth, Vertex),
                                GET_PASS_SHADER_BYTECODE(CopyDepth, Fragment)
                            )
                        );
                    }
                    if (!storage.bindGroupLayout)
                    {
                        storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kCopyDepth_Bindings);
                    }
                    if (!storage.pipeline)
                    {
                        storage.pipeline = createCopyDepthPipeline(rendererContext, storage.shader->getVariant(), *storage.bindGroupLayout);
                    }

                    const rhi::TextureView* src = context.getTexture(data.src);
                    if (storage.perViewCaches.size() <= data.viewId)
                    {
                        storage.perViewCaches.resize(data.viewId + 1);
                    }
                    auto& bindGroupCache = storage.perViewCaches[data.viewId];
                    if (bindGroupCache.needUpdate(src))
                    {
                        bindGroupCache.update(
                            renderBackend,
                            *storage.bindGroupLayout,
                            [&](rhi::BindGroup& bindGroup)
                            {
                                PassBindGroupWriter writer(generated::kCopyDepth_Bindings);
                                writer.setTexture("SourceDepthImage", src);
                                writer.apply(bindGroup);
                            }
                        );
                    }

                    auto& cmdList = context.getCommandList();
                    context.beginPass();
                    setViewportAndScissor(cmdList, data.viewport);
                    cmdList.bindPipeline(storage.pipeline.get());
                    cmdList.setBindGroup(0, rhi::PipelineBindPoint::Graphics, &storage.pipeline->getLayout(), bindGroupCache.getBindGroup(), {});
                    cmdList.draw(3, 1, 0, 0);

                    // 这个 pipeline layout 只有 1 个槽位——Vulkan 的描述符集兼容性规则是, 只要
                    // 对 set0 调用一次 vkCmdBindDescriptorSets, set0 之后所有更高编号的 set(这里
                    // 是 set1, 场景/视图数据)都会被判定为 disturbed(失效), 不会因为紧接着把
                    // set0 换回全局默认就自动连带恢复——set1 必须重新显式绑定一次, 否则本 pass
                    // 后面画方向光/点光源那几个 draw(它们全程不会自己重绑 set1, 指望的是
                    // "Prepare" pass 那次绑定还有效)会在 Vulkan 后端触发 set1 未绑定的校验错误。
                    // WebGPU 后端因为有 mPendingGraphicsBindGroups 重放机制, 不会暴露这个问题。
                    auto& globalBindings = renderBackend.getGlobalBindings();
                    cmdList.setBindGroup(0, rhi::PipelineBindPoint::Graphics, &globalBindings.getPipelineLayout(), &globalBindings.getGlobalDataBindGroup(), {});
                    uint32_t dynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(data.viewId);
                    cmdList.setBindGroup(1, rhi::PipelineBindPoint::Graphics, &rendererContext.getGlobalPipelineLayout(), &rendererContext.getViewDataManager().getBindGroup(), { &dynamicOffset, 1 });
                },
                rg::PassFlags::Skip_Begin_Pass
            );
            return result;
        }
    }

    void DirectLightingPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const RenderData& renderData,
        const AtmosphereProxy* atmosphere,
        const CameraProxy& camera
    )
    {
        // 光源体积球的硬件深度测试专用, 见 addLightVolumeDepthCopyPass()/
        // DirectLightingData::lightVolumeDepth 的注释。只在这个 view 视锥体内确实有点光源/聚光灯
        // (SceneRenderer::cull() 剔过的 renderData.visibleLocalLights, 不是场景全量)时才建这份
        // 拷贝/挂这个附件——LightVolumeDepth 自己用远大于默认冷却期的 maxUnusedFrames(见
        // addLightVolumeDepthCopyPass), 点光源短暂消失不会导致它被立刻销毁重建。这个 pass 的
        // 附件集合因此按 hasLocalLightVolume 分两种状态, DirectLightingStorage::pipelines
        // 相应按这个状态缓存两份, 不能只缓存一份跨帧复用
        bool hasLocalLightVolume = !renderData.localLights->empty();
        rg::TextureHandle lightVolumeDepth;
        if (hasLocalLightVolume)
        {
            lightVolumeDepth = addLightVolumeDepthCopyPass(renderGraph, rendererContext, scene, view);
        }

        renderGraph.addGraphicsPass<DirectLightingData>(
            "DirectLighting"_hs,
            [&](rg::RenderGraph::Builder& builder, DirectLightingData& data)
            {
                data.scene = &scene;
                data.sceneId = scene.getSceneId();
                data.viewId = view.getViewId();
                data.viewport = view.getViewport();
                data.hasAtmosphere = atmosphere != nullptr;
                data.hasLocalLightVolume = hasLocalLightVolume;
                data.localLights = renderData.localLights;
                auto& blackboard = builder.getBlackboard();
                rg::TextureHandle sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(data.viewId));
                data.sceneColor = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Load);
                blackboard.put("SceneColor"_hs(data.viewId), data.sceneColor);
                rg::TextureHandle gbufferA = blackboard.get<rg::TextureHandle>("GBufferA"_hs(data.viewId));
                data.gbufferA = builder.sample(gbufferA);
                rg::TextureHandle gbufferB = blackboard.get<rg::TextureHandle>("GBufferB"_hs(data.viewId));
                data.gbufferB = builder.sample(gbufferB);
                rg::TextureHandle gbufferC = blackboard.get<rg::TextureHandle>("GBufferC"_hs(data.viewId));
                data.gbufferC = builder.sample(gbufferC);
                rg::TextureHandle gbufferD = blackboard.get<rg::TextureHandle>("GBufferD"_hs(data.viewId));
                data.gbufferD = builder.sample(gbufferD);
                rg::TextureHandle sceneDepthD = blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(data.viewId));
                data.sceneDepthD = builder.sample(sceneDepthD);

                if (hasLocalLightVolume)
                {
                    data.lightVolumeDepth = builder.addDepthStencilAttachment(lightVolumeDepth, rhi::AttachmentLoadOp::Load, false, rhi::AttachmentLoadOp::Dont_Care, false);
                }

                if (data.hasAtmosphere)
                {
                    auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(data.sceneId));
                    data.transmittanceLut = builder.sample(transmittanceLut);
                }

                for (DirectionalLightProxy* directionalLight : scene.getDirectionalLightProxies())
                {
                    if (!directionalLight->isVisible())
                        continue;

                    HString shadowMaskName = directionalLight->getShadowMaskName(&camera);
                    if (shadowMaskName.empty())
                        continue;
                    rg::TextureHandle shadowMask = blackboard.get<rg::TextureHandle>(shadowMaskName);
                    if (!shadowMask.isValid())
                        continue;

                    uint32_t proxyIndex = directionalLight->getIndex();
                    if (data.shadowMasks.size() <= proxyIndex)
                    {
                        data.shadowMasks.resize(proxyIndex + 1);
                    }
                    data.shadowMasks[proxyIndex] = builder.sample(shadowMask);
                }
            },
            [&rendererContext, atmosphere, &camera](rg::ExecuteContext& context, DirectLightingData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<DirectLightingPass, DirectLightingStorage>();

                auto& renderBackend = context.getRenderBackend();

                if (storage.perViewPerLightCaches.size() <= data.viewId)
                {
                    storage.perViewPerLightCaches.resize(data.viewId + 1);
                }
                auto& perLightCaches = storage.perViewPerLightCaches[data.viewId];

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "DirectLighting",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(DirectLighting, Vertex),
                            GET_PASS_SHADER_BYTECODE(DirectLighting, Fragment)
                        )
                    );
                }

                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* gbufferC = context.getTexture(data.gbufferC);
                const rhi::TextureView* gbufferD = context.getTexture(data.gbufferD);
                const rhi::TextureView* sceneDepthD = context.getTexture(data.sceneDepthD);

                // 场景没有大气时绑 DefaultResources 的通用占位资源(全零 buffer + 全白纹理)。
                // DirectLighting.slang 里靠 topRadiusKm <= bottomRadiusKm(全零时天然成立)判断
                // 出"没有真实大气", 整段透过率计算直接跳过、transmittance 恒为 1, 不依赖占位 buffer
                // 里有什么具体内容

                auto& defaultResources = renderBackend.getDefaultResources();
                rhi::Buffer* atmosphereParameters = data.hasAtmosphere
                    ? &atmosphere->getParametersBuffer()
                    : &defaultResources.getDummyUniformBuffer();
                const rhi::TextureView* transmittanceLut = data.hasAtmosphere
                    ? context.getTexture(data.transmittanceLut)
                    : &defaultResources.getWhiteTextureView();
                rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kDirectLighting_Bindings);
                }

                TRef<rhi::Pipeline>& pipeline = storage.pipelines[data.hasLocalLightVolume];
                if (!pipeline)
                {
                    pipeline = createPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout,
                        data.hasLocalLightVolume
                    );
                }

                auto& cmdList = context.getCommandList();

                context.beginPass();

                setViewportAndScissor(cmdList, data.viewport);

                cmdList.bindPipeline(pipeline.get());

                for (auto directionalLightProxy : data.scene->getDirectionalLightProxies())
                {
                    if (!directionalLightProxy->isVisible())
                        continue;

                    uint32_t proxyIndex = directionalLightProxy->getIndex();

                    // 没开 Cast Shadows(或者这个相机这一帧没算出有效遮罩)的光, 用全白占位纹理
                    // 代替——等效于完全没有遮挡, 和"没有大气"那个分支是同一个道理
                    const rhi::TextureView* shadowMask = (proxyIndex < data.shadowMasks.size() && data.shadowMasks[proxyIndex].isValid())
                        ? context.getTexture(data.shadowMasks[proxyIndex])
                        : &defaultResources.getWhiteTextureView();

                    if (perLightCaches.size() <= proxyIndex)
                    {
                        perLightCaches.resize(proxyIndex + 1);
                    }
                    auto& bindGroupCache = perLightCaches[proxyIndex];

                    const rhi::Buffer* lightDataBuffer = &data.scene->getLightDataBuffer();

                    if (bindGroupCache.needUpdate(gbufferA, gbufferB, gbufferC, gbufferD, sceneDepthD, atmosphereParameters, transmittanceLut, linearClampNoMipSampler, shadowMask, lightDataBuffer))
                    {
                        bindGroupCache.update(
                            renderBackend,
                            *storage.bindGroupLayout,
                            [&](rhi::BindGroup& bindGroup)
                            {
                                PassBindGroupWriter writer(generated::kDirectLighting_Bindings);
                                writer.setTexture("GbufferAImage", gbufferA);
                                writer.setTexture("GbufferBImage", gbufferB);
                                writer.setTexture("GbufferCImage", gbufferC);
                                writer.setTexture("GbufferDImage", gbufferD);
                                writer.setTexture("SceneDepthImage", sceneDepthD);
                                writer.setBuffer("Atmosphere", atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                                writer.setTexture("TransmittanceLutTexture", transmittanceLut);
                                writer.setSampler("LutSampler", linearClampNoMipSampler);
                                writer.setTexture("ShadowMaskImage", shadowMask);
                                writer.setBuffer("sLightData", lightDataBuffer);
                                writer.apply(bindGroup);
                            }
                        );
                    }

                    cmdList.setBindGroup(
                        2,
                        rhi::PipelineBindPoint::Graphics,
                        &pipeline->getLayout(),
                        bindGroupCache.getBindGroup(),
                        {}
                    );
                    cmdList.setBindGroup(3, rhi::PipelineBindPoint::Graphics, &pipeline->getLayout(), &data.scene->getInstanceDataBindGroup(), {});

                    uint32_t lightIndex = directionalLightProxy->getLightIndex();
                    cmdList.setImmediateData(&pipeline->getLayout(), 0, std::span((const uint8_t*)&lightIndex, sizeof(lightIndex)));
                    cmdList.draw(3, 1, 0, 0);
                }

                if (data.hasLocalLightVolume)
                {
                    if (!storage.lightVolumeShader)
                    {
                        storage.lightVolumeShader = std::make_shared<PassShader>(
                            renderBackend.getDevice(),
                            "LightVolume",
                            makeVertexFragmentBytecode(
                                renderBackend.getRHIBackend(),
                                GET_PASS_SHADER_BYTECODE(LightVolume, Vertex),
                                GET_PASS_SHADER_BYTECODE(LightVolume, Fragment)
                            )
                        );
                    }
                    if (!storage.lightVolumeBindGroupLayout)
                    {
                        storage.lightVolumeBindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kLightVolume_Bindings);
                    }
                    if (!storage.lightVolumePipeline)
                    {
                        storage.lightVolumePipeline = createLightVolumePipeline(
                            rendererContext,
                            storage.lightVolumeShader->getVariant(),
                            *storage.lightVolumeBindGroupLayout
                        );
                    }

                    if (storage.perViewPerLocalLightVolumeCaches.size() <= data.viewId)
                    {
                        storage.perViewPerLocalLightVolumeCaches.resize(data.viewId + 1);
                    }
                    auto& localLightVolumeCaches = storage.perViewPerLocalLightVolumeCaches[data.viewId];
                    if (storage.perViewPerLocalLightFallbackCaches.size() <= data.viewId)
                    {
                        storage.perViewPerLocalLightFallbackCaches.resize(data.viewId + 1);
                    }
                    auto& localLightFallbackCaches = storage.perViewPerLocalLightFallbackCaches[data.viewId];

                    const rhi::TextureView* whiteTexture = &defaultResources.getWhiteTextureView();
                    const rhi::Buffer* lightDataBuffer = &data.scene->getLightDataBuffer();

                    // 光源体积几何跟全屏三角形不共用一条 pipeline barrier/管线状态, 相机是否
                    // 在这盏光内部逐光源判断, 每盏光各自选一条路径, 不需要在两条路径之间切换
                    // render pass
                    double4x4 viewMatrix = camera.getViewMatrix();
                    double3 cameraWorldPos = double3(glm::inverse(viewMatrix)[3]);

                    for (auto localLightProxy : *data.localLights)
                    {
                        uint32_t proxyIndex = localLightProxy->getLightIndex();
                        float attenuationRadius = localLightProxy->getAttenuationRadius();
                        double distanceToCamera = Math::length(localLightProxy->getPosition() - cameraWorldPos);
                        // 留一点余量: 近裁剪面附近球背面可能被裁掉, 稍微提前退化成全屏三角形
                        bool cameraInsideVolume = distanceToCamera < double(attenuationRadius) * 1.1;

                        if (cameraInsideVolume)
                        {
                            if (localLightFallbackCaches.size() <= proxyIndex)
                            {
                                localLightFallbackCaches.resize(proxyIndex + 1);
                            }
                            auto& bindGroupCache = localLightFallbackCaches[proxyIndex];

                            if (bindGroupCache.needUpdate(gbufferA, gbufferB, gbufferC, gbufferD, sceneDepthD, atmosphereParameters, transmittanceLut, linearClampNoMipSampler, whiteTexture, lightDataBuffer))
                            {
                                bindGroupCache.update(
                                    renderBackend,
                                    *storage.bindGroupLayout,
                                    [&](rhi::BindGroup& bindGroup)
                                    {
                                        PassBindGroupWriter writer(generated::kDirectLighting_Bindings);
                                        writer.setTexture("GbufferAImage", gbufferA);
                                        writer.setTexture("GbufferBImage", gbufferB);
                                        writer.setTexture("GbufferCImage", gbufferC);
                                        writer.setTexture("GbufferDImage", gbufferD);
                                        writer.setTexture("SceneDepthImage", sceneDepthD);
                                        writer.setBuffer("Atmosphere", atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                                        writer.setTexture("TransmittanceLutTexture", transmittanceLut);
                                        writer.setSampler("LutSampler", linearClampNoMipSampler);
                                        writer.setTexture("ShadowMaskImage", whiteTexture);
                                        writer.setBuffer("sLightData", lightDataBuffer);
                                        writer.apply(bindGroup);
                                    }
                                );
                            }

                            cmdList.bindPipeline(pipeline.get());
                            cmdList.setBindGroup(2, rhi::PipelineBindPoint::Graphics, &pipeline->getLayout(), bindGroupCache.getBindGroup(), {});
                            cmdList.setBindGroup(3, rhi::PipelineBindPoint::Graphics, &pipeline->getLayout(), &data.scene->getInstanceDataBindGroup(), {});
                            cmdList.setImmediateData(&pipeline->getLayout(), 0, std::span((const uint8_t*)&proxyIndex, sizeof(proxyIndex)));
                            cmdList.draw(3, 1, 0, 0);
                        }
                        else
                        {
                            if (localLightVolumeCaches.size() <= proxyIndex)
                            {
                                localLightVolumeCaches.resize(proxyIndex + 1);
                            }
                            auto& bindGroupCache = localLightVolumeCaches[proxyIndex];

                            if (bindGroupCache.needUpdate(gbufferA, gbufferB, gbufferC, gbufferD, sceneDepthD, lightDataBuffer))
                            {
                                bindGroupCache.update(
                                    renderBackend,
                                    *storage.lightVolumeBindGroupLayout,
                                    [&](rhi::BindGroup& bindGroup)
                                    {
                                        PassBindGroupWriter writer(generated::kLightVolume_Bindings);
                                        writer.setTexture("GbufferAImage", gbufferA);
                                        writer.setTexture("GbufferBImage", gbufferB);
                                        writer.setTexture("GbufferCImage", gbufferC);
                                        writer.setTexture("GbufferDImage", gbufferD);
                                        writer.setTexture("SceneDepthDImage", sceneDepthD);
                                        writer.setBuffer("sLightData", lightDataBuffer);
                                        writer.apply(bindGroup);
                                    }
                                );
                            }

                            cmdList.bindPipeline(storage.lightVolumePipeline.get());
                            cmdList.setBindGroup(2, rhi::PipelineBindPoint::Graphics, &storage.lightVolumePipeline->getLayout(), bindGroupCache.getBindGroup(), {});
                            cmdList.setBindGroup(3, rhi::PipelineBindPoint::Graphics, &storage.lightVolumePipeline->getLayout(), &data.scene->getInstanceDataBindGroup(), {});

                            double4 positionVSDouble = viewMatrix * double4(localLightProxy->getPosition(), 1.0);
                            // 字段顺序理由见 LightVolume.slang 里 LightVolumePushConstants 的注释——
                            // float3 摆在 16 字节边界起点, 不能随便调整顺序
                            struct LightVolumePushConstants
                            {
                                float3 positionVS;
                                uint32_t lightIndex;
                            };

                            LightVolumePushConstants pushConstants{
                                float3(positionVSDouble.x, positionVSDouble.y, positionVSDouble.z),
                                proxyIndex
                            };
                            cmdList.setImmediateData(&storage.lightVolumePipeline->getLayout(), 0, std::span((const uint8_t*)&pushConstants, sizeof(pushConstants)));

                            // 顶点位置由 LightVolume.slang 按 SV_VertexID 现算(真实
                            // attenuationRadius/outerConeAngle, 不是烘焙单位网格再缩放近似), 不需要
                            // 绑定顶点/索引 buffer——这两个 vertexCount 常量要跟 LightVolume.slang 里
                            // kConeSides/kConeSlices/kSphereSides/kSphereRings 手动保持一致
                            bool isSpot = data.scene->getLightData(proxyIndex).type == LightType::Spot;
                            constexpr uint32_t kConeVertexCount = 2 * (DefaultResources::kLightVolumeConeSlices - 1) * DefaultResources::kLightVolumeConeSides * 6;
                            constexpr uint32_t kSphereVertexCount = DefaultResources::kLightVolumeSphereRings * DefaultResources::kLightVolumeSphereSegments * 6;
                            cmdList.draw(isSpot ? kConeVertexCount : kSphereVertexCount, 1, 0, 0);
                        }
                    }
                }
            },
            rg::PassFlags::Skip_Begin_Pass
        );
    }

    rhi::Pipeline* DirectLightingPass::createPipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& bindGroupLayout,
        bool hasLightVolumeDepth
    )
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr, &rendererContext.getInstanceDataBindGroupLayout());
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::fullscreenTriangle();
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.colorBlendState = ColorBlendState::get<
            rhi::ColorBlendAttachmentState{ true, rhi::BlendFactor::One, rhi::BlendFactor::One, rhi::BlendOp::Add, rhi::BlendFactor::Zero, rhi::BlendFactor::One, rhi::BlendOp::Add, 0xF }
        >();
        // depthTestEnable/depthWriteEnable 都是 0(继承自 fullscreenTriangle() 预设)——这条
        // 全屏三角形管线本身不测深度, attachmentFormats 只是要跟 render pass 当帧实际绑定的
        // 附件集合保持一致(hasLightVolumeDepth 为 false 时这个 pass 没有挂 LightVolumeDepth 附件)
        pipelineKey.pipelineState.attachmentFormats = hasLightVolumeDepth
            ? AttachmentFormats::get<rhi::TextureFormat::D32_SFLOAT, rhi::TextureFormat::RGBA16_SFLOAT>()
            : AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA16_SFLOAT>();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

    rhi::Pipeline* DirectLightingPass::createLightVolumePipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& bindGroupLayout
    )
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr, &rendererContext.getInstanceDataBindGroupLayout());
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        // 画光源体积球/锥体的背面: 反向 Z(近=1远=0), 场景深度比球背面更近(数值更大)才说明这个
        // 像素落在光源体积内部, 所以是 Less——把着色范围限制在光源体积的屏幕投影内。测的是
        // LightVolumeDepth(SceneDepthD 拷贝出来的独立副本,
        // 见 addLightVolumeDepthCopyPass), 不是 SceneDepth/SceneDepthD 本身, 避免跟这个 pass
        // 里方向光那几个 draw 采样 SceneDepthD 产生同一张图的 layout 冲突。相机在体积内部的
        // 情况在 execute lambda 里按距离退化成 DirectLightingPass::createPipeline() 那条全屏
        // 三角形路径, 不靠这条管线处理。cullMode 是 Back 不是 Front——LightVolume.slang 里
        // 现算的球体/锥体三角形环绕方向经坐标验证是"内朝"的(叉积法线指向内部, 不是外部), 剔除
        // Back 才能留下真正朝外那一面(背对相机的那一半), 剔 Front 会把该留的那一半剔掉、留下
        // 面向相机的近面, 让下面这个深度比较对任何相机在体积外部的情形都恒为假
        GraphicsPipelineState pipelineState;
        pipelineState.topology = static_cast<uint32_t>(rhi::PrimitiveTopology::Triangle_List);
        pipelineState.samples = 0;
        pipelineState.cullMode = static_cast<uint32_t>(rhi::CullMode::Back);
        pipelineState.frontFace = static_cast<uint32_t>(rhi::FrontFace::Counter_Clockwise);
        pipelineState.depthTestEnable = 1;
        pipelineState.depthWriteEnable = 0;
        pipelineState.depthCompareOp = static_cast<uint32_t>(rhi::CompareOp::Less);
        pipelineState.stencilTestEnable = 0;
        pipelineState.stencilState = StencilState::get();
        pipelineState.colorBlendState = ColorBlendState::get<
            rhi::ColorBlendAttachmentState{ true, rhi::BlendFactor::One, rhi::BlendFactor::One, rhi::BlendOp::Add, rhi::BlendFactor::Zero, rhi::BlendFactor::One, rhi::BlendOp::Add, 0xF }
        >();
        pipelineState.shader = shaderVariant;
        pipelineState.attachmentFormats = AttachmentFormats::get<
            rhi::TextureFormat::D32_SFLOAT,
            rhi::TextureFormat::RGBA16_SFLOAT
        >();

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = pipelineState;
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

}
