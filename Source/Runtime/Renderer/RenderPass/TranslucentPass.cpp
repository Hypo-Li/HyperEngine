#include "TranslucentPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>

#include <LightVolumeInject.generated.h>

namespace hyp
{
    // 光照体积纹理的视锥体分辨率——第一版所有体素写的都是同一份天光 SH 结果(见
    // LightVolumeInject.slang 顶部注释), 分辨率本身现在还不影响任何可见效果, 数值选得比较保守,
    // 等真正接入有空间变化的局部光源时再按需调整。numthreads(4,4,4) 要求三个维度都能被 4 整除
    static constexpr uint32_t kLightVolumeDimX = 32;
    static constexpr uint32_t kLightVolumeDimY = 16;
    static constexpr uint32_t kLightVolumeDimZ = 24;

    struct TranslucentData
    {
        std::shared_ptr<DrawItemBucket> translucentBucket;
        const SceneProxy* scene;
        uint32_t sceneId;
        uint32_t viewId;
        float4 viewport;
        rg::TextureHandle sceneColor;
        rg::TextureHandle sceneDepth;
        bool hasLightVolume;
        rg::TextureHandle lightVolumeR;
        rg::TextureHandle lightVolumeG;
        rg::TextureHandle lightVolumeB;
        bool hasSpecularPrefilter;
        rg::TextureHandle specularPrefilter;
    };

    struct ForwardLightingConstants
    {
        uint32_t lightIndex0;
        uint32_t lightIndex1;
        uint32_t lightCount;
    };

    // TranslucentPass 专属的 scene-and-view bind group: 跟 RendererContext::
    // getSceneAndViewBindGroupLayout() 那份公共的完全独立, 前 4 个 entry 内容照抄(复用同一批
    // buffer, 见 SceneProxy::getInstanceDataBuffer 等), 多出的几个 entry 是光照体积纹理(漫反射
    // 间接光)+ 天光镜面反射预滤波 cubemap/DfgLut(镜面反射间接光, 复用 SkyLightProxy 已经算好的
    // 结果, 跟 SkyLighting.slang 给不透明物体用的是同一份数据, 只是走了一条独立的 bind group)。
    // 没有走公共 set1 的原因: 这些纹理按 view 变化, 公共 bind group 靠 ViewData 的 dynamic offset
    // 避免逐 view 重建, 塞进去会连累所有其它 pass 一起逐 view 重建; 而且写光照体积纹理的注入 pass
    // 也需要绑定普通的 scene/view 数据(不含这张纹理, 它是写入目标, 不是采样对象), 两者混在同一个
    // 公共 bind group 里会出现"同一个 set 里又读又写同一张纹理"的冲突。详见跟用户讨论确认的方案。
    struct TranslucentPassStorage
    {
        TRef<rhi::BindGroupLayout> sceneAndViewLayout;
        std::vector<BindGroupCache<11>> perViewCaches;
    };

    struct LightVolumeInjectData
    {
        uint32_t sceneId;
        rg::BufferHandle packedSHCoeff;
        rg::TextureHandle volumeR;
        rg::TextureHandle volumeG;
        rg::TextureHandle volumeB;
    };

    struct LightVolumeInjectStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> passLayout;
        std::vector<BindGroupCache<4>> perSceneCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    namespace
    {
        TRef<rhi::BindGroupLayout> createTranslucentSceneAndViewLayout(BindGroupLayoutCache& cache)
        {
            std::vector<rhi::BindGroupLayoutEntry> entries(11);

            entries[0].binding = 0;
            entries[0].type = rhi::BindingType::Storage_Buffer;
            entries[0].access = rhi::BindingAccess::Read_Only;
            entries[0].count = 1;
            entries[0].stageFlags = rhi::ShaderStage::All;

            entries[1].binding = 1;
            entries[1].type = rhi::BindingType::Storage_Buffer;
            entries[1].access = rhi::BindingAccess::Read_Only;
            entries[1].count = 1;
            entries[1].stageFlags = rhi::ShaderStage::All;

            entries[2].binding = 2;
            entries[2].type = rhi::BindingType::Storage_Buffer;
            entries[2].access = rhi::BindingAccess::Read_Only;
            entries[2].count = 1;
            entries[2].stageFlags = rhi::ShaderStage::All;

            entries[3].binding = 3;
            entries[3].type = rhi::BindingType::Dynamic_Storage_Buffer;
            entries[3].access = rhi::BindingAccess::Read_Only;
            entries[3].count = 1;
            entries[3].stageFlags = rhi::ShaderStage::All;

            // 4/5/6 = 光照体积纹理的 R/G/B 三个通道(各一个 1 阶 SH 系数 float4), 7 是它们共用的
            // 采样器——1 阶 SH 每个通道一个 float4(xyz=线性系数, w=常数项), 3 个通道装不进一张
            // float4 纹理, 只能拆成 3 张各自独立三线性过滤的 Texture3D, 见 LightVolumeInject.slang
            for (uint32_t i = 0; i < 3; ++i)
            {
                rhi::BindGroupLayoutEntry& entry = entries[4 + i];
                entry.binding = 4 + i;
                entry.type = rhi::BindingType::Sampled_Texture;
                entry.access = rhi::BindingAccess::Read_Only;
                entry.count = 1;
                entry.textureViewType = rhi::TextureViewType::Texture_View_3D;
                entry.textureSampleType = rhi::TextureSampleType::Float;
                entry.stageFlags = rhi::ShaderStage::Fragment;
            }

            entries[7].binding = 7;
            entries[7].type = rhi::BindingType::Sampler;
            entries[7].access = rhi::BindingAccess::Read_Only;
            entries[7].count = 1;
            entries[7].stageFlags = rhi::ShaderStage::Fragment;

            entries[8].binding = 8;
            entries[8].type = rhi::BindingType::Sampled_Texture;
            entries[8].access = rhi::BindingAccess::Read_Only;
            entries[8].count = 1;
            entries[8].textureViewType = rhi::TextureViewType::Texture_View_Cube;
            entries[8].textureSampleType = rhi::TextureSampleType::Float;
            entries[8].stageFlags = rhi::ShaderStage::Fragment;

            entries[9].binding = 9;
            entries[9].type = rhi::BindingType::Sampled_Texture;
            entries[9].access = rhi::BindingAccess::Read_Only;
            entries[9].count = 1;
            entries[9].textureViewType = rhi::TextureViewType::Texture_View_2D;
            entries[9].textureSampleType = rhi::TextureSampleType::Float;
            entries[9].stageFlags = rhi::ShaderStage::Fragment;

            entries[10].binding = 10;
            entries[10].type = rhi::BindingType::Sampler;
            entries[10].access = rhi::BindingAccess::Read_Only;
            entries[10].count = 1;
            entries[10].stageFlags = rhi::ShaderStage::Fragment;

            return cache.getOrCreate(entries);
        }

        rhi::PipelineLayout* createTranslucentPipelineLayout(
            RendererContext& rendererContext,
            const rhi::BindGroupLayout& sceneAndViewLayout,
            const rhi::BindGroupLayout* materialLayout
        )
        {
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
            PipelineCache::PipelineLayoutKey key;
            key.bindGroupLayouts[0] = &rendererContext.getGlobalDataBindGroupLayout();
            key.bindGroupLayouts[1] = &sceneAndViewLayout;
            key.bindGroupLayouts[2] = materialLayout;
            key.bindGroupLayouts[3] = nullptr;
            return pipelineCache.getOrCreate(key);
        }

        // 只在场景有 active SkyLight 时才加进 render graph——没有天光时 TranslucentPass 直接绑
        // DefaultResources 的黑色占位 3D 纹理, 不需要跑这个 pass, 也不需要在 shader 里判断
        void addLightVolumeInjectPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            SkyLightProxy& skyLight
        )
        {
            auto packedSHCoeff = renderGraph.importBuffer(
                "TranslucentLightVolumeSrcSH"_hs(scene.getSceneId()),
                &skyLight.getPackedSHBuffer()
            );

            renderGraph.addComputePass<LightVolumeInjectData>(
                "LightVolumeInject"_hs,
                [&](rg::RenderGraph::Builder& builder, LightVolumeInjectData& data)
                {
                    builder.markSideEffect();

                    data.sceneId = scene.getSceneId();
                    data.packedSHCoeff = builder.accessBuffer(packedSHCoeff, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

                    rhi::TextureDesc volumeDesc;
                    volumeDesc.type = rhi::TextureType::Texture_3D;
                    volumeDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                    volumeDesc.extent = { kLightVolumeDimX, kLightVolumeDimY, kLightVolumeDimZ };
                    volumeDesc.mipCount = 1;
                    volumeDesc.layerCount = 1;
                    volumeDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;

                    // 1 阶 SH 每个体素是 3 个 float4(R/G/B 各一个), 装不进一张 float4 纹理, 拆成
                    // 3 张独立的 Texture3D——builder.storage() 直接操作的是完整纹理句柄(没有经过
                    // createSubTexture 拆子资源), 不需要 syncTextures, 直接放进 blackboard 就是
                    // 后续 pass 能用的新版本号
                    auto volumeR = builder.createTexture("TranslucentLightVolumeR"_hs(data.sceneId), volumeDesc);
                    data.volumeR = builder.storage(volumeR, rg::AccessType::Write_Only);
                    builder.getBlackboard().put("TranslucentLightVolumeR"_hs(data.sceneId), data.volumeR);

                    auto volumeG = builder.createTexture("TranslucentLightVolumeG"_hs(data.sceneId), volumeDesc);
                    data.volumeG = builder.storage(volumeG, rg::AccessType::Write_Only);
                    builder.getBlackboard().put("TranslucentLightVolumeG"_hs(data.sceneId), data.volumeG);

                    auto volumeB = builder.createTexture("TranslucentLightVolumeB"_hs(data.sceneId), volumeDesc);
                    data.volumeB = builder.storage(volumeB, rg::AccessType::Write_Only);
                    builder.getBlackboard().put("TranslucentLightVolumeB"_hs(data.sceneId), data.volumeB);
                },
                [&rendererContext](rg::ExecuteContext& context, LightVolumeInjectData& data)
                {
                    static auto& storage = rendererContext.getPassStorage().getOrCreate<TranslucentPass, LightVolumeInjectStorage>();
                    auto& renderBackend = context.getRenderBackend();

                    if (!storage.shader)
                    {
                        storage.shader = std::make_shared<PassShader>(
                            renderBackend.getDevice(),
                            "LightVolumeInject",
                            makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(LightVolumeInject, Compute))
                        );
                    }

                    if (!storage.passLayout)
                    {
                        storage.passLayout = createPassBindGroupLayout(renderBackend, generated::kLightVolumeInject_Bindings);
                    }

                    if (storage.perSceneCaches.size() <= data.sceneId)
                    {
                        storage.perSceneCaches.resize(data.sceneId + 1);
                    }
                    auto& bindGroupCache = storage.perSceneCaches[data.sceneId];

                    rhi::Buffer* packedSHCoeff = context.getBuffer(data.packedSHCoeff);
                    rhi::TextureView* volumeR = context.getTexture(data.volumeR);
                    rhi::TextureView* volumeG = context.getTexture(data.volumeG);
                    rhi::TextureView* volumeB = context.getTexture(data.volumeB);

                    if (bindGroupCache.needUpdate(packedSHCoeff, volumeR, volumeG, volumeB))
                    {
                        bindGroupCache.update(
                            renderBackend,
                            *storage.passLayout,
                            [&](rhi::BindGroup& bindGroup)
                            {
                                PassBindGroupWriter writer(generated::kLightVolumeInject_Bindings);
                                writer.setBuffer("SkyLightPackedSHCoeff", packedSHCoeff);
                                writer.setTexture("LightVolumeR", volumeR);
                                writer.setTexture("LightVolumeG", volumeG);
                                writer.setTexture("LightVolumeB", volumeB);
                                writer.apply(bindGroup);
                            }
                        );
                    }

                    if (!storage.pipeline)
                    {
                        storage.pipeline = createComputePipeline(
                            rendererContext,
                            storage.shader->getVariant(),
                            *storage.passLayout
                        );
                    }

                    auto& cmd = context.getCommandList();
                    cmd.bindPipeline(storage.pipeline.get());
                    cmd.setBindGroup(
                        2,
                        rhi::PipelineBindPoint::Compute,
                        &storage.pipeline->getLayout(),
                        bindGroupCache.getBindGroup(),
                        {}
                    );

                    struct { uint32_t dimensions[3]; } immediateData = { { kLightVolumeDimX, kLightVolumeDimY, kLightVolumeDimZ } };
                    cmd.setImmediateData(
                        &storage.pipeline->getLayout(),
                        0,
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&immediateData), sizeof(immediateData))
                    );
                    cmd.dispatch(kLightVolumeDimX / 4, kLightVolumeDimY / 4, kLightVolumeDimZ / 4);
                }
            );
        }
    }

    void TranslucentPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const RenderData& renderData,
        SkyLightProxy* activeSkyLight
    )
    {
        if (activeSkyLight)
        {
            addLightVolumeInjectPass(renderGraph, rendererContext, scene, *activeSkyLight);
        }

        // 天光镜面反射复用 SkyLightProxy 常驻的 prefilter cubemap(SkyLighting.slang 给不透明
        // 物体用的是同一份), 用独立的 import 名字(不是 IndirectLightingPass 用的那个), 避免
        // 两个 pass 对同一份纹理各自 import 时互相冲突
        TextureProxy* specularPrefilter = activeSkyLight ? activeSkyLight->getSpecularPrefilter() : nullptr;
        rg::TextureHandle specularPrefilterHandle;
        if (specularPrefilter)
        {
            specularPrefilterHandle = renderGraph.importTexture(
                "TranslucentSkyLightPrefilter"_hs(scene.getSceneId()),
                &specularPrefilter->getTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
        }

        renderGraph.addGraphicsPass<TranslucentData>(
            "Translucent"_hs,
            [&](rg::RenderGraph::Builder& builder, TranslucentData& data)
            {
                data.translucentBucket = renderData.translucentBucket;
                data.scene = &scene;
                data.sceneId = scene.getSceneId();
                data.viewId = view.getViewId();
                data.viewport = view.getViewport();

                uint32_t viewId = view.getViewId();
                rg::Blackboard& blackboard = builder.getBlackboard();
                rg::TextureHandle sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(viewId));
                data.sceneColor = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Load);
                blackboard.put("SceneColor"_hs(viewId), data.sceneColor);

                // 只做深度测试(跟不透明物体正确遮挡), 不写深度——半透明物体已经按距离从远到近
                // 排过序(makeTranslucentSortKey), 互相之间的前后关系靠绘制顺序 + blend 保证,
                // 不需要也不应该写深度
                rg::TextureHandle sceneDepth = blackboard.get<rg::TextureHandle>("SceneDepth"_hs(viewId));
                data.sceneDepth = builder.addDepthStencilAttachment(sceneDepth, rhi::AttachmentLoadOp::Load, false, rhi::AttachmentLoadOp::Load, false);

                data.hasLightVolume = activeSkyLight != nullptr;
                if (data.hasLightVolume)
                {
                    auto lightVolumeR = blackboard.get<rg::TextureHandle>("TranslucentLightVolumeR"_hs(data.sceneId));
                    data.lightVolumeR = builder.sample(lightVolumeR);
                    auto lightVolumeG = blackboard.get<rg::TextureHandle>("TranslucentLightVolumeG"_hs(data.sceneId));
                    data.lightVolumeG = builder.sample(lightVolumeG);
                    auto lightVolumeB = blackboard.get<rg::TextureHandle>("TranslucentLightVolumeB"_hs(data.sceneId));
                    data.lightVolumeB = builder.sample(lightVolumeB);
                }

                data.hasSpecularPrefilter = specularPrefilter != nullptr;
                if (data.hasSpecularPrefilter)
                {
                    data.specularPrefilter = builder.sample(specularPrefilterHandle);
                }
            },
            [&rendererContext](rg::ExecuteContext& context, TranslucentData& data)
            {
                if (data.translucentBucket->empty())
                {
                    context.beginPass();
                    return;
                }

                auto& storage = rendererContext.getPassStorage().getOrCreate<TranslucentPass, TranslucentPassStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.sceneAndViewLayout)
                {
                    storage.sceneAndViewLayout = createTranslucentSceneAndViewLayout(renderBackend.getBindGroupLayoutCache());
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& sceneAndViewCache = storage.perViewCaches[data.viewId];

                auto& defaultResources = renderBackend.getDefaultResources();
                // 没有 light volume 时, R/G/B 三个 slot 都指向同一份黑色占位纹理——反正是只读采样,
                // 三个 binding 共享同一个物理资源没有问题
                const rhi::TextureView* lightVolumeR = data.hasLightVolume
                    ? context.getTexture(data.lightVolumeR)
                    : &defaultResources.getBlackTexture3DView();
                const rhi::TextureView* lightVolumeG = data.hasLightVolume
                    ? context.getTexture(data.lightVolumeG)
                    : &defaultResources.getBlackTexture3DView();
                const rhi::TextureView* lightVolumeB = data.hasLightVolume
                    ? context.getTexture(data.lightVolumeB)
                    : &defaultResources.getBlackTexture3DView();
                const rhi::TextureView* specularPrefilter = data.hasSpecularPrefilter
                    ? context.getTexture(data.specularPrefilter)
                    : &defaultResources.getBlackTextureCubeView();
                const rhi::TextureView* dfgLut = &defaultResources.getDfgLutView();
                const rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                const rhi::Sampler* linearClampMipSampler = renderBackend.getSamplerCache().getLinearClampMip();

                if (sceneAndViewCache.needUpdate(
                        &data.scene->getIndicesBuffer(),
                        &data.scene->getInstanceDataBuffer(),
                        &data.scene->getLightDataBuffer(),
                        &rendererContext.getViewDataManager().getViewDataBuffer(),
                        lightVolumeR,
                        lightVolumeG,
                        lightVolumeB,
                        linearClampNoMipSampler,
                        specularPrefilter,
                        dfgLut,
                        linearClampMipSampler
                    ))
                {
                    sceneAndViewCache.update(
                        renderBackend,
                        *storage.sceneAndViewLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            rhi::BufferBindingInfo indicesBinding{ &data.scene->getIndicesBuffer(), 0, rhi::kWholeBuffer };
                            rhi::BufferBindingInfo instanceDataBinding{ &data.scene->getInstanceDataBuffer(), 0, rhi::kWholeBuffer };
                            rhi::BufferBindingInfo lightDataBinding{ &data.scene->getLightDataBuffer(), 0, rhi::kWholeBuffer };
                            rhi::BufferBindingInfo viewDataBinding{ &rendererContext.getViewDataManager().getViewDataBuffer(), 0, sizeof(ViewData) };

                            rhi::BindGroupEntry entries[11] = {};
                            entries[0] = { 0, rhi::BindingType::Storage_Buffer, 0, { &indicesBinding, 1 }, {}, {} };
                            entries[1] = { 1, rhi::BindingType::Storage_Buffer, 0, { &instanceDataBinding, 1 }, {}, {} };
                            entries[2] = { 2, rhi::BindingType::Storage_Buffer, 0, { &lightDataBinding, 1 }, {}, {} };
                            entries[3] = { 3, rhi::BindingType::Dynamic_Storage_Buffer, 0, { &viewDataBinding, 1 }, {}, {} };
                            entries[4] = { 4, rhi::BindingType::Sampled_Texture, 0, {}, { &lightVolumeR, 1 }, {} };
                            entries[5] = { 5, rhi::BindingType::Sampled_Texture, 0, {}, { &lightVolumeG, 1 }, {} };
                            entries[6] = { 6, rhi::BindingType::Sampled_Texture, 0, {}, { &lightVolumeB, 1 }, {} };
                            entries[7] = { 7, rhi::BindingType::Sampler, 0, {}, {}, { &linearClampNoMipSampler, 1 } };
                            entries[8] = { 8, rhi::BindingType::Sampled_Texture, 0, {}, { &specularPrefilter, 1 }, {} };
                            entries[9] = { 9, rhi::BindingType::Sampled_Texture, 0, {}, { &dfgLut, 1 }, {} };
                            entries[10] = { 10, rhi::BindingType::Sampler, 0, {}, {}, { &linearClampMipSampler, 1 } };
                            bindGroup.update({ entries, 11 });
                        }
                    );
                }

                HString renderTag = BuiltinRenderTags::Translucent;
                auto& cmd = context.getCommandList();

                // Translucent Bucket 按距离排序, 相邻 item 大概率材质不同, 没有 GBufferPass 那种
                // "相邻且材质/geometry/section 都相同就合并成一次 instanced draw" 的空间, 每个
                // item 单独一次 draw, 但仍然复用同一套 indices buffer 上传机制
                std::vector<uint32_t> instanceIndices;
                instanceIndices.reserve(data.translucentBucket->size());
                for (const auto& sortedDrawItem : *data.translucentBucket)
                {
                    instanceIndices.push_back(sortedDrawItem.drawItem.instanceIndex);
                }

                auto& indicesBuffer = data.scene->getIndicesBuffer();
                size_t indicesBufferSize = instanceIndices.size() * sizeof(uint32_t);
                auto allocation = rendererContext.getRenderBackend().getStagingAllocator().forceAllocate(indicesBufferSize);
                std::memcpy(allocation.mappedData, instanceIndices.data(), indicesBufferSize);

                rhi::BufferBarrier preBarrier;
                preBarrier.buffer = &indicesBuffer;
                preBarrier.offset = 0;
                preBarrier.size = indicesBufferSize;
                preBarrier.oldState = rhi::ResourceState::Shader_Storage_Read;
                preBarrier.newState = rhi::ResourceState::Transfer_Dst;
                cmd.insertBarriers({ &preBarrier, 1 });

                rhi::BufferCopyRegion copyRegion;
                copyRegion.srcOffset = allocation.offset;
                copyRegion.dstOffset = 0;
                copyRegion.size = indicesBufferSize;
                cmd.copyBuffer(allocation.buffer, &indicesBuffer, { &copyRegion, 1 });

                rhi::BufferBarrier postBarrier;
                postBarrier.buffer = &indicesBuffer;
                postBarrier.offset = 0;
                postBarrier.size = indicesBufferSize;
                postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
                postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
                cmd.insertBarriers({ &postBarrier, 1 });

                context.beginPass();

                setViewportAndScissor(cmd, data.viewport);

                uint32_t viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(data.viewId);

                // 第一版只做太阳(方向光)直接光照, 最多取 2 盏——跟 AtmosphereParameters 的
                // sun0/sun1 约定一致, 见 Translucent.slang 顶部注释
                ForwardLightingConstants forwardLighting{};
                auto directionalLights = data.scene->getDirectionalLightProxies();
                forwardLighting.lightCount = std::min<uint32_t>(static_cast<uint32_t>(directionalLights.size()), 2);
                if (forwardLighting.lightCount > 0)
                    forwardLighting.lightIndex0 = directionalLights[0]->getLightIndex();
                if (forwardLighting.lightCount > 1)
                    forwardLighting.lightIndex1 = directionalLights[1]->getLightIndex();

                rhi::Pipeline* currentPipeline = nullptr;
                MaterialProxy* prevMaterial = nullptr;
                GeometryProxy* prevGeometry = nullptr;
                for (uint32_t i = 0; i < data.translucentBucket->size(); ++i)
                {
                    const auto& drawItem = (*data.translucentBucket)[i].drawItem;

                    VertexLayout vertexLayout = drawItem.geometry->getVertexLayout();
                    rhi::Pipeline* pipeline = drawItem.material->getCachedPipeline(renderTag, vertexLayout);
                    if (!pipeline)
                    {
                        pipeline = createPipeline(rendererContext, *drawItem.material, renderTag, vertexLayout, *storage.sceneAndViewLayout);
                        drawItem.material->cachePipeline(renderTag, vertexLayout, pipeline);
                    }
                    if (pipeline != currentPipeline)
                    {
                        cmd.bindPipeline(pipeline);
                        currentPipeline = pipeline;
                        cmd.setBindGroup(
                            1,
                            rhi::PipelineBindPoint::Graphics,
                            &currentPipeline->getLayout(),
                            sceneAndViewCache.getBindGroup(),
                            { &viewDataDynamicOffset, 1 }
                        );
                        cmd.setImmediateData(&currentPipeline->getLayout(), 0, std::span((const uint8_t*)&forwardLighting, sizeof(forwardLighting)));
                        prevMaterial = nullptr;
                    }

                    if (drawItem.material != prevMaterial)
                    {
                        const rhi::BindGroup* parametersBindGroup = drawItem.material->getParametersBindGroup();
                        if (parametersBindGroup)
                        {
                            cmd.setBindGroup(
                                2,
                                rhi::PipelineBindPoint::Graphics,
                                &currentPipeline->getLayout(),
                                parametersBindGroup,
                                {}
                            );
                        }
                        prevMaterial = drawItem.material;
                    }

                    if (drawItem.geometry != prevGeometry)
                    {
                        drawItem.geometry->bind(cmd);
                        prevGeometry = drawItem.geometry;
                    }

                    drawItem.geometry->draw(cmd, drawItem.sectionIndex, 1, i);
                }
            },
            rg::PassFlags::Skip_Begin_Pass
        );
    }

    rhi::Pipeline* TranslucentPass::createPipeline(
        RendererContext& rendererContext,
        MaterialProxy& material,
        HString renderTag,
        VertexLayout vertexLayout,
        const rhi::BindGroupLayout& sceneAndViewLayout
    )
    {
        auto shaderVariant = material.getOrCreateShaderVariant(renderTag, vertexLayout);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        const rhi::BindGroup* materialParameterBindGroup = material.getParametersBindGroup();
        auto* pipelineLayout = createTranslucentPipelineLayout(
            rendererContext,
            sceneAndViewLayout,
            materialParameterBindGroup ? &materialParameterBindGroup->getLayout() : nullptr
        );

        GraphicsPipelineState pipelineState;
        pipelineState.topology = static_cast<uint32_t>(rhi::PrimitiveTopology::Triangle_List);
        pipelineState.polygonMode = static_cast<uint32_t>(rhi::PolygonMode::Fill);
        pipelineState.samples = 0;
        pipelineState.cullMode = static_cast<uint32_t>(material.getAttributes().doubleSided ? rhi::CullMode::None : rhi::CullMode::Back);
        pipelineState.frontFace = static_cast<uint32_t>(rhi::FrontFace::Counter_Clockwise);
        pipelineState.depthTestEnable = 1;
        pipelineState.depthWriteEnable = 0;
        pipelineState.depthCompareOp = static_cast<uint32_t>(rhi::CompareOp::Greater_Equal);
        pipelineState.stencilTestEnable = 0;
        pipelineState.stencilState = StencilState::get();
        // 预乘 alpha 混合: (One, OneMinusSrcAlpha)——Translucent.slang 的 FragmentMain 已经把
        // 高光项以"满强度"(不乘 alpha)混进 Src 颜色, 这里的混合系数不需要、也不应该再对它做
        // 任何按 alpha 的衰减, 具体推导见跟用户讨论确认的方案
        pipelineState.colorBlendState = ColorBlendState::get<
            rhi::ColorBlendAttachmentState{ true, rhi::BlendFactor::One, rhi::BlendFactor::One_Minus_Src_Alpha, rhi::BlendOp::Add, rhi::BlendFactor::One, rhi::BlendFactor::One_Minus_Src_Alpha, rhi::BlendOp::Add, 0xF }
        >();
        pipelineState.shader = shaderVariant;
        pipelineState.vertexLayout = vertexLayout;
        pipelineState.attachmentFormats = AttachmentFormats::get<
            rhi::TextureFormat::D32_SFLOAT_S8_UINT,
            rhi::TextureFormat::RGBA16_SFLOAT
        >();

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = pipelineState;
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
