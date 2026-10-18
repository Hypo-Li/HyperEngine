#include "SkyLightPass.h"
#include "RenderPassUtils.h"
#include "SkyBackgroundPass.h"

#include <SkyCubemap.generated.h>
#include <CubemapMip.generated.h>
#include <SHCoeffGather.generated.h>
#include <SHCoeffReduce.generated.h>
#include <CubemapPrefilter.generated.h>

#include <bit>
#include <cstring>

namespace hyp
{
    // 球谐投影最多使用 32x32；更小的 Cubemap 直接使用它最精细的 mip。
    static constexpr uint32_t kSkyLightSHFaceSize = 32;
    static constexpr uint32_t kMaxSkyLightResolution = 512;

    struct SkyLightReadbackMipLayout
    {
        uint64_t offset;
        uint32_t width;
        uint32_t height;
        uint32_t bytesPerRow;
    };

    struct SkyLightReadbackLayout
    {
        std::vector<SkyLightReadbackMipLayout> mips;
        uint64_t bufferSize = 0;
        size_t tightlyPackedMipSize = 0;
    };

    static constexpr uint64_t alignUp(uint64_t value, uint64_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    static SkyLightReadbackLayout makeSkyLightReadbackLayout(uint32_t resolution, uint32_t mipCount)
    {
        static constexpr uint32_t kBytesPerPixel = sizeof(uint16_t) * 4;
        static constexpr uint32_t kTextureRowAlignment = 256;

        SkyLightReadbackLayout layout;
        layout.mips.resize(mipCount);
        uint64_t offset = alignUp(7 * sizeof(float4), kTextureRowAlignment);
        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            uint32_t size = std::max(resolution >> mip, 1u);
            uint32_t bytesPerRow = static_cast<uint32_t>(alignUp(size * kBytesPerPixel, kTextureRowAlignment));
            offset = alignUp(offset, kTextureRowAlignment);
            layout.mips[mip] = { offset, size, size, bytesPerRow };
            offset += static_cast<uint64_t>(bytesPerRow) * size * 6;
            layout.tightlyPackedMipSize += static_cast<size_t>(size) * size * kBytesPerPixel * 6;
        }
        layout.bufferSize = offset;
        return layout;
    }

    // SH 打包(9 个卷积后系数 -> 7 个 float4 二次多项式)现在由 SHCoeffReduce.slang 在 GPU 上直接
    // 完成(推导过程见那个文件里的注释),这里不再需要 CPU 端的 packDiffuseSH。

    void SkyLightPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        SkyLightProxy& skyLight,
        const AtmosphereProxy* atmosphere,
        const SkyBackgroundProxy* skyBackground
    )
    {
        bool isOneShot = skyLight.hasCaptureRequest();
        bool isRealtime = !isOneShot && skyLight.isCaptureSky() && skyLight.isRealTimeCapture();
        if (!isOneShot && !isRealtime)
            return;

        if (isRealtime)
        {
            // 一次性请求靠 hasCaptureRequest()/consumeCaptureRequest() 天然只被第一个 view 消费一次;
            // 实时模式没有这种"消费"语义, 而这个函数本身按 view 循环调用(RenderPipeline.cpp),
            // sky capture 不分 view, 一帧只需要跑一次——参照 AtmosphereLutPass::addTransmittanceLutPass
            // 的做法, 查 blackboard 这一帧是否已经有别的 view 跑过, 跑过了直接跳过
            auto& blackboard = renderGraph.getBlackboard();
            if (blackboard.get<rg::TextureHandle>("SkyCubemap"_hs(scene.getSceneId())).isValid())
                return;
        }

        // isCaptureSky() 为真但场景既没有大气也没有天空背景(比如大气刚被删掉)不能直接 return——
        // 那样已经算出来的 SH/prefilter 会永远停在上一次有真实大气时的旧值上, 表现就是"删了大气,
        // 天光看起来还是有大气时的样子"。这种情况和"isCaptureSky() 为假且没设环境贴图"一样, 退化成
        // 黑色 cube 走环境贴图这条既有路径, 让 SH gather/prefilter 正常跑一遍算出全零/全黑的结果,
        // 而不是什么都不算
        bool captureFromScene = skyLight.isCaptureSky() && (atmosphere != nullptr || skyBackground != nullptr);
        rhi::TextureView* environmentMapView = nullptr;
        if (!captureFromScene)
        {
            TextureProxy* explicitEnvironmentMap = skyLight.isCaptureSky() ? nullptr : skyLight.getEnvironmentMap();
            environmentMapView = explicitEnvironmentMap
                ? &explicitEnvironmentMap->getTextureView()
                : &rendererContext.getRenderBackend().getDefaultResources().getBlackTextureCubeView();
            const rhi::TextureDesc& environmentMapDesc = environmentMapView->getTexture().getDesc();
            const uint32_t environmentMapResolution = environmentMapDesc.extent.x;
            const uint32_t environmentMapMipCount = environmentMapDesc.mipCount;

            const bool validEnvironmentMap = environmentMapDesc.type == rhi::TextureType::Texture_Cube
                && environmentMapResolution > 0
                && std::has_single_bit(environmentMapResolution)
                && environmentMapDesc.extent.y == environmentMapResolution
                && environmentMapDesc.layerCount == 6
                && environmentMapMipCount == std::bit_width(environmentMapResolution);
            assert(validEnvironmentMap);
            if (!validEnvironmentMap)
                return;
        }

        // 一次性请求的目标纹理可能是一份刚建好、待 swap 上位的新纹理(见 SkyLight::requestCapture());
        // 实时模式没有这种 swap 语义, 每帧直接原地覆盖当前这份常驻纹理
        std::optional<SkyLightProxy::CaptureRequest> captureRequest;
        TextureProxy* specularPrefilterTarget = nullptr;
        if (isOneShot)
        {
            captureRequest = skyLight.consumeCaptureRequest();
            assert(captureRequest.has_value() && captureRequest->specularPrefilter);
            if (!captureRequest || !captureRequest->specularPrefilter)
                return;
            specularPrefilterTarget = captureRequest->specularPrefilter;
        }
        else
        {
            specularPrefilterTarget = skyLight.getSpecularPrefilter();
            if (!specularPrefilterTarget)
                return;
        }

        const rhi::TextureDesc& prefilterDesc = specularPrefilterTarget->getTexture().getDesc();
        const uint32_t prefilterResolution = prefilterDesc.extent.x;
        const uint32_t prefilterMipCount = prefilterDesc.mipCount;
        const bool validPrefilter = prefilterDesc.type == rhi::TextureType::Texture_Cube
            && std::has_single_bit(prefilterResolution)
            && prefilterResolution <= kMaxSkyLightResolution
            && prefilterDesc.extent.y == prefilterResolution
            && prefilterDesc.layerCount == 6
            && prefilterDesc.format == rhi::TextureFormat::RGBA16_SFLOAT
            && prefilterMipCount == std::bit_width(prefilterResolution);
        assert(validPrefilter);
        if (!validPrefilter)
            return;

        uint32_t sourceResolution = prefilterResolution;
        uint32_t sourceMipCount = prefilterMipCount;
        rhi::TextureFormat sourceFormat = rhi::TextureFormat::RGBA16_SFLOAT;

        if (captureFromScene)
        {
            addCaptureSkyPass(
                renderGraph,
                rendererContext,
                scene,
                skyLight,
                atmosphere,
                skyBackground,
                sourceResolution,
                sourceMipCount
            );
            addGenSkyLightMipmapPass(
                renderGraph,
                rendererContext,
                scene,
                sourceResolution,
                sourceMipCount
            );
        }
        else
        {
            const rhi::TextureDesc& environmentMapDesc = environmentMapView->getTexture().getDesc();
            sourceResolution = environmentMapDesc.extent.x;
            sourceMipCount = environmentMapDesc.mipCount;
            sourceFormat = environmentMapDesc.format;

            auto skyCubemap = renderGraph.importTexture(
                "SkyLightEnvironmentMap"_hs(scene.getSceneId()),
                environmentMapView,
                rhi::ResourceState::Shader_Sampled
            );
            renderGraph.getBlackboard().put("SkyCubemap"_hs(scene.getSceneId()), skyCubemap);
        }

        // DataBuffer 常驻在 SkyLightProxy 上(不是 render graph 自己创建的瞬时资源), import
        // 进来当 Reduce pass 的写入目标——这样非 capture 的普通帧, SkyLighting pass 才能直接从
        // proxy 读到上一次 capture 算出来的结果, 不需要 blackboard 里那些只在 capture 帧才存在的条目
        auto packedSHCoeff = renderGraph.importBuffer(
            "SkyLightData"_hs(scene.getSceneId()),
            &skyLight.getDataBuffer()
        );

        addSkyLightSHCoeffPass(
            renderGraph,
            rendererContext,
            scene,
            sourceResolution,
            sourceFormat,
            packedSHCoeff
        );
        addSkyLightPrefilterPass(
            renderGraph,
            rendererContext,
            scene,
            sourceResolution,
            sourceMipCount,
            prefilterResolution,
            prefilterMipCount
        );

        if (isOneShot)
        {
            addReadbackPass(
                renderGraph,
                rendererContext,
                scene,
                std::move(*captureRequest),
                prefilterResolution,
                prefilterMipCount,
                packedSHCoeff
            );
        }
        else
        {
            addResolvePrefilterPass(
                renderGraph,
                rendererContext,
                scene,
                specularPrefilterTarget,
                prefilterResolution,
                prefilterMipCount
            );
        }
    }

    static_assert(sizeof(WorldCoord) == 32);

    struct CaptureSkyData
    {
        uint32_t sceneId;
        const AtmosphereProxy* atmosphere;
        WorldCoord captureOrigin;
        uint32_t hasBackground;
        uint32_t dispatchGroupCount;
        rg::TextureHandle transmittanceLut;
        rg::TextureHandle multiScatteringLut;
        rg::TextureHandle skyCubemapMip0;
    };

    struct CaptureSkyStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> passLayout;
        std::vector<BindGroupCache<5>> perSceneCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void SkyLightPass::addCaptureSkyPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SkyLightProxy& skyLight,
        const AtmosphereProxy* atmosphere,
        const SkyBackgroundProxy* skyBackground,
        uint32_t captureResolution,
        uint32_t captureMipCount)
    {
        const bool hasBackground = skyBackground != nullptr;

        rhi::TextureDesc skyCubemapDesc;
        skyCubemapDesc.type = rhi::TextureType::Texture_Cube;
        skyCubemapDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
        skyCubemapDesc.extent = { captureResolution, captureResolution, 1 };
        skyCubemapDesc.mipCount = captureMipCount;
        skyCubemapDesc.layerCount = 6;
        skyCubemapDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage | rhi::TextureUsage::Color_Attachment;

        if (hasBackground)
        {
            SkyBackgroundPass::addCaptureToRenderGraph(
                renderGraph,
                rendererContext,
                scene,
                *skyBackground,
                "SkyCubemap"_hs(scene.getSceneId()),
                skyCubemapDesc
            );
        }

        if (!atmosphere)
            return;

        renderGraph.addComputePass<CaptureSkyData>(
            "CaptureSky"_hs,
            [&](rg::RenderGraph::Builder& builder, CaptureSkyData& data)
            {
                builder.markSideEffect();

                data.sceneId = scene.getSceneId();
                data.atmosphere = atmosphere;
                data.captureOrigin.fromDouble3(skyLight.getCapturePosition());
                data.hasBackground = hasBackground ? 1u : 0u;
                data.dispatchGroupCount = std::max((captureResolution + 7u) / 8u, 1u);

                auto& blackboard = builder.getBlackboard();

                auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(data.sceneId));
                data.transmittanceLut = builder.sample(transmittanceLut);
                auto multiScatteringLut = blackboard.get<rg::TextureHandle>("MultiScatteringLut"_hs(data.sceneId));
                data.multiScatteringLut = builder.sample(multiScatteringLut);

                auto skyCubemap = hasBackground
                    ? blackboard.get<rg::TextureHandle>("SkyCubemap"_hs(data.sceneId))
                    : builder.createTexture("SkyCubemap"_hs(data.sceneId), skyCubemapDesc);

                rhi::TextureViewDesc skyCubemapMip0Desc;
                skyCubemapMip0Desc.type = rhi::TextureViewType::Texture_View_2D_Array;
                skyCubemapMip0Desc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                skyCubemapMip0Desc.range.aspect = rhi::TextureAspect::Color;
                skyCubemapMip0Desc.range.baseMip = 0;
                skyCubemapMip0Desc.range.mipCount = 1;
                skyCubemapMip0Desc.range.baseLayer = 0;
                skyCubemapMip0Desc.range.layerCount = 6;
                auto skyCubemapMip0 = builder.createSubTexture(
                    "SkyCubemapMip0"_hs(data.sceneId),
                    skyCubemap,
                    skyCubemapMip0Desc
                );
                data.skyCubemapMip0 = builder.storage(
                    skyCubemapMip0,
                    hasBackground ? rg::AccessType::Read_Write : rg::AccessType::Write_Only
                );

                skyCubemap = builder.syncTextures(data.skyCubemapMip0, skyCubemap);
                blackboard.put("SkyCubemap"_hs(data.sceneId), skyCubemap);
            },
            [&rendererContext](rg::ExecuteContext& context, CaptureSkyData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<SkyLightPass, CaptureSkyStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "CaptureSky",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(SkyCubemap, Compute))
                    );
                }

                if (storage.perSceneCaches.size() <= data.sceneId)
                {
                    storage.perSceneCaches.resize(data.sceneId + 1);
                }
                auto& bindGroupCache = storage.perSceneCaches[data.sceneId];

                rhi::Buffer* atmosphereParameters = &data.atmosphere->getParametersBuffer();
                rhi::TextureView* transmittanceLut = context.getTexture(data.transmittanceLut);
                rhi::TextureView* multiScatteringLut = context.getTexture(data.multiScatteringLut);
                rhi::TextureView* skyCubemapMip0 = context.getTexture(data.skyCubemapMip0);

                rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                if (bindGroupCache.needUpdate(atmosphereParameters, transmittanceLut, multiScatteringLut, skyCubemapMip0, linearClampNoMipSampler))
                {
                    if (!storage.passLayout)
                    {
                        storage.passLayout = createPassBindGroupLayout(renderBackend, generated::kSkyCubemap_Bindings);
                    }

                    bindGroupCache.update(
                        renderBackend,
                        *storage.passLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kSkyCubemap_Bindings);
                            writer.setBuffer("Atmosphere", atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.setSampler("LutSampler", linearClampNoMipSampler);
                            writer.setTexture("TransmittanceLutTexture", transmittanceLut);
                            writer.setTexture("MultiScatteringLutTexture", multiScatteringLut);
                            writer.setTexture("SkyCubemapImage", skyCubemapMip0);
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
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(&data.captureOrigin),
                        sizeof(data.captureOrigin)
                    )
                );
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(),
                    sizeof(data.captureOrigin),
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(&data.hasBackground),
                        sizeof(data.hasBackground)
                    )
                );
                cmdList.dispatch(data.dispatchGroupCount, data.dispatchGroupCount, 6);
            }
        );
    }

    struct GenSkyLightMipmapData
    {
        uint32_t sceneId;
        uint32_t mipLevel;
        uint32_t dispatchGroupCount;
        rg::TextureHandle srcMip;
        rg::TextureHandle dstMip;
    };

    struct GenSkyLightMipmapStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> passLayout;
        // 每个 scene 一份、每个 mip 目标级别一份 cache(mip 0 由 CaptureSky 生成, 这里用不到,
        // 对应槽位空着不用)——每一级用到的 src/dst view 都不一样, 不能像 Upsweep 那样只用
        // 2 个槽位 ping-pong
        std::vector<std::vector<BindGroupCache<3>>> perSceneMipCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void SkyLightPass::addGenSkyLightMipmapPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        uint32_t captureResolution,
        uint32_t captureMipCount
    )
    {
        static const HString passNames[] = {
            "GenSkyLightMipmap1"_hs,
            "GenSkyLightMipmap2"_hs,
            "GenSkyLightMipmap3"_hs,
            "GenSkyLightMipmap4"_hs,
            "GenSkyLightMipmap5"_hs,
            "GenSkyLightMipmap6"_hs,
            "GenSkyLightMipmap7"_hs,
            "GenSkyLightMipmap8"_hs,
            "GenSkyLightMipmap9"_hs,
            "GenSkyLightMipmap10"_hs,
            "GenSkyLightMipmap11"_hs,
            "GenSkyLightMipmap12"_hs,
        };
        static const HString srcMipNames[] = {
            "SkyCubemapMip1Src"_hs,
            "SkyCubemapMip2Src"_hs,
            "SkyCubemapMip3Src"_hs,
            "SkyCubemapMip4Src"_hs,
            "SkyCubemapMip5Src"_hs,
            "SkyCubemapMip6Src"_hs,
            "SkyCubemapMip7Src"_hs,
            "SkyCubemapMip8Src"_hs,
            "SkyCubemapMip9Src"_hs,
            "SkyCubemapMip10Src"_hs,
            "SkyCubemapMip11Src"_hs,
            "SkyCubemapMip12Src"_hs,
        };
        static const HString dstMipNames[] = {
            "SkyCubemapMip1Dst"_hs,
            "SkyCubemapMip2Dst"_hs,
            "SkyCubemapMip3Dst"_hs,
            "SkyCubemapMip4Dst"_hs,
            "SkyCubemapMip5Dst"_hs,
            "SkyCubemapMip6Dst"_hs,
            "SkyCubemapMip7Dst"_hs,
            "SkyCubemapMip8Dst"_hs,
            "SkyCubemapMip9Dst"_hs,
            "SkyCubemapMip10Dst"_hs,
            "SkyCubemapMip11Dst"_hs,
            "SkyCubemapMip12Dst"_hs,
        };

        assert(captureMipCount > 0 && captureMipCount - 1 <= std::size(passNames));

        for (uint32_t mip = 1; mip < captureMipCount; ++mip)
        {
            uint32_t mipSize = std::max(captureResolution >> mip, 1u);

            renderGraph.addComputePass<GenSkyLightMipmapData>(
                passNames[mip - 1],
                [&, mip](rg::RenderGraph::Builder& builder, GenSkyLightMipmapData& data)
                {
                    builder.markSideEffect();

                    data.sceneId = scene.getSceneId();
                    data.mipLevel = mip;
                    data.dispatchGroupCount = std::max((mipSize + 7u) / 8u, 1u);

                    auto& blackboard = builder.getBlackboard();
                    auto skyCubemap = blackboard.get<rg::TextureHandle>("SkyCubemap"_hs(data.sceneId));

                    rhi::TextureViewDesc srcDesc;
                    srcDesc.type = rhi::TextureViewType::Texture_View_Cube;
                    srcDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                    srcDesc.range.aspect = rhi::TextureAspect::Color;
                    srcDesc.range.baseMip = mip - 1;
                    srcDesc.range.mipCount = 1;
                    srcDesc.range.baseLayer = 0;
                    srcDesc.range.layerCount = 6;
                    auto srcMip = builder.createSubTexture(
                        srcMipNames[mip - 1](data.sceneId),
                        skyCubemap,
                        srcDesc
                    );
                    data.srcMip = builder.sample(srcMip);

                    rhi::TextureViewDesc dstDesc;
                    dstDesc.type = rhi::TextureViewType::Texture_View_2D_Array;
                    dstDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                    dstDesc.range.aspect = rhi::TextureAspect::Color;
                    dstDesc.range.baseMip = mip;
                    dstDesc.range.mipCount = 1;
                    dstDesc.range.baseLayer = 0;
                    dstDesc.range.layerCount = 6;
                    auto dstMip = builder.createSubTexture(
                        dstMipNames[mip - 1](data.sceneId),
                        skyCubemap,
                        dstDesc
                    );
                    data.dstMip = builder.storage(dstMip, rg::AccessType::Write_Only);

                    skyCubemap = builder.syncTextures(data.dstMip, skyCubemap);
                    blackboard.put("SkyCubemap"_hs(data.sceneId), skyCubemap);
                },
                [&rendererContext](rg::ExecuteContext& context, GenSkyLightMipmapData& data)
                {
                    static auto& storage = rendererContext.getPassStorage().getOrCreate<SkyLightPass, GenSkyLightMipmapStorage>();
                    auto& renderBackend = context.getRenderBackend();

                    if (!storage.shader)
                    {
                        storage.shader = std::make_shared<PassShader>(
                            renderBackend.getDevice(),
                            "CubemapMip",
                            makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(CubemapMip, Compute))
                        );
                    }

                    if (!storage.passLayout)
                    {
                        storage.passLayout = createPassBindGroupLayout(renderBackend, generated::kCubemapMip_Bindings);
                    }

                    if (storage.perSceneMipCaches.size() <= data.sceneId)
                    {
                        storage.perSceneMipCaches.resize(data.sceneId + 1);
                    }
                    auto& mipCaches = storage.perSceneMipCaches[data.sceneId];
                    if (mipCaches.size() <= data.mipLevel)
                        mipCaches.resize(data.mipLevel + 1);
                    auto& bindGroupCache = mipCaches[data.mipLevel];

                    rhi::TextureView* srcMip = context.getTexture(data.srcMip);
                    rhi::TextureView* dstMip = context.getTexture(data.dstMip);
                    rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                    if (bindGroupCache.needUpdate(linearClampNoMipSampler, srcMip, dstMip))
                    {
                        bindGroupCache.update(
                            renderBackend,
                            *storage.passLayout,
                            [&](rhi::BindGroup& bindGroup)
                            {
                                PassBindGroupWriter writer(generated::kCubemapMip_Bindings);
                                writer.setSampler("LinearSampler", linearClampNoMipSampler);
                                writer.setTexture("SrcMip", srcMip);
                                writer.setTexture("DstMip", dstMip);
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
                    cmdList.dispatch(data.dispatchGroupCount, data.dispatchGroupCount, 6);
                }
            );
        }
    }

    struct SkyLightSHGatherData
    {
        uint32_t sceneId;
        uint32_t faceSize;
        uint32_t groupsPerRow;
        rg::TextureHandle srcMip;
        rg::BufferHandle partialSHCoeff;
    };

    struct SkyLightSHGatherStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> passLayout;
        std::vector<BindGroupCache<3>> perSceneCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    struct SkyLightSHReduceData
    {
        uint32_t sceneId;
        uint32_t groupCount;
        rg::BufferHandle partialSHCoeff;
        rg::BufferHandle packedSHCoeff;
    };

    struct SkyLightSHReduceStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> passLayout;
        std::vector<BindGroupCache<2>> perSceneCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    // Gather 只负责"计算 + workgroup 内规约", 每个 workgroup 把自己的部分和写到独占的一段 buffer
    // 里, 互不冲突, 不需要原子操作; Reduce 再把这些部分和沿 group 维度求和成最终的 9 个系数。两个
    // pass 之间的读写关系(Gather 写 partialSHCoeff、Reduce 读它)通过 blackboard + builder.accessBuffer
    // 正常声明, RenderGraph 会自动插入正确的 barrier, Vulkan/WebGPU 都不需要手写同步
    void SkyLightPass::addSkyLightSHCoeffPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        uint32_t sourceResolution,
        rhi::TextureFormat sourceFormat,
        rg::BufferHandle packedSHCoeff
    )
    {
        const uint32_t shFaceSize = std::min(sourceResolution, kSkyLightSHFaceSize);
        uint32_t shMipLevel = 0;
        uint32_t sourceMipSize = sourceResolution;
        while (sourceMipSize > kSkyLightSHFaceSize)
        {
            sourceMipSize >>= 1;
            ++shMipLevel;
        }
        assert(sourceMipSize == shFaceSize);
        const uint32_t shGroupsPerRow = std::max((shFaceSize + 7u) / 8u, 1u);
        const uint32_t shGroupCount = shGroupsPerRow * shGroupsPerRow * 6;

        renderGraph.addComputePass<SkyLightSHGatherData>(
            "SkyLightSHGather"_hs,
            [&](rg::RenderGraph::Builder& builder, SkyLightSHGatherData& data)
            {
                builder.markSideEffect();

                data.sceneId = scene.getSceneId();
                data.faceSize = shFaceSize;
                data.groupsPerRow = shGroupsPerRow;

                auto& blackboard = builder.getBlackboard();
                auto skyCubemap = blackboard.get<rg::TextureHandle>("SkyCubemap"_hs(data.sceneId));

                rhi::TextureViewDesc srcDesc;
                srcDesc.type = rhi::TextureViewType::Texture_View_Cube;
                srcDesc.format = sourceFormat;
                srcDesc.range.aspect = rhi::TextureAspect::Color;
                srcDesc.range.baseMip = shMipLevel;
                srcDesc.range.mipCount = 1;
                srcDesc.range.baseLayer = 0;
                srcDesc.range.layerCount = 6;
                auto srcMip = builder.createSubTexture(
                    "SkyCubemapSHMip"_hs(data.sceneId),
                    skyCubemap,
                    srcDesc
                );
                data.srcMip = builder.sample(srcMip);

                auto partialSHCoeffHandle = builder.createBuffer(
                    "SkyLightPartialSHCoeff"_hs(data.sceneId),
                    rhi::BufferDesc{ shGroupCount * 9 * sizeof(float4), rhi::BufferUsage::Storage }
                );
                data.partialSHCoeff = builder.accessBuffer(partialSHCoeffHandle, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);
                blackboard.put("SkyLightPartialSHCoeff"_hs(data.sceneId), data.partialSHCoeff);
            },
            [&rendererContext](rg::ExecuteContext& context, SkyLightSHGatherData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<SkyLightPass, SkyLightSHGatherStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "SHCoeffGather",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(SHCoeffGather, Compute))
                    );
                }

                if (!storage.passLayout)
                {
                    storage.passLayout = createPassBindGroupLayout(renderBackend, generated::kSHCoeffGather_Bindings);
                }

                if (storage.perSceneCaches.size() <= data.sceneId)
                {
                    storage.perSceneCaches.resize(data.sceneId + 1);
                }
                auto& bindGroupCache = storage.perSceneCaches[data.sceneId];

                rhi::TextureView* srcMip = context.getTexture(data.srcMip);
                rhi::Buffer* partialSHCoeff = context.getBuffer(data.partialSHCoeff);
                rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                if (bindGroupCache.needUpdate(linearClampNoMipSampler, srcMip, partialSHCoeff))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.passLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kSHCoeffGather_Bindings);
                            writer.setSampler("LinearSampler", linearClampNoMipSampler);
                            writer.setTexture("SrcCubemap", srcMip);
                            writer.setBuffer("DstPartialSHCoeff", partialSHCoeff);
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
                struct GatherImmediateData
                {
                    uint32_t faceSize;
                    uint32_t groupsPerRow;
                };
                GatherImmediateData immediateData{ data.faceSize, data.groupsPerRow };
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&immediateData), sizeof(immediateData))
                );
                cmdList.dispatch(data.groupsPerRow, data.groupsPerRow, 6);
            }
        );

        renderGraph.addComputePass<SkyLightSHReduceData>(
            "SkyLightSHReduce"_hs,
            [&](rg::RenderGraph::Builder& builder, SkyLightSHReduceData& data)
            {
                builder.markSideEffect();

                data.sceneId = scene.getSceneId();
                data.groupCount = shGroupCount;

                auto& blackboard = builder.getBlackboard();
                auto partialSHCoeff = blackboard.get<rg::BufferHandle>("SkyLightPartialSHCoeff"_hs(data.sceneId));
                data.partialSHCoeff = builder.accessBuffer(partialSHCoeff, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

                // packedSHCoeff 是 SkyLightProxy 常驻的 buffer(import 进来的, 不是这里新建的),
                // Reduce 直接把打包好的结果写进去, SkyLighting pass 以后每帧都能从 proxy 读到
                data.packedSHCoeff = builder.accessBuffer(packedSHCoeff, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);
            },
            [&rendererContext](rg::ExecuteContext& context, SkyLightSHReduceData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<SkyLightPass, SkyLightSHReduceStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "SHCoeffReduce",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(SHCoeffReduce, Compute))
                    );
                }

                if (!storage.passLayout)
                {
                    storage.passLayout = createPassBindGroupLayout(renderBackend, generated::kSHCoeffReduce_Bindings);
                }

                if (storage.perSceneCaches.size() <= data.sceneId)
                {
                    storage.perSceneCaches.resize(data.sceneId + 1);
                }
                auto& bindGroupCache = storage.perSceneCaches[data.sceneId];

                rhi::Buffer* partialSHCoeff = context.getBuffer(data.partialSHCoeff);
                rhi::Buffer* packedSHCoeff = context.getBuffer(data.packedSHCoeff);

                if (bindGroupCache.needUpdate(partialSHCoeff, packedSHCoeff))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.passLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kSHCoeffReduce_Bindings);
                            writer.setBuffer("SrcPartialSHCoeff", partialSHCoeff);
                            writer.setBuffer("DstPackedSHCoeff", packedSHCoeff);
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
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(&data.groupCount),
                        sizeof(data.groupCount)
                    )
                );
                cmdList.dispatch(1, 1, 1);
            }
        );
    }

    struct SkyLightPrefilterImmediateData
    {
        uint32_t mipLevel;
        uint32_t sampleCount;
        uint32_t sourceResolution;
        uint32_t sourceMipCount;
        uint32_t prefilterMipCount;
    };

    struct SkyLightPrefilterData
    {
        uint32_t sceneId;
        uint32_t mipLevel;
        uint32_t dispatchGroupCount;
        SkyLightPrefilterImmediateData immediateData;
        rg::TextureHandle srcCubemap;
        rg::TextureHandle dstMip;
    };

    struct SkyLightPrefilterStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> passLayout;
        // 每个 scene、每一级 prefilter mip 各自一份 cache——每一级绑定的目标 view 都不一样,
        // 源 cubemap(整条 mip 链)倒是每一级都相同, 但一起放进同一个 needUpdate 比较里也没坏处
        std::vector<std::vector<BindGroupCache<3>>> perSceneMipCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    // 从带完整 mip 链的源 Cubemap 生成可配置分辨率的 GGX prefilter。每一级写瞬时纹理的一个 mip，
    // 粗糙度、源尺寸和实际 mip 数通过 push constant 传入；最后再统一 resolve 到持久纹理。
    void SkyLightPass::addSkyLightPrefilterPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        uint32_t sourceResolution,
        uint32_t sourceMipCount,
        uint32_t prefilterResolution,
        uint32_t prefilterMipCount
    )
    {
        static const HString dstMipNames[] = {
            "SkyLightPrefilterMip0Dst"_hs,
            "SkyLightPrefilterMip1Dst"_hs,
            "SkyLightPrefilterMip2Dst"_hs,
            "SkyLightPrefilterMip3Dst"_hs,
            "SkyLightPrefilterMip4Dst"_hs,
            "SkyLightPrefilterMip5Dst"_hs,
            "SkyLightPrefilterMip6Dst"_hs,
            "SkyLightPrefilterMip7Dst"_hs,
            "SkyLightPrefilterMip8Dst"_hs,
            "SkyLightPrefilterMip9Dst"_hs,
            "SkyLightPrefilterMip10Dst"_hs,
            "SkyLightPrefilterMip11Dst"_hs,
            "SkyLightPrefilterMip12Dst"_hs,
        };

        assert(prefilterMipCount <= std::size(dstMipNames));

        for (uint32_t mip = 0; mip < prefilterMipCount; ++mip)
        {
            uint32_t mipSize = std::max(prefilterResolution >> mip, 1u);
            // 越粗糙的 mip 用越多的重要性采样样本压噪点, 上限 256 避免太夸张
            uint32_t sampleCount = 32u << std::min(mip, 3u);

            renderGraph.addComputePass<SkyLightPrefilterData>(
                "SkyLightPrefilter"_hs(mip),
                [&, mip](rg::RenderGraph::Builder& builder, SkyLightPrefilterData& data)
                {
                    builder.markSideEffect();

                    data.sceneId = scene.getSceneId();
                    data.mipLevel = mip;
                    data.dispatchGroupCount = std::max((mipSize + 7u) / 8u, 1u);
                    data.immediateData = {
                        mip,
                        sampleCount,
                        sourceResolution,
                        sourceMipCount,
                        prefilterMipCount
                    };

                    auto& blackboard = builder.getBlackboard();
                    auto skyCubemap = blackboard.get<rg::TextureHandle>("SkyCubemap"_hs(data.sceneId));
                    data.srcCubemap = builder.sample(skyCubemap);

                    rg::TextureHandle prefilterCubemap;
                    if (mip == 0)
                    {
                        rhi::TextureDesc prefilterDesc;
                        prefilterDesc.type = rhi::TextureType::Texture_Cube;
                        prefilterDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                        prefilterDesc.extent = { prefilterResolution, prefilterResolution, 1 };
                        prefilterDesc.mipCount = prefilterMipCount;
                        prefilterDesc.layerCount = 6;
                        prefilterCubemap = builder.createTexture(
                            "SkyLightPrefilter"_hs(data.sceneId),
                            prefilterDesc
                        );
                    }
                    else
                    {
                        prefilterCubemap = blackboard.get<rg::TextureHandle>("SkyLightPrefilter"_hs(data.sceneId));
                    }

                    rhi::TextureViewDesc dstDesc;
                    dstDesc.type = rhi::TextureViewType::Texture_View_2D_Array;
                    dstDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                    dstDesc.range.aspect = rhi::TextureAspect::Color;
                    dstDesc.range.baseMip = mip;
                    dstDesc.range.mipCount = 1;
                    dstDesc.range.baseLayer = 0;
                    dstDesc.range.layerCount = 6;
                    auto dstMip = builder.createSubTexture(
                        dstMipNames[mip](data.sceneId),
                        prefilterCubemap,
                        dstDesc
                    );
                    data.dstMip = builder.storage(dstMip, rg::AccessType::Write_Only);

                    prefilterCubemap = builder.syncTextures(data.dstMip, prefilterCubemap);
                    blackboard.put("SkyLightPrefilter"_hs(data.sceneId), prefilterCubemap);
                },
                [&rendererContext](rg::ExecuteContext& context, SkyLightPrefilterData& data)
                {
                    static auto& storage = rendererContext.getPassStorage().getOrCreate<SkyLightPass, SkyLightPrefilterStorage>();
                    auto& renderBackend = context.getRenderBackend();

                    if (!storage.shader)
                    {
                        storage.shader = std::make_shared<PassShader>(
                            renderBackend.getDevice(),
                            "CubemapPrefilter",
                            makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(CubemapPrefilter, Compute))
                        );
                    }

                    if (!storage.passLayout)
                    {
                        storage.passLayout = createPassBindGroupLayout(renderBackend, generated::kCubemapPrefilter_Bindings);
                    }

                    if (storage.perSceneMipCaches.size() <= data.sceneId)
                    {
                        storage.perSceneMipCaches.resize(data.sceneId + 1);
                    }
                    auto& mipCaches = storage.perSceneMipCaches[data.sceneId];
                    if (mipCaches.size() <= data.mipLevel)
                        mipCaches.resize(data.mipLevel + 1);
                    auto& descriptorCache = mipCaches[data.mipLevel];

                    rhi::TextureView* srcCubemap = context.getTexture(data.srcCubemap);
                    rhi::TextureView* dstMip = context.getTexture(data.dstMip);
                    rhi::Sampler* linearClampMipSampler = renderBackend.getSamplerCache().getLinearClampMip();

                    if (descriptorCache.needUpdate(linearClampMipSampler, srcCubemap, dstMip))
                    {
                        descriptorCache.update(
                            renderBackend,
                            *storage.passLayout,
                            [&](rhi::BindGroup& bindGroup)
                            {
                                PassBindGroupWriter writer(generated::kCubemapPrefilter_Bindings);
                                writer.setSampler("LinearSampler", linearClampMipSampler);
                                writer.setTexture("SrcCubemap", srcCubemap);
                                writer.setTexture("DstPrefilterMip", dstMip);
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
                        descriptorCache.getBindGroup(),
                        {}
                    );

                    cmdList.setImmediateData(
                        &storage.pipeline->getLayout(),
                        0,
                        std::span<const uint8_t>(
                            reinterpret_cast<const uint8_t*>(&data.immediateData),
                            sizeof(data.immediateData)
                        )
                    );

                    cmdList.dispatch(data.dispatchGroupCount, data.dispatchGroupCount, 6);
                }
            );
        }
    }

    struct SkyLightReadbackData
    {
        uint32_t sceneId;
        rg::BufferHandle packedSHCoeff;
        rg::TextureHandle specularPrefilter;
        rg::TextureHandle resolvedSpecularPrefilter;
        rg::BufferHandle readback;
        TRef<rhi::Buffer> readbackBuffer;
        SkyLightProxy::CaptureRequest captureRequest;
        SkyLightReadbackLayout readbackLayout;
    };

    void SkyLightPass::addReadbackPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        SkyLightProxy::CaptureRequest captureRequest,
        uint32_t prefilterResolution,
        uint32_t prefilterMipCount,
        rg::BufferHandle packedSHCoeff)
    {
        SkyLightReadbackLayout readbackLayout = makeSkyLightReadbackLayout(
            prefilterResolution,
            prefilterMipCount
        );

        rhi::BufferDesc readbackDesc;
        readbackDesc.size = readbackLayout.bufferSize;
        readbackDesc.usage = rhi::BufferUsage::Transfer_Dst;
        TRef<rhi::Buffer> readbackBuffer = rendererContext.getRenderBackend().getDevice().createBuffer(
            readbackDesc,
            rhi::MemoryType::Gpu_To_Cpu
        );
        // execute 全程只有 copyBuffer/copyTexture/copyTextureToBuffer, 真正的 CPU 侧回读(map/
        // memcpy/unmap)推迟到 DeferredTaskQueue 里、等 GPU 拷贝完成后的未来某帧才执行, 不在这个
        // command list 录制期间发生 —— 没有任何 dispatch/draw/setBindGroup, 是 rg::TransferPass
        // 的典型用例。
        renderGraph.addTransferPass<SkyLightReadbackData>(
            "SkyLightResolveAndReadback"_hs,
            [
                &scene,
                readbackBuffer,
                captureRequest = std::move(captureRequest),
                readbackLayout = std::move(readbackLayout),
                packedSHCoeff
            ](
                rg::RenderGraph::Builder& builder,
                SkyLightReadbackData& data) mutable
            {
                builder.markSideEffect();
                data.sceneId = scene.getSceneId();
                data.readbackBuffer = readbackBuffer;
                data.captureRequest = std::move(captureRequest);
                data.readbackLayout = std::move(readbackLayout);

                auto& blackboard = builder.getBlackboard();
                data.packedSHCoeff = builder.accessBuffer(
                    packedSHCoeff,
                    rhi::BufferUsage::Transfer_Src,
                    rg::AccessType::Read_Only
                );

                auto specularPrefilter = blackboard.get<rg::TextureHandle>("SkyLightPrefilter"_hs(data.sceneId));
                data.specularPrefilter = builder.transfer(specularPrefilter, rg::AccessType::Read_Only);

                auto resolvedSpecularPrefilter = builder.importTexture(
                    "ResolvedSkyLightPrefilter"_hs(data.sceneId),
                    &data.captureRequest.specularPrefilter->getTextureView(),
                    rhi::ResourceState::Shader_Sampled
                );
                data.resolvedSpecularPrefilter = builder.transfer(
                    resolvedSpecularPrefilter,
                    rg::AccessType::Write_Only
                );

                auto readback = builder.importBuffer("SkyLightReadbackBuffer"_hs(data.sceneId), readbackBuffer.get());
                data.readback = builder.accessBuffer(
                    readback,
                    rhi::BufferUsage::Transfer_Dst,
                    rg::AccessType::Write_Only
                );
            },
            [&rendererContext](rg::ExecuteContext& context, SkyLightReadbackData& data)
            {
                rhi::CommandList& cmdList = context.getCommandList();
                rhi::Buffer* readback = context.getBuffer(data.readback);
                const SkyLightReadbackLayout& readbackLayout = data.readbackLayout;
                const uint32_t prefilterMipCount = static_cast<uint32_t>(readbackLayout.mips.size());

                rhi::BufferCopyRegion shCopy;
                shCopy.srcOffset = 0;
                shCopy.dstOffset = 0;
                shCopy.size = 7 * sizeof(float4);
                cmdList.copyBuffer(context.getBuffer(data.packedSHCoeff), readback, { &shCopy, 1 });

                std::vector<rhi::TextureCopyRegion> resolveRegions(prefilterMipCount);
                std::vector<rhi::BufferTextureCopyRegion> readbackRegions(prefilterMipCount);
                for (uint32_t mip = 0; mip < prefilterMipCount; ++mip)
                {
                    const auto& mipLayout = readbackLayout.mips[mip];
                    auto& resolveRegion = resolveRegions[mip];
                    resolveRegion.srcSlice = { rhi::TextureAspect::Color, mip, 0, 6 };
                    resolveRegion.srcOffset = { 0, 0, 0 };
                    resolveRegion.dstSlice = { rhi::TextureAspect::Color, mip, 0, 6 };
                    resolveRegion.dstOffset = { 0, 0, 0 };
                    resolveRegion.extent = { mipLayout.width, mipLayout.height, 1 };

                    auto& readbackRegion = readbackRegions[mip];
                    readbackRegion.bufferOffset = mipLayout.offset;
                    readbackRegion.bytesPerRow = mipLayout.bytesPerRow;
                    readbackRegion.rowsPerTexture = mipLayout.height;
                    readbackRegion.slice = { rhi::TextureAspect::Color, mip, 0, 6 };
                    readbackRegion.textureOffset = { 0, 0, 0 };
                    readbackRegion.textureExtent = { mipLayout.width, mipLayout.height, 1 };
                }

                rhi::TextureView* prefilterView = context.getTexture(data.specularPrefilter);
                rhi::TextureView* resolvedPrefilterView = context.getTexture(data.resolvedSpecularPrefilter);
                cmdList.copyTexture(
                    &prefilterView->getTexture(),
                    &resolvedPrefilterView->getTexture(),
                    resolveRegions
                );
                cmdList.copyTextureToBuffer(&prefilterView->getTexture(), readback, readbackRegions);

                auto promise = std::move(data.captureRequest.promise);

                rendererContext.getRenderBackend().getDeferredTaskQueue().push(
                    [readbackLayout = std::move(data.readbackLayout),
                    promise = std::move(promise),
                    readback = std::move(data.readbackBuffer)]() mutable
                    {
                        const uint8_t* mapped = static_cast<const uint8_t*>(readback->map());
                        if (!mapped)
                            return;

                        SkyLightCaptureResult result;
                        std::memcpy(result.diffuseSHCoeff.data(), mapped, sizeof(result.diffuseSHCoeff));
                        result.specularPrefilterMips.resize(readbackLayout.tightlyPackedMipSize);

                        static constexpr size_t kBytesPerPixel = sizeof(uint16_t) * 4;
                        uint8_t* dst = result.specularPrefilterMips.data();
                        for (const auto& mipLayout : readbackLayout.mips)
                        {
                            size_t tightRowSize = static_cast<size_t>(mipLayout.width) * kBytesPerPixel;
                            for (uint32_t layer = 0; layer < 6; ++layer)
                            {
                                const uint8_t* srcLayer = mapped + mipLayout.offset
                                    + static_cast<size_t>(layer) * mipLayout.bytesPerRow * mipLayout.height;
                                for (uint32_t row = 0; row < mipLayout.height; ++row)
                                {
                                    std::memcpy(dst, srcLayer + static_cast<size_t>(row) * mipLayout.bytesPerRow, tightRowSize);
                                    dst += tightRowSize;
                                }
                            }
                        }

                        readback->unmap();
                        promise->set_value(std::move(result));
                    },
                    kMaxFramesInFlight
                );
            }
        );
    }

    struct SkyLightResolvePrefilterData
    {
        uint32_t sceneId;
        uint32_t prefilterResolution;
        uint32_t prefilterMipCount;
        rg::TextureHandle specularPrefilter;
        rg::TextureHandle resolvedSpecularPrefilter;
    };

    void SkyLightPass::addResolvePrefilterPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        TextureProxy* specularPrefilterTarget,
        uint32_t prefilterResolution,
        uint32_t prefilterMipCount)
    {
        renderGraph.addTransferPass<SkyLightResolvePrefilterData>(
            "SkyLightResolvePrefilter"_hs,
            [&scene, specularPrefilterTarget, prefilterResolution, prefilterMipCount](
                rg::RenderGraph::Builder& builder,
                SkyLightResolvePrefilterData& data)
            {
                builder.markSideEffect();
                data.sceneId = scene.getSceneId();
                data.prefilterResolution = prefilterResolution;
                data.prefilterMipCount = prefilterMipCount;

                auto& blackboard = builder.getBlackboard();
                auto specularPrefilter = blackboard.get<rg::TextureHandle>("SkyLightPrefilter"_hs(data.sceneId));
                data.specularPrefilter = builder.transfer(specularPrefilter, rg::AccessType::Read_Only);

                auto resolvedSpecularPrefilter = builder.importTexture(
                    "ResolvedSkyLightPrefilter"_hs(data.sceneId),
                    &specularPrefilterTarget->getTextureView(),
                    rhi::ResourceState::Shader_Sampled
                );
                data.resolvedSpecularPrefilter = builder.transfer(
                    resolvedSpecularPrefilter,
                    rg::AccessType::Write_Only
                );
            },
            [](rg::ExecuteContext& context, SkyLightResolvePrefilterData& data)
            {
                rhi::CommandList& cmdList = context.getCommandList();

                std::vector<rhi::TextureCopyRegion> resolveRegions(data.prefilterMipCount);
                for (uint32_t mip = 0; mip < data.prefilterMipCount; ++mip)
                {
                    uint32_t mipSize = std::max(data.prefilterResolution >> mip, 1u);
                    auto& resolveRegion = resolveRegions[mip];
                    resolveRegion.srcSlice = { rhi::TextureAspect::Color, mip, 0, 6 };
                    resolveRegion.srcOffset = { 0, 0, 0 };
                    resolveRegion.dstSlice = { rhi::TextureAspect::Color, mip, 0, 6 };
                    resolveRegion.dstOffset = { 0, 0, 0 };
                    resolveRegion.extent = { mipSize, mipSize, 1 };
                }

                rhi::TextureView* prefilterView = context.getTexture(data.specularPrefilter);
                rhi::TextureView* resolvedPrefilterView = context.getTexture(data.resolvedSpecularPrefilter);
                cmdList.copyTexture(&prefilterView->getTexture(), &resolvedPrefilterView->getTexture(), resolveRegions);
            }
        );
    }

}
