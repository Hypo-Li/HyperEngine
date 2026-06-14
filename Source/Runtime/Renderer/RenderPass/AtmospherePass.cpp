#include "AtmospherePass.h"
#include "RenderPassUtils.h"

namespace hyp
{
    void AtmospherePass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy& atmosphere
    )
    {
        addTransmittanceLutPass(renderGraph, rendererContext, scene, atmosphere);
        addMultiScatteringLutPass(renderGraph, rendererContext, scene, atmosphere);
        addSkyViewLutPass(renderGraph, rendererContext, scene, view, atmosphere);
        addAerialPerspectiveLutPass(renderGraph, rendererContext, scene, view, atmosphere);
        addAtmospherePass(renderGraph, rendererContext, scene, view, atmosphere);
    }

    struct TransmittanceLutData
    {
        SceneRendererContext* rendererContext;
        uint32_t sceneId;
        const AtmosphereProxy* atmosphere;
        rg::TextureHandle transmittanceLut;
    };

    struct TransmittanceLutStorage
    {
        std::shared_ptr<PassShader> shader;
        std::vector<DescriptorCache<2>> perSceneCaches;
        TRef<vk::Pipeline> pipeline;
    };

    void AtmospherePass::addTransmittanceLutPass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
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
                data.rendererContext = &rendererContext;
                data.sceneId = scene.getSceneId();
                data.atmosphere = &atmosphere;
                auto transmittanceLut = builder.createTexture(
                    transmittanceLutName,
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
                        VkExtent3D{ 256, 64, 1 },
                        1, 1, 1
                    }
                );
                data.transmittanceLut = builder.storage(transmittanceLut, rg::AccessType::Write_Only);
                builder.getBlackboard().put(transmittanceLutName, data.transmittanceLut);
            },
            [](rg::ExecuteContext& context, TransmittanceLutData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<AtmospherePass, TransmittanceLutStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "TransmittanceLut",
                        SHADER_DIR "/Atmosphere/TransmittanceLut.slang"
                    );
                }

                if (storage.perSceneCaches.size() <= data.sceneId)
                {
                    storage.perSceneCaches.resize(data.sceneId + 1);
                }
                auto& descriptorCache = storage.perSceneCaches[data.sceneId];

                VkBuffer atmosphereParameters = data.atmosphere->getParametersBuffer().getHandle();
                VkImageView transmittanceLut = context.getTexture(data.transmittanceLut)->getHandle();
                if (descriptorCache.needUpdate(atmosphereParameters, transmittanceLut))
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // AtmosphereParams
                        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // TransmittanceLut
                    };
                    descriptorCache.update(
                        renderBackend,
                        bindings,
                        [&](vk::DescriptorWriter& writer)
                        {
                            writer.writeBuffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.writeImage(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, transmittanceLut, VK_IMAGE_LAYOUT_GENERAL);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createComputePipeline(
                        *data.rendererContext,
                        storage.shader->getOrCreateVariant({}),
                        descriptorCache.getDescriptorSet()->getLayout()
                    );
                }

                auto& cmd = context.getCommandBuffer();
                cmd.bindPipeline(storage.pipeline->getBindPoint(), storage.pipeline->getHandle());
                VkDescriptorSet descriptorSets[] = { descriptorCache.getDescriptorSet()->getHandle() };
                cmd.bindDescriptorSets(
                    storage.pipeline->getBindPoint(),
                    storage.pipeline->getLayout().getHandle(),
                    2,
                    descriptorSets,
                    {}
                );
                cmd.dispatch(32, 8, 1);
            }
        );
    }

    struct MultiScatteringLutData
    {
        SceneRendererContext* rendererContext;
        uint32_t sceneId;
        const AtmosphereProxy* atmosphere;
        rg::TextureHandle transmittanceLut;
        rg::TextureHandle multiScatteringLut;
    };

    struct MultiScatteringLutStorage
    {
        std::shared_ptr<PassShader> shader;
        std::vector<DescriptorCache<3>> perSceneCaches;
        TRef<vk::Pipeline> pipeline;
    };

    void AtmospherePass::addMultiScatteringLutPass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
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
                data.rendererContext = &rendererContext;
                data.sceneId = scene.getSceneId();
                data.atmosphere = &atmosphere;
                auto multiScatteringLut = builder.createTexture(
                    multiScatteringLutName,
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
                        VkExtent3D{ 32, 32, 1 },
                        1, 1, 1
                    }
                );
                auto& blackboard = builder.getBlackboard();
                auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(scene.getSceneId()));
                assert(transmittanceLut.isValid());
                data.transmittanceLut = builder.sample(transmittanceLut);
                data.multiScatteringLut = builder.storage(multiScatteringLut, rg::AccessType::Write_Only);
                builder.getBlackboard().put(multiScatteringLutName, data.multiScatteringLut);
            },
            [](rg::ExecuteContext& context, MultiScatteringLutData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<AtmospherePass, MultiScatteringLutStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "MultiScatteringLut",
                        SHADER_DIR "/Atmosphere/MultiScatteringLut.slang"
                    );
                }

                if (storage.perSceneCaches.size() <= data.sceneId)
                {
                    storage.perSceneCaches.resize(data.sceneId + 1);
                }
                auto& descriptorCache = storage.perSceneCaches[data.sceneId];

                VkBuffer atmosphereParameters = data.atmosphere->getParametersBuffer().getHandle();
                VkImageView transmittanceLut = context.getTexture(data.transmittanceLut)->getHandle();
                VkImageView multiScatteringLut = context.getTexture(data.multiScatteringLut)->getHandle();
                if (descriptorCache.needUpdate(atmosphereParameters, transmittanceLut, multiScatteringLut))
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // AtmosphereParams
                        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // TransmittanceLut
                        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // MultiScatteringLut
                    };
                    descriptorCache.update(
                        renderBackend,
                        bindings,
                        [&](vk::DescriptorWriter& writer)
                        {
                            VkSampler sampler = renderBackend.getDevice().getSamplerCache().getLinearClampNoMip()->getHandle();
                            writer.writeBuffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.writeImage(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler, transmittanceLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, multiScatteringLut, VK_IMAGE_LAYOUT_GENERAL);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createComputePipeline(
                        *data.rendererContext,
                        storage.shader->getOrCreateVariant({}),
                        descriptorCache.getDescriptorSet()->getLayout()
                    );
                }

                auto& cmd = context.getCommandBuffer();
                cmd.bindPipeline(storage.pipeline->getBindPoint(), storage.pipeline->getHandle());
                VkDescriptorSet descriptorSets[] = { descriptorCache.getDescriptorSet()->getHandle()};
                cmd.bindDescriptorSets(
                    storage.pipeline->getBindPoint(),
                    storage.pipeline->getLayout().getHandle(),
                    2,
                    descriptorSets,
                    {}
                );
                cmd.dispatch(4, 4, 1);
            }
        );
    }

    struct SkyViewLutData
    {
        SceneRendererContext* rendererContext;
        uint32_t viewId;
        const AtmosphereProxy* atmosphere;
        rg::TextureHandle transmittanceLut;
        rg::TextureHandle multiScatteringLut;
        rg::TextureHandle skyViewLut;
    };

    struct SkyViewLutStorage
    {
        std::shared_ptr<PassShader> shader;
        std::vector<DescriptorCache<4>> perViewCaches;
        TRef<vk::Pipeline> pipeline;
    };

    void AtmospherePass::addSkyViewLutPass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy& atmosphere
    )
    {
        renderGraph.addComputePass<SkyViewLutData>(
            "SkyViewLut"_hs,
            [&](rg::RenderGraph::Builder& builder, SkyViewLutData& data)
            {
                data.rendererContext = &rendererContext;
                data.viewId = view.getViewId();
                data.atmosphere = &atmosphere;
                auto skyViewLut = builder.createTexture(
                    "SkyViewLut"_hs(data.viewId),
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
                        VkExtent3D{ 192, 104, 1 },
                        1, 1, 1
                    }
                );
                auto& blackboard = builder.getBlackboard();
                auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(scene.getSceneId()));
                auto multiScatteringLut = blackboard.get<rg::TextureHandle>("MultiScatteringLut"_hs(scene.getSceneId()));
                assert(transmittanceLut.isValid() && multiScatteringLut.isValid());
                data.transmittanceLut = builder.sample(transmittanceLut);
                data.multiScatteringLut = builder.sample(multiScatteringLut);
                data.skyViewLut = builder.storage(skyViewLut, rg::AccessType::Write_Only);
                builder.getBlackboard().put("SkyViewLut"_hs(data.viewId), data.skyViewLut);
            },
            [](rg::ExecuteContext& context, SkyViewLutData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<AtmospherePass, SkyViewLutStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "SkyViewLut",
                        SHADER_DIR "/Atmosphere/SkyViewLut.slang"
                    );
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& descriptorCache = storage.perViewCaches[data.viewId];

                VkBuffer atmosphereParameters = data.atmosphere->getParametersBuffer().getHandle();
                VkImageView transmittanceLut = context.getTexture(data.transmittanceLut)->getHandle();
                VkImageView multiScatteringLut = context.getTexture(data.multiScatteringLut)->getHandle();
                VkImageView skyViewLut = context.getTexture(data.skyViewLut)->getHandle();
                if (descriptorCache.needUpdate(atmosphereParameters, transmittanceLut, multiScatteringLut, skyViewLut))
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                       { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // AtmosphereParams
                       { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // TransmittanceLut
                       { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // MultiScatteringLut
                       { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // SkyViewLut
                    };
                    descriptorCache.update(
                        renderBackend,
                        bindings,
                        [&](vk::DescriptorWriter& writer)
                        {
                            VkSampler sampler = renderBackend.getDevice().getSamplerCache().getLinearClampNoMip()->getHandle();
                            writer.writeBuffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.writeImage(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler, transmittanceLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler, multiScatteringLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, skyViewLut, VK_IMAGE_LAYOUT_GENERAL);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createComputePipeline(
                        *data.rendererContext,
                        storage.shader->getOrCreateVariant({}),
                        descriptorCache.getDescriptorSet()->getLayout()
                    );
                }

                auto& cmd = context.getCommandBuffer();
                cmd.bindPipeline(storage.pipeline->getBindPoint(), storage.pipeline->getHandle());
                VkDescriptorSet descriptorSets[] = { descriptorCache.getDescriptorSet()->getHandle()};
                cmd.bindDescriptorSets(
                    storage.pipeline->getBindPoint(),
                    storage.pipeline->getLayout().getHandle(),
                    2,
                    descriptorSets,
                    {}
                );
                cmd.dispatch(24, 13, 1);
            }
        );
    }

    struct AerialPerspectiveLutData
    {
        SceneRendererContext* rendererContext;
        uint32_t viewId;
        const AtmosphereProxy* atmosphere;
        rg::TextureHandle transmittanceLut;
        rg::TextureHandle multiScatteringLut;
        rg::TextureHandle aerialPerspectiveLut;
    };

    struct AerialPerspectiveLutStorage
    {
        std::shared_ptr<PassShader> shader;
        std::vector<DescriptorCache<4>> perViewCaches;
        TRef<vk::Pipeline> pipeline;
    };

    void AtmospherePass::addAerialPerspectiveLutPass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy& atmosphere
    )
    {
        renderGraph.addComputePass<AerialPerspectiveLutData>(
            "AerialPerspectiveLutPass"_hs,
            [&](rg::RenderGraph::Builder& builder, AerialPerspectiveLutData& data)
            {
                data.rendererContext = &rendererContext;
                data.viewId = view.getViewId();
                data.atmosphere = &atmosphere;
                auto aerialPerspectiveLut = builder.createTexture(
                    "AerialPerspectiveLut"_hs(data.viewId),
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_3D,
                        VK_FORMAT_R16G16B16A16_SFLOAT,
                        VkExtent3D{ 32, 32, 32 },
                        1, 1, 1
                    }
                );
                auto& blackboard = builder.getBlackboard();
                auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(scene.getSceneId()));
                auto multiScatteringLut = blackboard.get<rg::TextureHandle>("MultiScatteringLut"_hs(scene.getSceneId()));
                assert(transmittanceLut.isValid() && multiScatteringLut.isValid());
                data.transmittanceLut = builder.sample(transmittanceLut);
                data.multiScatteringLut = builder.sample(multiScatteringLut);
                data.aerialPerspectiveLut = builder.storage(aerialPerspectiveLut, rg::AccessType::Write_Only);
                builder.getBlackboard().put("AerialPerspectiveLut"_hs(data.viewId), data.aerialPerspectiveLut);
            },
            [](rg::ExecuteContext& context, AerialPerspectiveLutData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<AtmospherePass, AerialPerspectiveLutStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "AerialPerspectiveLut",
                        SHADER_DIR "/Atmosphere/AerialPerspectiveLut.slang"
                    );
                }
                
                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& descriptorCache = storage.perViewCaches[data.viewId];
                
                VkBuffer atmosphereParameters = data.atmosphere->getParametersBuffer().getHandle();
                VkImageView transmittanceLut = context.getTexture(data.transmittanceLut)->getHandle();
                VkImageView multiScatteringLut = context.getTexture(data.multiScatteringLut)->getHandle();
                VkImageView aerialPerspectiveLut = context.getTexture(data.aerialPerspectiveLut)->getHandle();
                if (descriptorCache.needUpdate(atmosphereParameters, transmittanceLut, multiScatteringLut, aerialPerspectiveLut))
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // AtmosphereParams
                        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // TransmittanceLut
                        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // MultiScatteringLut
                        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // AerialPerspectiveLut
                    };
                    descriptorCache.update(
                        renderBackend,
                        bindings,
                        [&](vk::DescriptorWriter& writer)
                        {
                            VkSampler sampler = renderBackend.getDevice().getSamplerCache().getLinearClampNoMip()->getHandle();
                            writer.writeBuffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.writeImage(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler, transmittanceLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler, multiScatteringLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, aerialPerspectiveLut, VK_IMAGE_LAYOUT_GENERAL);
                        }
                    );
                }
                
                if (!storage.pipeline)
                {
                    storage.pipeline = createComputePipeline(
                        *data.rendererContext,
                        storage.shader->getOrCreateVariant({}),
                        descriptorCache.getDescriptorSet()->getLayout()
                    );
                }
                
                auto& cmd = context.getCommandBuffer();
                cmd.bindPipeline(storage.pipeline->getBindPoint(), storage.pipeline->getHandle());
                VkDescriptorSet descriptorSets[] = { descriptorCache.getDescriptorSet()->getHandle()};
                cmd.bindDescriptorSets(
                    storage.pipeline->getBindPoint(),
                    storage.pipeline->getLayout().getHandle(),
                    2,
                    descriptorSets,
                    {}
                );
                cmd.dispatch(8, 8, 8);
            }
        );
    }

    struct AtmosphereData
    {
        SceneRendererContext* rendererContext;
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
        std::vector<DescriptorCache<6>> perViewCaches;
        TRef<vk::Pipeline> pipeline;
    };

    void AtmospherePass::addAtmospherePass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy& atmosphere
    )
    {
        renderGraph.addGraphicsPass<AtmosphereData>(
            "Atmosphere"_hs,
            [&](rg::RenderGraph::Builder& builder, AtmosphereData& data)
            {
                data.rendererContext = &rendererContext;
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
                data.sceneColor = builder.addColorAttachment(sceneColor, VK_ATTACHMENT_LOAD_OP_LOAD);
                blackboard.put("SceneColor"_hs(data.viewId), data.sceneColor);
            },
            [](rg::ExecuteContext& context, AtmosphereData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<AtmospherePass, AtmosphereStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "Atmosphere",
                        SHADER_DIR "/Atmosphere/Atmosphere.slang"
                    );
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& descriptorCache = storage.perViewCaches[data.viewId];

                VkBuffer atmosphereParameters = data.atmosphere->getParametersBuffer().getHandle();
                VkImageView transmittanceLut = context.getTexture(data.transmittanceLut)->getHandle();
                VkImageView multiScatteringLut = context.getTexture(data.multiScatteringLut)->getHandle();
                VkImageView skyViewLut = context.getTexture(data.skyViewLut)->getHandle();
                VkImageView aerialPerspectiveLut = context.getTexture(data.aerialPerspectiveLut)->getHandle();
                VkImageView sceneDepthD = context.getTexture(data.sceneDepthD)->getHandle();
                if (descriptorCache.needUpdate(atmosphereParameters, transmittanceLut, multiScatteringLut, skyViewLut, aerialPerspectiveLut, sceneDepthD))
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }, // AtmosphereParams
                        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }, // TransmittanceLut
                        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }, // MultiScatteringLut
                        { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }, // SkyViewLut
                        { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr }, // AerialPerspectiveLut
                        { 5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr} // SceneDepthD
                    };
                    descriptorCache.update(
                        renderBackend,
                        bindings,
                        [&](vk::DescriptorWriter& writer)
                        {
                            VkSampler sampler = renderBackend.getDevice().getSamplerCache().getLinearClampNoMip()->getHandle();
                            writer.writeBuffer(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.writeImage(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler, transmittanceLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler, multiScatteringLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler, skyViewLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler, aerialPerspectiveLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            writer.writeImage(5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, sceneDepthD, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createAtmospherePipeline(
                        *data.rendererContext,
                        storage.shader->getOrCreateVariant({}),
                        descriptorCache.getDescriptorSet()->getLayout()
                    );
                }

                auto& cmd = context.getCommandBuffer();

                setViewportAndScissor(cmd, data.viewport);

                cmd.bindPipeline(storage.pipeline->getBindPoint(), storage.pipeline->getHandle());
                VkDescriptorSet descriptorSets[] = {
                    descriptorCache.getDescriptorSet()->getHandle()
                };
                cmd.bindDescriptorSets(
                    storage.pipeline->getBindPoint(),
                    storage.pipeline->getLayout().getHandle(),
                    2,
                    descriptorSets,
                    {}
                );
                cmd.draw(3, 1, 0, 0);
            }
        );
    }

    vk::Pipeline* AtmospherePass::createAtmospherePipeline(
        SceneRendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const vk::DescriptorSetLayout& descriptorSetLayout
    )
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, descriptorSetLayout.getHandle());
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.topology = static_cast<uint32_t>(Topology::Triangle_List);
        pipelineKey.pipelineState.samples = static_cast<uint32_t>(SampleCount::Count_1);
        pipelineKey.pipelineState.polygonMode = static_cast<uint32_t>(PolygonMode::Fill);
        pipelineKey.pipelineState.cullMode = static_cast<uint32_t>(CullMode::Back);
        pipelineKey.pipelineState.frontFace = static_cast<uint32_t>(FrontFace::Counter_Clockwise);
        pipelineKey.pipelineState.depthTestEnable = 0;
        pipelineKey.pipelineState.depthWriteEnable = 0;
        pipelineKey.pipelineState.depthCompareOp = static_cast<uint32_t>(CompareOp::Greater_Or_Equal);
        pipelineKey.pipelineState.stencilTestEnable = 0;
        pipelineKey.pipelineState.stencilState = StencilState::get();
        pipelineKey.pipelineState.colorBlendState = ColorBlendState::get <
            AttachmentBlendState{ VK_TRUE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, vk::kColorComponentAll }
        > ();
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_R16G16B16A16_SFLOAT
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

}
