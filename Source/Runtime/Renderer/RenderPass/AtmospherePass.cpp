#include "AtmospherePass.h"
#include "RenderPassUtils.h"

#include <TransmittanceLut.generated.h>
#include <MultiScatteringLut.generated.h>
#include <SkyViewLut.generated.h>
#include <AerialPerspectiveLut.generated.h>
#include <Atmosphere.generated.h>

namespace hyp
{
    void AtmosphereLutPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy& atmosphere
    )
    {
        addTransmittanceLutPass(renderGraph, rendererContext, scene, atmosphere);
        addMultiScatteringLutPass(renderGraph, rendererContext, scene, atmosphere);
        addSkyViewLutPass(renderGraph, rendererContext, scene, view, atmosphere);
        addAerialPerspectiveLutPass(renderGraph, rendererContext, scene, view, atmosphere);
    }

    struct TransmittanceLutData
    {
        uint32_t sceneId;
        const AtmosphereProxy* atmosphere;
        rg::TextureHandle transmittanceLut;
    };

    struct TransmittanceLutStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<2>> perSceneCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void AtmosphereLutPass::addTransmittanceLutPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const AtmosphereProxy& atmosphere)
    {
        auto& blackboard = renderGraph.getBlackboard();
        HString transmittanceLutName = "TransmittanceLut"_hs(scene.getSceneId());
        auto transmittanceLut = blackboard.get<rg::TextureHandle>(transmittanceLutName);
        if (transmittanceLut.isValid())
        {
            return;
        }

        renderGraph.addComputePass<TransmittanceLutData>(
            "TransmittanceLut"_hs,
            [&, transmittanceLutName](rg::RenderGraph::Builder& builder, TransmittanceLutData& data)
            {
                data.sceneId = scene.getSceneId();
                data.atmosphere = &atmosphere;

                rhi::TextureDesc desc;
                desc.type = rhi::TextureType::Texture_2D;
                desc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                desc.extent = { 256, 64, 1 };
                desc.mipCount = 1;
                desc.layerCount = 1;
                desc.samples = 1;
                desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto transmittanceLut = builder.createTexture(transmittanceLutName, desc);

                data.transmittanceLut = builder.storage(transmittanceLut, rg::AccessType::Write_Only);
                builder.getBlackboard().put(transmittanceLutName, data.transmittanceLut);
            },
            [&rendererContext](rg::ExecuteContext& context, TransmittanceLutData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<AtmosphereLutPass, TransmittanceLutStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "TransmittanceLut",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(TransmittanceLut, Compute))
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kTransmittanceLut_Bindings);
                }

                if (storage.perSceneCaches.size() <= data.sceneId)
                {
                    storage.perSceneCaches.resize(data.sceneId + 1);
                }
                auto& bindGroupCache = storage.perSceneCaches[data.sceneId];

                rhi::Buffer* atmosphereParameters = &data.atmosphere->getParametersBuffer();
                rhi::TextureView* transmittanceLut = context.getTexture(data.transmittanceLut);
                if (bindGroupCache.needUpdate(atmosphereParameters, transmittanceLut))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kTransmittanceLut_Bindings);
                            writer.setBuffer("Atmosphere", atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.setTexture("TransmittanceLutImage", transmittanceLut);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createComputePipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout
                    );
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.pipeline);
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Compute,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.dispatch(32, 8, 1);
            }
        );
    }

    struct MultiScatteringLutData
    {
        uint32_t sceneId;
        const AtmosphereProxy* atmosphere;
        rg::TextureHandle transmittanceLut;
        rg::TextureHandle multiScatteringLut;
    };

    struct MultiScatteringLutStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<4>> perSceneCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void AtmosphereLutPass::addMultiScatteringLutPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const AtmosphereProxy& atmosphere
    )
    {
        auto& blackboard = renderGraph.getBlackboard();
        HString multiScatteringLutName = "MultiScatteringLut"_hs(scene.getSceneId());
        auto multiScatteringLut = blackboard.get<rg::TextureHandle>(multiScatteringLutName);
        if (multiScatteringLut.isValid())
        {
            return;
        }

        renderGraph.addComputePass<MultiScatteringLutData>(
            "MultiScatteringLut"_hs,
            [&, multiScatteringLutName](rg::RenderGraph::Builder& builder, MultiScatteringLutData& data)
            {
                data.sceneId = scene.getSceneId();
                data.atmosphere = &atmosphere;

                rhi::TextureDesc desc;
                desc.type = rhi::TextureType::Texture_2D;
                desc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                desc.extent = { 32, 32, 1 };
                desc.mipCount = 1;
                desc.layerCount = 1;
                desc.samples = 1;
                desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto multiScatteringLut = builder.createTexture(multiScatteringLutName, desc);

                auto& blackboard = builder.getBlackboard();
                auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(scene.getSceneId()));
                assert(transmittanceLut.isValid());
                data.transmittanceLut = builder.sample(transmittanceLut);
                data.multiScatteringLut = builder.storage(multiScatteringLut, rg::AccessType::Write_Only);
                builder.getBlackboard().put(multiScatteringLutName, data.multiScatteringLut);
            },
            [&rendererContext](rg::ExecuteContext& context, MultiScatteringLutData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<AtmosphereLutPass, MultiScatteringLutStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "MultiScatteringLut",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(MultiScatteringLut, Compute))
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kMultiScatteringLut_Bindings);
                }

                if (storage.perSceneCaches.size() <= data.sceneId)
                {
                    storage.perSceneCaches.resize(data.sceneId + 1);
                }
                auto& bindGroupCache = storage.perSceneCaches[data.sceneId];

                rhi::Buffer* atmosphereParameters = &data.atmosphere->getParametersBuffer();
                rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                rhi::TextureView* transmittanceLut = context.getTexture(data.transmittanceLut);
                rhi::TextureView* multiScatteringLut = context.getTexture(data.multiScatteringLut);

                if (bindGroupCache.needUpdate(atmosphereParameters, linearClampNoMipSampler, transmittanceLut, multiScatteringLut))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kMultiScatteringLut_Bindings);
                            writer.setBuffer("Atmosphere", atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.setSampler("LutSampler", linearClampNoMipSampler);
                            writer.setTexture("TransmittanceLutTexture", transmittanceLut);
                            writer.setTexture("MultiScatteringLutImage", multiScatteringLut);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createComputePipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout
                    );
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.pipeline);
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Compute,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.dispatch(4, 4, 1);
            }
        );
    }

    struct SkyViewLutData
    {
        uint32_t viewId;
        const AtmosphereProxy* atmosphere;
        rg::TextureHandle transmittanceLut;
        rg::TextureHandle multiScatteringLut;
        rg::TextureHandle skyViewLut;
    };

    struct SkyViewLutStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<5>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void AtmosphereLutPass::addSkyViewLutPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy& atmosphere
    )
    {
        renderGraph.addComputePass<SkyViewLutData>(
            "SkyViewLut"_hs,
            [&](rg::RenderGraph::Builder& builder, SkyViewLutData& data)
            {
                data.viewId = view.getViewId();
                data.atmosphere = &atmosphere;

                rhi::TextureDesc desc;
                desc.type = rhi::TextureType::Texture_2D;
                desc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                desc.extent = { 192, 104, 1 };
                desc.mipCount = 1;
                desc.layerCount = 1;
                desc.samples = 1;
                desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto skyViewLut = builder.createTexture("SkyViewLut"_hs(data.viewId), desc);

                auto& blackboard = builder.getBlackboard();
                auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(scene.getSceneId()));
                auto multiScatteringLut = blackboard.get<rg::TextureHandle>("MultiScatteringLut"_hs(scene.getSceneId()));
                assert(transmittanceLut.isValid() && multiScatteringLut.isValid());
                data.transmittanceLut = builder.sample(transmittanceLut);
                data.multiScatteringLut = builder.sample(multiScatteringLut);
                data.skyViewLut = builder.storage(skyViewLut, rg::AccessType::Write_Only);
                builder.getBlackboard().put("SkyViewLut"_hs(data.viewId), data.skyViewLut);
            },
            [&rendererContext](rg::ExecuteContext& context, SkyViewLutData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<AtmosphereLutPass, SkyViewLutStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "SkyViewLut",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(SkyViewLut, Compute))
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kSkyViewLut_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                rhi::Buffer* atmosphereParameters = &data.atmosphere->getParametersBuffer();
                rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                rhi::TextureView* transmittanceLut = context.getTexture(data.transmittanceLut);
                rhi::TextureView* multiScatteringLut = context.getTexture(data.multiScatteringLut);
                rhi::TextureView* skyViewLut = context.getTexture(data.skyViewLut);

                if (bindGroupCache.needUpdate(atmosphereParameters, linearClampNoMipSampler, transmittanceLut, multiScatteringLut, skyViewLut))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kSkyViewLut_Bindings);
                            writer.setBuffer("Atmosphere", atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.setSampler("LutSampler", linearClampNoMipSampler);
                            writer.setTexture("TransmittanceLutTexture", transmittanceLut);
                            writer.setTexture("MultiScatteringLutTexture", multiScatteringLut);
                            writer.setTexture("SkyViewLutImage", skyViewLut);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createComputePipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout
                    );
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.pipeline);
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Compute,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.dispatch(24, 13, 1);
            }
        );
    }

    struct AerialPerspectiveLutData
    {
        uint32_t viewId;
        const AtmosphereProxy* atmosphere;
        rg::TextureHandle transmittanceLut;
        rg::TextureHandle multiScatteringLut;
        rg::TextureHandle aerialPerspectiveLut;
    };

    struct AerialPerspectiveLutStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<5>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void AtmosphereLutPass::addAerialPerspectiveLutPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy& atmosphere
    )
    {
        renderGraph.addComputePass<AerialPerspectiveLutData>(
            "AerialPerspectiveLutPass"_hs,
            [&](rg::RenderGraph::Builder& builder, AerialPerspectiveLutData& data)
            {
                data.viewId = view.getViewId();
                data.atmosphere = &atmosphere;

                rhi::TextureDesc desc;
                desc.type = rhi::TextureType::Texture_3D;
                desc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                desc.extent = { 32, 32, 32 };
                desc.mipCount = 1;
                desc.layerCount = 1;
                desc.samples = 1;
                desc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto aerialPerspectiveLut = builder.createTexture("AerialPerspectiveLut"_hs(data.viewId), desc);

                auto& blackboard = builder.getBlackboard();
                auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(scene.getSceneId()));
                auto multiScatteringLut = blackboard.get<rg::TextureHandle>("MultiScatteringLut"_hs(scene.getSceneId()));
                assert(transmittanceLut.isValid() && multiScatteringLut.isValid());
                data.transmittanceLut = builder.sample(transmittanceLut);
                data.multiScatteringLut = builder.sample(multiScatteringLut);
                data.aerialPerspectiveLut = builder.storage(aerialPerspectiveLut, rg::AccessType::Write_Only);
                builder.getBlackboard().put("AerialPerspectiveLut"_hs(data.viewId), data.aerialPerspectiveLut);
            },
            [&rendererContext](rg::ExecuteContext& context, AerialPerspectiveLutData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<AtmosphereLutPass, AerialPerspectiveLutStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "AerialPerspectiveLut",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(AerialPerspectiveLut, Compute))
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kAerialPerspectiveLut_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                rhi::Buffer* atmosphereParameters = &data.atmosphere->getParametersBuffer();
                rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                rhi::TextureView* transmittanceLut = context.getTexture(data.transmittanceLut);
                rhi::TextureView* multiScatteringLut = context.getTexture(data.multiScatteringLut);
                rhi::TextureView* aerialPerspectiveLut = context.getTexture(data.aerialPerspectiveLut);

                if (bindGroupCache.needUpdate(atmosphereParameters, linearClampNoMipSampler, transmittanceLut, multiScatteringLut, aerialPerspectiveLut))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kAerialPerspectiveLut_Bindings);
                            writer.setBuffer("Atmosphere", atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.setSampler("LutSampler", linearClampNoMipSampler);
                            writer.setTexture("TransmittanceLutTexture", transmittanceLut);
                            writer.setTexture("MultiScatteringLutTexture", multiScatteringLut);
                            writer.setTexture("AerialPerspectiveLutImage", aerialPerspectiveLut);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createComputePipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout
                    );
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.pipeline);
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Compute,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.dispatch(8, 8, 8);
            }
        );
    }

    struct AtmosphereData
    {
        uint32_t viewId;
        float4 viewport;
        const AtmosphereProxy* atmosphere;
        rg::TextureHandle transmittanceLut;
        rg::TextureHandle multiScatteringLut;
        rg::TextureHandle skyViewLut;
        rg::TextureHandle aerialPerspectiveLut;
        rg::TextureHandle sceneDepthD;
        rg::TextureHandle sceneColor;
    };

    struct AtmosphereStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<7>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void AtmospherePass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy& atmosphere
    )
    {
        renderGraph.addGraphicsPass<AtmosphereData>(
            "Atmosphere"_hs,
            [&](rg::RenderGraph::Builder& builder, AtmosphereData& data)
            {
                data.viewId = view.getViewId();
                data.viewport = view.getViewport();
                data.atmosphere = &atmosphere;
                auto& blackboard = builder.getBlackboard();
                uint32_t sceneId = scene.getSceneId();
                auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(sceneId));
                auto multiScatteringLut = blackboard.get<rg::TextureHandle>("MultiScatteringLut"_hs(sceneId));
                auto skyViewLut = blackboard.get<rg::TextureHandle>("SkyViewLut"_hs(data.viewId));
                auto aerialPerspectiveLut = blackboard.get<rg::TextureHandle>("AerialPerspectiveLut"_hs(data.viewId));
                auto sceneDepthD = blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(data.viewId));
                auto sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(data.viewId));

                data.transmittanceLut = builder.sample(transmittanceLut);
                data.multiScatteringLut = builder.sample(multiScatteringLut);
                data.skyViewLut = builder.sample(skyViewLut);
                data.aerialPerspectiveLut = builder.sample(aerialPerspectiveLut);
                data.sceneDepthD = builder.sample(sceneDepthD);
                data.sceneColor = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Load);
                blackboard.put("SceneColor"_hs(data.viewId), data.sceneColor);
            },
            [&rendererContext](rg::ExecuteContext& context, AtmosphereData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<AtmospherePass, AtmosphereStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "Atmosphere",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(Atmosphere, Vertex),
                            GET_PASS_SHADER_BYTECODE(Atmosphere, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kAtmosphere_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                rhi::Buffer* atmosphereParameters = &data.atmosphere->getParametersBuffer();
                rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                rhi::TextureView* transmittanceLut = context.getTexture(data.transmittanceLut);
                rhi::TextureView* multiScatteringLut = context.getTexture(data.multiScatteringLut);
                rhi::TextureView* skyViewLut = context.getTexture(data.skyViewLut);
                rhi::TextureView* aerialPerspectiveLut = context.getTexture(data.aerialPerspectiveLut);
                rhi::TextureView* sceneDepthD = context.getTexture(data.sceneDepthD);

                if (bindGroupCache.needUpdate(
                    atmosphereParameters,
                    linearClampNoMipSampler,
                    transmittanceLut,
                    multiScatteringLut,
                    skyViewLut,
                    aerialPerspectiveLut,
                    sceneDepthD))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kAtmosphere_Bindings);
                            writer.setBuffer("Atmosphere", atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.setSampler("LutSampler", linearClampNoMipSampler);
                            writer.setTexture("TransmittanceLutTexture", transmittanceLut);
                            writer.setTexture("MultiScatteringLutTexture", multiScatteringLut);
                            writer.setTexture("SkyViewLutTexture", skyViewLut);
                            writer.setTexture("AerialPerspectiveLutTexture", aerialPerspectiveLut);
                            writer.setTexture("SceneDepthImage", sceneDepthD);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createAtmospherePipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout
                    );
                }

                auto& cmdList = context.getCommandList();

                setViewportAndScissor(cmdList, data.viewport);

                cmdList.bindPipeline(storage.pipeline);
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.draw(3, 1, 0, 0);
            }
        );
    }

    rhi::Pipeline* AtmospherePass::createAtmospherePipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& bindGroupLayout
    )
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::fullscreenTriangle();
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.colorBlendState = ColorBlendState::get<
            rhi::ColorBlendAttachmentState{ true, rhi::BlendFactor::One, rhi::BlendFactor::Src_Alpha, rhi::BlendOp::Add, rhi::BlendFactor::Zero, rhi::BlendFactor::One, rhi::BlendOp::Add, 0xFF }
        >();
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            rhi::TextureFormat::Undefined,
            rhi::TextureFormat::RGBA16_SFLOAT
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
