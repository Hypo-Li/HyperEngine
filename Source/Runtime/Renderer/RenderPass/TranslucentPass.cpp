#include "TranslucentPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>
#include <Core/Common/LogSystem.h>

#include <LightVolumeInject.generated.h>

namespace hyp
{
    // 光照体积纹理的视锥体分辨率——第一版所有体素写的都是同一份天光 SH 结果(见
    // LightVolumeInject.slang 顶部注释), 分辨率本身现在还不影响任何可见效果, 数值选得比较保守,
    // 等真正接入有空间变化的局部光源时再按需调整。numthreads(4,4,4) 要求三个维度都能被 4 整除
    static constexpr uint32_t kLightVolumeDimX = 32;
    static constexpr uint32_t kLightVolumeDimY = 16;
    static constexpr uint32_t kLightVolumeDimZ = 24;

    // TAA 用: 半透明物体既不写深度也不写 Velocity(见 GbufferPass.cpp), depth/motion 两条路径都
    // 找不到它们, resolve pass 没法靠常规的重投影+disocclusion 检测识别这些像素——所以改成在
    // stencil 里直接标记, TAAPass 读到这一位就知道要对这个像素做特殊处理(丢弃/大幅降低历史权重,
    // 具体见 TAA.slang, 思路上对应 UE TAACommon.ush 里 STENCIL_TEMPORAL_RESPONSIVE_AA_MASK 那一类
    // "这个像素没有可靠运动矢量"的处理)。这个值必须和 TAA.slang 里读 stencil 时用的掩码保持一致。
    static constexpr uint32_t kTAAStencilBit = 0x01;

    struct TranslucentData
    {
        std::shared_ptr<DrawItemList> drawItems;
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
        bool hasSkyLightData;
        rg::BufferHandle skyLightData;
        bool hasAtmosphere;
        rg::TextureHandle transmittanceLut;
        // 逐 instance 的点光源下标表(header+payload 拼在一个 buffer 里, CSR 布局), builder
        // 阶段整个算完存这里、execute 阶段原样上传——见 addToRenderGraph 顶部的详细注释
        rg::BufferHandle instanceLightIndices;
        std::vector<uint32_t> instanceLightIndicesContent;
    };

    struct ForwardLightingConstants
    {
        uint32_t lightIndex0;
        uint32_t lightIndex1;
        uint32_t lightCount;
    };

    // TranslucentPass 专属的 set1 bind group: 跟 ViewDataManager::getBindGroupLayout()
    // 那份公共的完全独立, ViewData 那个 entry 内容照抄(复用同一块 buffer, 见
    // ViewDataManager::getViewDataBuffer), 多出的几个 entry 是光源数据、光照体积纹理(漫反射
    // 间接光)+ 天光镜面反射预滤波 cubemap/DfgLut(镜面反射间接光, 复用 SkyLightProxy 已经算好的
    // 结果, 跟 SkyLighting.slang 给不透明物体用的是同一份数据, 只是走了一条独立的 bind group)。
    // 没有走公共 set1 的原因: 这些纹理按 view 变化, 公共 bind group 靠 ViewData 的 dynamic offset
    // 避免逐 view 重建, 塞进去会连累所有其它 pass 一起逐 view 重建; 而且写光照体积纹理的注入 pass
    // 也需要绑定普通的 scene/view 数据(不含这张纹理, 它是写入目标, 不是采样对象), 两者混在同一个
    // 公共 bind group 里会出现"同一个 set 里又读又写同一张纹理"的冲突。
    struct TranslucentPassStorage
    {
        TRef<rhi::BindGroupLayout> viewLayout;
        std::vector<BindGroupCache<13>> perViewCaches;
    };

    namespace
    {
        // 按 2 的幂取整——InstanceLightIndices buffer 每帧内容大小都会变(哪盏光照到哪个 instance
        // 会变), 如果每次都精确按用量建 buffer, TransientResources 的 BufferKey(含 desc.size)
        // 逐帧对不上, 池化形同虚设(建了就废、下一帧又建一个新的)。取整到同一个阶梯内 size 不变,
        // 才能真正复用。
        uint32_t roundUpToPowerOfTwo(uint32_t value)
        {
            uint32_t result = 1;
            while (result < value)
                result <<= 1;
            return result;
        }
    }

    struct LightVolumeInjectData
    {
        uint32_t sceneId;
        rg::BufferHandle skyLightData;
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
        TRef<rhi::BindGroupLayout> createTranslucentViewLayout(BindGroupLayoutCache& cache)
        {
            std::vector<rhi::BindGroupLayoutEntry> entries(13);

            // binding 0 必须是 uViewData——Binding.slang 里 [[vk::binding(0, 1)]] 是硬编码的
            // 全局约定(任何 include 了它的 shader 都这么认), 不能被这个 pass 自己的资源占掉
            entries[0].binding = 0;
            entries[0].type = rhi::BindingType::Dynamic_Uniform_Buffer;
            entries[0].access = rhi::BindingAccess::Read_Only;
            entries[0].count = 1;
            entries[0].stageFlags = rhi::ShaderStage::All;

            entries[1].binding = 2;
            entries[1].type = rhi::BindingType::Storage_Buffer;
            entries[1].access = rhi::BindingAccess::Read_Only;
            entries[1].count = 1;
            entries[1].stageFlags = rhi::ShaderStage::All;

            // 3/4/5 = 光照体积纹理的 R/G/B 三个通道(各一个 1 阶 SH 系数 float4), 6 是它们共用的
            // 采样器——1 阶 SH 每个通道一个 float4(xyz=线性系数, w=常数项), 3 个通道装不进一张
            // float4 纹理, 只能拆成 3 张各自独立三线性过滤的 Texture3D, 见 LightVolumeInject.slang
            for (uint32_t i = 0; i < 3; ++i)
            {
                rhi::BindGroupLayoutEntry& entry = entries[2 + i];
                entry.binding = 3 + i;
                entry.type = rhi::BindingType::Sampled_Texture;
                entry.access = rhi::BindingAccess::Read_Only;
                entry.count = 1;
                entry.textureViewType = rhi::TextureViewType::Texture_View_3D;
                entry.textureSampleType = rhi::TextureSampleType::Float;
                entry.stageFlags = rhi::ShaderStage::Fragment;
            }

            entries[5].binding = 6;
            entries[5].type = rhi::BindingType::Sampler;
            entries[5].access = rhi::BindingAccess::Read_Only;
            entries[5].count = 1;
            entries[5].stageFlags = rhi::ShaderStage::Fragment;

            entries[6].binding = 7;
            entries[6].type = rhi::BindingType::Sampled_Texture;
            entries[6].access = rhi::BindingAccess::Read_Only;
            entries[6].count = 1;
            entries[6].textureViewType = rhi::TextureViewType::Texture_View_Cube;
            entries[6].textureSampleType = rhi::TextureSampleType::Float;
            entries[6].stageFlags = rhi::ShaderStage::Fragment;

            entries[7].binding = 8;
            entries[7].type = rhi::BindingType::Sampled_Texture;
            entries[7].access = rhi::BindingAccess::Read_Only;
            entries[7].count = 1;
            entries[7].textureViewType = rhi::TextureViewType::Texture_View_2D;
            entries[7].textureSampleType = rhi::TextureSampleType::Float;
            entries[7].stageFlags = rhi::ShaderStage::Fragment;

            entries[8].binding = 9;
            entries[8].type = rhi::BindingType::Sampler;
            entries[8].access = rhi::BindingAccess::Read_Only;
            entries[8].count = 1;
            entries[8].stageFlags = rhi::ShaderStage::Fragment;

            // 逐 instance 的点光源下标表(InstanceLightIndices, header+payload CSR 布局),
            // 只有 Translucent.slang 的片元阶段读, 见 addToRenderGraph 顶部注释
            entries[9].binding = 10;
            entries[9].type = rhi::BindingType::Storage_Buffer;
            entries[9].access = rhi::BindingAccess::Read_Only;
            entries[9].count = 1;
            entries[9].stageFlags = rhi::ShaderStage::Fragment;

            // 方向光穿过大气到达着色点的透过率, 跟 DirectLightingPass 给不透明物体用的是同一套
            // AtmosphereParameters/TransmittanceLut——采样器复用 6 号 NoMipSampler(本来就是
            // getLinearClampNoMip(), 跟 DirectLighting.slang 的 LutSampler 是同一个采样器),
            // 不用再开一个 sampler binding
            entries[10].binding = 11;
            entries[10].type = rhi::BindingType::Uniform_Buffer;
            entries[10].access = rhi::BindingAccess::Read_Only;
            entries[10].count = 1;
            entries[10].stageFlags = rhi::ShaderStage::Fragment;

            entries[11].binding = 12;
            entries[11].type = rhi::BindingType::Sampled_Texture;
            entries[11].access = rhi::BindingAccess::Read_Only;
            entries[11].count = 1;
            entries[11].textureViewType = rhi::TextureViewType::Texture_View_2D;
            entries[11].textureSampleType = rhi::TextureSampleType::Float;
            entries[11].stageFlags = rhi::ShaderStage::Fragment;

            entries[12].binding = 13;
            entries[12].type = rhi::BindingType::Storage_Buffer;
            entries[12].access = rhi::BindingAccess::Read_Only;
            entries[12].count = 1;
            entries[12].stageFlags = rhi::ShaderStage::Fragment;

            return cache.getOrCreate(entries);
        }

        rhi::PipelineLayout* createTranslucentPipelineLayout(
            RendererContext& rendererContext,
            const rhi::BindGroupLayout& viewLayout,
            const rhi::BindGroupLayout* materialLayout,
            const rhi::BindGroupLayout* instanceLayout
        )
        {
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
            PipelineCache::PipelineLayoutKey key;
            key.bindGroupLayouts[0] = &rendererContext.getGlobalDataBindGroupLayout();
            key.bindGroupLayouts[1] = &viewLayout;
            key.bindGroupLayouts[2] = materialLayout;
            key.bindGroupLayouts[3] = instanceLayout;
            return pipelineCache.getOrCreateLayout(key);
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
            auto skyLightData = renderGraph.importBuffer(
                "TranslucentLightVolumeSrcSH"_hs(scene.getSceneId()),
                &skyLight.getDataBuffer()
            );

            renderGraph.addComputePass<LightVolumeInjectData>(
                "LightVolumeInject"_hs,
                [&](rg::RenderGraph::Builder& builder, LightVolumeInjectData& data)
                {
                    builder.markSideEffect();

                    data.sceneId = scene.getSceneId();
                    data.skyLightData = builder.accessBuffer(skyLightData, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

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

                    rhi::Buffer* skyLightData = context.getBuffer(data.skyLightData);
                    rhi::TextureView* volumeR = context.getTexture(data.volumeR);
                    rhi::TextureView* volumeG = context.getTexture(data.volumeG);
                    rhi::TextureView* volumeB = context.getTexture(data.volumeB);

                    if (bindGroupCache.needUpdate(skyLightData, volumeR, volumeG, volumeB))
                    {
                        bindGroupCache.update(
                            renderBackend,
                            *storage.passLayout,
                            [&](rhi::BindGroup& bindGroup)
                            {
                                PassBindGroupWriter writer(generated::kLightVolumeInject_Bindings);
                                writer.setBuffer("SkyLightData", skyLightData);
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

                    auto& cmdList = context.getCommandList();
                    cmdList.bindPipeline(storage.pipeline.get());
                    cmdList.setBindGroup(
                        2,
                        rhi::PipelineBindPoint::Compute,
                        &storage.pipeline->getLayout(),
                        bindGroupCache.getBindGroup(),
                        {}
                    );

                    struct { uint32_t dimensions[3]; } immediateData = { { kLightVolumeDimX, kLightVolumeDimY, kLightVolumeDimZ } };
                    cmdList.setImmediateData(
                        &storage.pipeline->getLayout(),
                        0,
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&immediateData), sizeof(immediateData))
                    );
                    cmdList.dispatch(kLightVolumeDimX / 4, kLightVolumeDimY / 4, kLightVolumeDimZ / 4);
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
        SkyLightProxy* activeSkyLight,
        const AtmosphereProxy* atmosphere
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

        rg::BufferHandle skyLightDataHandle;
        if (activeSkyLight)
        {
            skyLightDataHandle = renderGraph.importBuffer(
                "TranslucentSkyLightData"_hs(scene.getSceneId()),
                &activeSkyLight->getDataBuffer()
            );
        }

        renderGraph.addGraphicsPass<TranslucentData>(
            "Translucent"_hs,
            [&](rg::RenderGraph::Builder& builder, TranslucentData& data)
            {
                data.drawItems = renderData.drawItems;
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
                // 不需要也不应该写深度。stencil 打开写(kTAAStencilBit), 给 TAAPass 标记"这个像素
                // 没有可靠 Velocity"
                rg::TextureHandle sceneDepth = blackboard.get<rg::TextureHandle>("SceneDepth"_hs(viewId));
                data.sceneDepth = builder.addDepthStencilAttachment(sceneDepth, rhi::AttachmentLoadOp::Load, false, rhi::AttachmentLoadOp::Load, true);

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

                data.hasSkyLightData = activeSkyLight != nullptr;
                if (data.hasSkyLightData)
                {
                    data.skyLightData = builder.accessBuffer(skyLightDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                }

                data.hasAtmosphere = atmosphere != nullptr;
                if (data.hasAtmosphere)
                {
                    auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(data.sceneId));
                    data.transmittanceLut = builder.sample(transmittanceLut);
                }

                // InstanceLightIndices: 逐 instance 的点光源下标表, header+payload 拼进同一个
                // buffer(CSR 布局, header[instanceIndex*2] = payload 里的起始下标(绝对下标,
                // 直接能索引同一个 buffer), header[instanceIndex*2+1] = count, payload 紧跟在
                // header 后面, 是 count 个 lightIndex)。完全在这个 pass 自己的 setup 阶段算完、
                // 建 buffer, 不碰 SceneProxy/update()——只有这个 pass 自己用得到。
                // 每帧全量重算(哪盏光照到哪个 instance 会变, 不适合套 InstanceData/LightData
                // 那种"只在真正变化时才标脏"的增量模型), 但 buffer 大小取整到 2 的幂
                // (roundUpToPowerOfTwo), 避免 TransientResources 的 BufferKey 逐帧对不上导致
                // 池化失效
                if (!renderData.translucentBucket->empty())
                {
                    uint32_t instanceCount = scene.getInstanceCount();
                    std::vector<uint32_t>& content = data.instanceLightIndicesContent;
                    content.assign(static_cast<size_t>(instanceCount) * 2, 0);

                    std::vector<uint32_t> overlappingLights;
                    for (MeshRenderProxy* meshProxy : scene.getMeshRenderProxies())
                    {
                        uint32_t instanceIndex = meshProxy->getInstanceIndex();
                        const Boxd& box = meshProxy->getBoundingBox();

                        overlappingLights.clear();
                        // renderData.localLights 是 SceneRenderer::cull() 按这个 view 视锥体
                        // 剔过一遍的结果, 不是场景全量——包围球不在视锥体内的灯不可能照亮任何可见
                        // 像素, 不需要再逐 mesh 做 box-sphere 测试
                        for (LocalLightProxy* localLightProxy : *renderData.localLights)
                        {
                            double3 lightPos = localLightProxy->getPosition();
                            double radius = static_cast<double>(localLightProxy->getAttenuationRadius());
                            double3 closest = glm::clamp(lightPos, box.getMin(), box.getMax());
                            double3 diff = closest - lightPos;
                            if (glm::dot(diff, diff) <= radius * radius)
                            {
                                overlappingLights.push_back(localLightProxy->getLightIndex());
                            }
                        }

                        if (!overlappingLights.empty())
                        {
                            uint32_t offset = static_cast<uint32_t>(content.size());
                            content[instanceIndex * 2 + 0] = offset;
                            content[instanceIndex * 2 + 1] = static_cast<uint32_t>(overlappingLights.size());
                            content.insert(content.end(), overlappingLights.begin(), overlappingLights.end());
                        }
                    }

                    uint32_t bufferElementCount = roundUpToPowerOfTwo(std::max<uint32_t>(static_cast<uint32_t>(content.size()), 64));
                    auto instanceLightIndices = builder.createBuffer(
                        "TranslucentInstanceLightIndices"_hs(viewId),
                        rhi::BufferDesc{ uint64_t(bufferElementCount) * sizeof(uint32_t), rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst }
                    );
                    data.instanceLightIndices = builder.accessBuffer(instanceLightIndices, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);
                }
            },
            [&rendererContext, atmosphere](rg::ExecuteContext& context, TranslucentData& data)
            {
                if (data.translucentBucket->empty())
                {
                    context.beginPass();
                    return;
                }

                auto& storage = rendererContext.getPassStorage().getOrCreate<TranslucentPass, TranslucentPassStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.viewLayout)
                {
                    storage.viewLayout = createTranslucentViewLayout(renderBackend.getBindGroupLayoutCache());
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& viewCache = storage.perViewCaches[data.viewId];

                rhi::Buffer* instanceLightIndicesBuffer = context.getBuffer(data.instanceLightIndices);
                {
                    auto& uploadCmdList = context.getCommandList();
                    size_t uploadSize = data.instanceLightIndicesContent.size() * sizeof(uint32_t);
                    auto allocation = renderBackend.getStagingAllocator().forceAllocate(uploadSize);
                    std::memcpy(allocation.mappedData, data.instanceLightIndicesContent.data(), uploadSize);

                    rhi::BufferBarrier preBarrier;
                    preBarrier.buffer = instanceLightIndicesBuffer;
                    preBarrier.offset = 0;
                    preBarrier.size = rhi::kWholeBuffer;
                    preBarrier.oldState = rhi::ResourceState::None;
                    preBarrier.newState = rhi::ResourceState::Transfer_Dst;
                    uploadCmdList.insertBarriers({ &preBarrier, 1 });

                    rhi::BufferCopyRegion copyRegion;
                    copyRegion.srcOffset = allocation.offset;
                    copyRegion.dstOffset = 0;
                    copyRegion.size = uploadSize;
                    uploadCmdList.copyBuffer(allocation.buffer, instanceLightIndicesBuffer, { &copyRegion, 1 });

                    rhi::BufferBarrier postBarrier;
                    postBarrier.buffer = instanceLightIndicesBuffer;
                    postBarrier.offset = 0;
                    postBarrier.size = rhi::kWholeBuffer;
                    postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
                    postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
                    uploadCmdList.insertBarriers({ &postBarrier, 1 });
                }

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
                rhi::Buffer* skyLightData = data.hasSkyLightData
                    ? context.getBuffer(data.skyLightData)
                    : &defaultResources.getDummyStorageBuffer();
                const rhi::TextureView* dfgLut = &defaultResources.getDfgLutView();
                const rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                const rhi::Sampler* linearClampMipSampler = renderBackend.getSamplerCache().getLinearClampMip();

                // 没有大气时绑 DefaultResources 的通用占位资源, 靠半径退化(topRadiusKm<=
                // bottomRadiusKm)让 evaluateDirectLight() 自然跳过透过率计算——跟 DirectLightingPass
                // 给不透明物体用的是同一套占位约定, 见那边的注释
                rhi::Buffer* atmosphereParameters = data.hasAtmosphere
                    ? &atmosphere->getParametersBuffer()
                    : &defaultResources.getDummyUniformBuffer();
                const rhi::TextureView* transmittanceLut = data.hasAtmosphere
                    ? context.getTexture(data.transmittanceLut)
                    : &defaultResources.getWhiteTextureView();

                if (viewCache.needUpdate(
                        &data.scene->getLightDataBuffer(),
                        &rendererContext.getViewDataManager().getViewDataBuffer(),
                        lightVolumeR,
                        lightVolumeG,
                        lightVolumeB,
                        linearClampNoMipSampler,
                        specularPrefilter,
                        dfgLut,
                        linearClampMipSampler,
                        instanceLightIndicesBuffer,
                        atmosphereParameters,
                        transmittanceLut,
                        skyLightData
                    ))
                {
                    viewCache.update(
                        renderBackend,
                        *storage.viewLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            rhi::BufferBindingInfo lightDataBinding{ &data.scene->getLightDataBuffer(), 0, rhi::kWholeBuffer };
                            rhi::BufferBindingInfo viewDataBinding{ &rendererContext.getViewDataManager().getViewDataBuffer(), 0, sizeof(ViewData) };
                            rhi::BufferBindingInfo instanceLightIndicesBinding{ instanceLightIndicesBuffer, 0, rhi::kWholeBuffer };
                            rhi::BufferBindingInfo atmosphereBinding{ atmosphereParameters, 0, sizeof(AtmosphereShaderParameters) };
                            rhi::BufferBindingInfo skyLightDataBinding{ skyLightData, 0, rhi::kWholeBuffer };

                            rhi::BindGroupEntry entries[13] = {};
                            entries[0] = { 0, rhi::BindingType::Dynamic_Uniform_Buffer, 0, { &viewDataBinding, 1 }, {}, {} };
                            entries[1] = { 2, rhi::BindingType::Storage_Buffer, 0, { &lightDataBinding, 1 }, {}, {} };
                            entries[2] = { 3, rhi::BindingType::Sampled_Texture, 0, {}, { &lightVolumeR, 1 }, {} };
                            entries[3] = { 4, rhi::BindingType::Sampled_Texture, 0, {}, { &lightVolumeG, 1 }, {} };
                            entries[4] = { 5, rhi::BindingType::Sampled_Texture, 0, {}, { &lightVolumeB, 1 }, {} };
                            entries[5] = { 6, rhi::BindingType::Sampler, 0, {}, {}, { &linearClampNoMipSampler, 1 } };
                            entries[6] = { 7, rhi::BindingType::Sampled_Texture, 0, {}, { &specularPrefilter, 1 }, {} };
                            entries[7] = { 8, rhi::BindingType::Sampled_Texture, 0, {}, { &dfgLut, 1 }, {} };
                            entries[8] = { 9, rhi::BindingType::Sampler, 0, {}, {}, { &linearClampMipSampler, 1 } };
                            entries[9] = { 10, rhi::BindingType::Storage_Buffer, 0, { &instanceLightIndicesBinding, 1 }, {}, {} };
                            entries[10] = { 11, rhi::BindingType::Uniform_Buffer, 0, { &atmosphereBinding, 1 }, {}, {} };
                            entries[11] = { 12, rhi::BindingType::Sampled_Texture, 0, {}, { &transmittanceLut, 1 }, {} };
                            entries[12] = { 13, rhi::BindingType::Storage_Buffer, 0, { &skyLightDataBinding, 1 }, {}, {} };
                            bindGroup.update({ entries, 13 });
                        }
                    );
                }

                HString renderTag = BuiltinRenderTags::Translucent;
                auto& cmdList = context.getCommandList();

                // Translucent Bucket 按距离排序, 相邻 item 大概率材质不同, 没有 GBufferPass 那种
                // "相邻且材质/geometry/section 都相同就合并成一次 instanced draw" 的空间, 每个
                // item 单独一次 draw, 但仍然复用同一套 indices buffer 上传机制
                std::vector<uint32_t> instanceIndices;
                instanceIndices.reserve(data.translucentBucket->size());
                for (const auto& entry : *data.translucentBucket)
                {
                    instanceIndices.push_back((*data.drawItems)[entry.itemIndex].instanceIndex);
                }

                if (!rendererContext.getIndicesBuffer().upload(cmdList, instanceIndices))
                {
                    context.beginPass();
                    return;
                }

                uint32_t viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(data.viewId);

                context.beginPass();

                setViewportAndScissor(cmdList, data.viewport);
                // 整个 pass 的所有半透明 draw 都标同一个 bit, 不需要按 draw item 切换, 设一次就够
                cmdList.setStencilReference(kTAAStencilBit);

                // 第一版只做太阳(方向光)直接光照, 最多取 2 盏——跟 AtmosphereParameters 的
                // sun0/sun1 约定一致, 见 Translucent.slang 顶部注释
                ForwardLightingConstants forwardLighting{};
                for (DirectionalLightProxy* directionalLight : data.scene->getDirectionalLightProxies())
                {
                    if (!directionalLight->isVisible())
                        continue;

                    if (forwardLighting.lightCount == 0)
                        forwardLighting.lightIndex0 = directionalLight->getLightIndex();
                    else
                        forwardLighting.lightIndex1 = directionalLight->getLightIndex();

                    if (++forwardLighting.lightCount == 2)
                        break;
                }

                rhi::Pipeline* currentPipeline = nullptr;
                const rhi::PipelineLayout* instanceBindLayout = nullptr;
                MaterialProxy* prevMaterial = nullptr;
                GeometryProxy* prevGeometry = nullptr;
                for (uint32_t i = 0; i < data.translucentBucket->size(); ++i)
                {
                    const DrawItem& drawItem = (*data.drawItems)[(*data.translucentBucket)[i].itemIndex];

                    VertexLayout vertexLayout = drawItem.getVertexLayout();
                    rhi::Pipeline* pipeline = drawItem.material->getCachedPipeline(renderTag, vertexLayout);
                    if (!pipeline)
                    {
                        pipeline = createPipeline(rendererContext, *drawItem.material, renderTag, vertexLayout, *storage.viewLayout);
                        drawItem.material->cachePipeline(renderTag, vertexLayout, pipeline);
                    }
                    if (pipeline != currentPipeline)
                    {
                        cmdList.bindPipeline(pipeline);
                        currentPipeline = pipeline;
                        cmdList.setBindGroup(
                            1,
                            rhi::PipelineBindPoint::Graphics,
                            &currentPipeline->getLayout(),
                            viewCache.getBindGroup(),
                            { &viewDataDynamicOffset, 1 }
                        );
                        cmdList.setImmediateData(&currentPipeline->getLayout(), 0, std::span((const uint8_t*)&forwardLighting, sizeof(forwardLighting)));
                        prevMaterial = nullptr;
                    }

                    if (drawItem.material != prevMaterial)
                    {
                        const rhi::BindGroup* parametersBindGroup = drawItem.material->getParametersBindGroup();
                        if (parametersBindGroup)
                        {
                            cmdList.setBindGroup(
                                2,
                                rhi::PipelineBindPoint::Graphics,
                                &currentPipeline->getLayout(),
                                parametersBindGroup,
                                {}
                            );
                        }
                        prevMaterial = drawItem.material;
                    }

                    // set3 必须排在 set1/set2 之后绑, 否则它们换了布局会让它失效
                    if (&currentPipeline->getLayout() != instanceBindLayout)
                    {
                        const rhi::BindGroup& instanceBindGroup = data.scene->getDrawBindGroup(vertexLayout.getType());
                        cmdList.setBindGroup(3, rhi::PipelineBindPoint::Graphics, &currentPipeline->getLayout(), &instanceBindGroup, {});
                        instanceBindLayout = &currentPipeline->getLayout();
                    }

                    if (drawItem.geometry != prevGeometry)
                    {
                        drawItem.bind(cmdList);
                        prevGeometry = drawItem.geometry;
                    }

                    drawItem.draw(cmdList, 1, i);
                }

                cmdList.setBindGroup(
                    1,
                    rhi::PipelineBindPoint::Graphics,
                    &rendererContext.getGlobalPipelineLayout(),
                    &rendererContext.getViewDataManager().getBindGroup(),
                    { &viewDataDynamicOffset, 1 }
                );
            },
            rg::PassFlags::Skip_Begin_Pass
        );
    }

    rhi::Pipeline* TranslucentPass::createPipeline(
        RendererContext& rendererContext,
        MaterialProxy& material,
        HString renderTag,
        VertexLayout vertexLayout,
        const rhi::BindGroupLayout& viewLayout
    )
    {
        auto shaderVariant = material.getOrCreateShaderVariant(renderTag, vertexLayout);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        const rhi::BindGroupLayout* instanceLayout = &rendererContext.getDrawBindGroupLayout(vertexLayout.getType());

        const rhi::BindGroup* materialParameterBindGroup = material.getParametersBindGroup();
        auto* pipelineLayout = createTranslucentPipelineLayout(
            rendererContext,
            viewLayout,
            materialParameterBindGroup ? &materialParameterBindGroup->getLayout() : nullptr,
            instanceLayout
        );

        GraphicsPipelineState pipelineState;
        pipelineState.topology = static_cast<uint32_t>(rhi::PrimitiveTopology::Triangle_List);
        pipelineState.samples = 0;
        pipelineState.cullMode = static_cast<uint32_t>(material.getAttributes().doubleSided ? rhi::CullMode::None : rhi::CullMode::Back);
        pipelineState.frontFace = static_cast<uint32_t>(rhi::FrontFace::Counter_Clockwise);
        pipelineState.depthTestEnable = 1;
        pipelineState.depthWriteEnable = 0;
        pipelineState.depthCompareOp = static_cast<uint32_t>(rhi::CompareOp::Greater_Equal);
        // compareOp Always + passOp Replace: 深度测试通过的像素(不管深度测试本身是不是靠 stencil
        // 挡住东西, 这里纯粹借 stencil attachment 当一张"标记图"用)一律把 stencilReference
        // (kTAAStencilBit)写进去, 不需要真正按 stencil 值做任何取舍
        pipelineState.stencilTestEnable = 1;
        pipelineState.stencilState = StencilState::get<
            rhi::StencilOpState{ rhi::StencilOp::Keep, rhi::StencilOp::Replace, rhi::StencilOp::Keep, rhi::CompareOp::Always, 0xFF, 0xFF },
            rhi::StencilOpState{ rhi::StencilOp::Keep, rhi::StencilOp::Replace, rhi::StencilOp::Keep, rhi::CompareOp::Always, 0xFF, 0xFF }
        >();
        // 预乘 alpha 混合: (One, OneMinusSrcAlpha)——Translucent.slang 的 FragmentMain 已经把
        // 高光项以"满强度"(不乘 alpha)混进 Src 颜色, 这里的混合系数不需要、也不应该再对它做
        // 任何按 alpha 的衰减
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
