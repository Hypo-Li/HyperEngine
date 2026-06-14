#include "SkyLightPass.h"
#include "RenderPassUtils.h"

namespace hyp
{
    void SkyLightPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy& atmosphere
    )
    {
        addCaptureSkyPass(
            renderGraph,
            rendererContext,
            scene,
            view,
            atmosphere
        );
        addGenSkyLightMipmapPass(
            renderGraph,
            rendererContext,
            scene,
            view,
            atmosphere
        );
    }

    struct CaptureSkyData
    {
        SceneRendererContext* rendererContext;
        uint32_t sceneId;
        const AtmosphereProxy* atmosphere;
        rg::TextureHandle transmittanceLut;
        rg::TextureHandle multiScatteringLut;
        rg::TextureHandle skyCubemapMip0;
    };

    struct CaptureSkyStorage
    {
        std::shared_ptr<PassShader> shader;
        std::vector<DescriptorCache<4>> perSceneCaches;
        TRef<vk::Pipeline> pipeline;
    };

    void SkyLightPass::addCaptureSkyPass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy& atmosphere)
    {
        renderGraph.addComputePass<CaptureSkyData>(
            "CaptureSky"_hs,
            [&](rg::RenderGraph::Builder& builder, CaptureSkyData& data)
            {
                data.rendererContext = &rendererContext;
                data.sceneId = scene.getSceneId();
                data.atmosphere = &atmosphere;

                auto& blackboard = builder.getBlackboard();

                auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(data.sceneId));
                data.transmittanceLut = builder.sample(transmittanceLut);
                auto multiScatteringLut = blackboard.get<rg::TextureHandle>("MultiScatteringLut"_hs(data.sceneId));
                data.multiScatteringLut = builder.sample(multiScatteringLut);
                auto skyCubemap = builder.createTexture(
                    "SkyCubemap"_hs(data.sceneId),
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_R16G16B16A16_SFLOAT,
                        VkExtent3D{ 512, 512, 1 },
                        10, 6, 1, true,
                        VK_IMAGE_USAGE_SAMPLED_BIT
                    }
                );

                auto skyCubemapMip0 = builder.createSubTexture(
                    "SkyCubemapMip0"_hs,
                    skyCubemap,
                    rg::SubTextureDescription{
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        0, 1, 0, 6, false
                    }
                );
                data.skyCubemapMip0 = builder.storage(skyCubemapMip0, rg::AccessType::Write_Only);

                skyCubemap = builder.syncTextures(data.skyCubemapMip0, skyCubemap);
                blackboard.put("SkyCubemap"_hs(data.sceneId), skyCubemap);
            },
            [](rg::ExecuteContext& context, CaptureSkyData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<SkyLightPass, CaptureSkyStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "CaptureSky",
                        SHADER_DIR "/Atmosphere/SkyCubemap.slang"
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
                VkImageView skyCubemapMip0 = context.getTexture(data.skyCubemapMip0)->getHandle();
                if (descriptorCache.needUpdate(atmosphereParameters, transmittanceLut, multiScatteringLut, skyCubemapMip0))
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                        { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }
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
                            writer.writeImage(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, skyCubemapMip0, VK_IMAGE_LAYOUT_GENERAL);
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
                cmd.dispatch(512 / 8, 512 / 8, 6);
            }
        );
    }

    struct GenSkyLightMipmapData
    {
        SceneRendererContext* rendererContext;
        uint32_t sceneId;
        const AtmosphereProxy* atmosphere;
        rg::TextureHandle skyCubemapMipTextures[12];
        rg::TextureHandle skyCubemapMipImages[12];
    };

    struct GenSkyLightMipmapStorage
    {
        std::shared_ptr<PassShader> shader;
        std::vector<DescriptorCache<1>> perSceneCaches;
        TRef<vk::Pipeline> pipeline;
    };

    void SkyLightPass::addGenSkyLightMipmapPass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy& atmosphere
    )
    {
        renderGraph.addComputePass<GenSkyLightMipmapData>(
            "GenSkyLightMipmap"_hs,
            [&](rg::RenderGraph::Builder& builder, GenSkyLightMipmapData& data)
            {
                builder.markSideEffect();

                data.rendererContext = &rendererContext;
                data.sceneId = scene.getSceneId();
                data.atmosphere = &atmosphere;

                auto& blackboard = builder.getBlackboard();
                auto skyCubemap = blackboard.get<rg::TextureHandle>("SkyCubemap"_hs(data.sceneId));
                skyCubemap = builder.storage(skyCubemap, rg::AccessType::Read_Write);
                blackboard.put("SkyCubemap"_hs(data.sceneId), skyCubemap);

                uint32_t mipCount = std::bit_width(512u);
                for (uint32_t i = 0; i < mipCount; ++i)
                {
                    data.skyCubemapMipTextures[i] = builder.createSubTexture(
                        "SkyCubemapMipTex"_hs,
                        skyCubemap,
                        rg::SubTextureDescription{
                            VK_IMAGE_ASPECT_COLOR_BIT,
                            i, 1, 0, 6, true
                        }
                    );

                    if (i != 0)
                    {
                        data.skyCubemapMipImages[i] = builder.createSubTexture(
                            "SkyCubemapMipImg"_hs,
                            skyCubemap,
                            rg::SubTextureDescription{
                                VK_IMAGE_ASPECT_COLOR_BIT,
                                i, 1, 0, 6, false
                            }
                        );
                    }
                }
            },
            [](rg::ExecuteContext& context, GenSkyLightMipmapData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<SkyLightPass, GenSkyLightMipmapStorage>();
                auto& renderBackend = data.rendererContext->getRenderBackend();
                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "GenSkyLightMipmap",
                        SHADER_DIR "/Common/CubemapMip.slang"
                    );
                }

                if (storage.perSceneCaches.size() <= data.sceneId)
                {
                    storage.perSceneCaches.resize(data.sceneId + 1);
                }
                auto& descriptorCache = storage.perSceneCaches[data.sceneId];
                vk::ImageView* skyCubemapMip0 = context.getTexture(data.skyCubemapMipTextures[0]);
                if (descriptorCache.needUpdate(skyCubemapMip0->getHandle()))
                {
                    VkDescriptorSetLayoutBinding bindings[] =
                    {
                        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 12, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
                    };
                    descriptorCache.update(
                        renderBackend,
                        bindings,
                        [&](vk::DescriptorWriter& writer)
                        {
                            VkSampler sampler = renderBackend.getDevice().getSamplerCache().getLinearClampNoMip()->getHandle();
                            for (uint32_t i = 0; i < std::bit_width(512u); ++i)
                            {
                                VkImageView tex = context.getTexture(data.skyCubemapMipTextures[i])->getHandle();
                                writer.writeImage(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler, tex, VK_IMAGE_LAYOUT_GENERAL, i);

                                if (i != 0)
                                {
                                    VkImageView img = context.getTexture(data.skyCubemapMipImages[i])->getHandle();
                                    writer.writeImage(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, img, VK_IMAGE_LAYOUT_GENERAL, i);
                                }
                            }
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

                uint32_t mipCount = std::bit_width(512u);

                for (uint32_t i = 1; i < mipCount; ++i)
                {
                    uint32_t currentMipLevel = i;
                    cmd.pushConstants(
                        storage.pipeline->getLayout().getHandle(),
                        VK_SHADER_STAGE_ALL,
                        0,
                        sizeof(uint32_t),
                        &currentMipLevel
                    );

                    cmd.dispatch(
                        std::max((512u >> i) / 8u, 1u),
                        std::max((512u >> i) / 8u, 1u),
                        6
                    );

                    VkImageMemoryBarrier2 barrier = {};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
                    barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR;
                    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
                    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                    barrier.image = skyCubemapMip0->getImage().getHandle();
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier.subresourceRange.baseMipLevel = i;
                    barrier.subresourceRange.levelCount = 1;
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.layerCount = 6;
                    cmd.pipelineBarrier2({ &barrier, 1 });
                }
            }
        );
    }

}
