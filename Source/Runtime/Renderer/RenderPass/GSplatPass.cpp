#include "GSplatPass.h"
#include "RenderPassUtils.h"
#define VRDX_IMPLEMENTATION
#include <vk_radix_sort.h>

namespace hyp
{
    void GSplatPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneView& view,
        const GSplatScene& gsplatScene
    )
    {
        addGSplatCullingPass(renderGraph, rendererContext, view, gsplatScene);
        addGSplatSortPass(renderGraph, rendererContext, view, gsplatScene);
        addGSplatRenderPass(renderGraph, rendererContext, view, gsplatScene);
        //addGSplatDepthPass(renderGraph, rendererContext, view, gsplatScene);
        //addGSplatRelightingPass(renderGraph, rendererContext, view, gsplatScene);
    }

    struct GSplatCullingData
    {
        SceneRendererContext* rendererContext;
        uint32_t viewId;
        const GSplatScene* gsplatScene;
        rg::BufferHandle gsplatDrawIndirect;
        rg::BufferHandle splatIndices;
        rg::BufferHandle splatDistances;
    };

    struct GSplatCullingStorage
    {
        std::shared_ptr<PassShader> shader;
        std::vector<DescriptorCache<4>> descriptorCaches;
        TRef<vk::Pipeline> pipeline;
    };

    void GSplatPass::addGSplatCullingPass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneView& view,
        const GSplatScene& gsplatScene
    )
    {
        renderGraph.addComputePass<GSplatCullingData>(
            "GSplatCulling"_hs,
            [&](rg::RenderGraph::Builder& builder, GSplatCullingData& data)
            {
                data.rendererContext = &rendererContext;
                data.viewId = view.getViewId();
                data.gsplatScene = &gsplatScene;
                auto& blackboard = builder.getBlackboard();

                auto splatIndices = builder.createBuffer(
                    "SplatIndices"_hs(data.viewId),
                    rg::BufferDescription{ sizeof(uint32_t) * gsplatScene.getTotalSplatCount() }
                );
                data.splatIndices = builder.accessBuffer(
                    splatIndices,
                    rg::BufferUsage::Storage,
                    rg::AccessType::Write_Only
                );
                blackboard.put("SplatIndices"_hs(data.viewId), data.splatIndices);

                auto gsplatDrawIndirect = builder.createBuffer(
                    "GSplatDrawIndirect"_hs(data.viewId),
                    rg::BufferDescription{ sizeof(VkDrawIndirectCommand), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT }
                );
                data.gsplatDrawIndirect = builder.accessBuffer(
                    gsplatDrawIndirect,
                    rg::BufferUsage::Storage,
                    rg::AccessType::Write_Only
                );
                blackboard.put("GSplatDrawIndirect"_hs(data.viewId), data.gsplatDrawIndirect);

                auto splatDistances = builder.createBuffer(
                    "SplatDistances"_hs(data.viewId),
                    rg::BufferDescription{ sizeof(float) * gsplatScene.getTotalSplatCount() }
                );
                data.splatDistances = builder.accessBuffer(
                    splatDistances,
                    rg::BufferUsage::Storage,
                    rg::AccessType::Write_Only
                );
                blackboard.put("SplatDistances"_hs(data.viewId), data.splatDistances);
            },
            [](rg::ExecuteContext& context, GSplatCullingData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<GSplatPass, GSplatCullingStorage>();

                auto& renderBackend = context.getRenderBackend();

                bool firstCulling = storage.shader == nullptr;
                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "GSplatCulling",
                        SHADER_DIR "/GSplat/Culling.slang"
                    );
                }

                if (storage.descriptorCaches.size() <= data.viewId)
                {
                    storage.descriptorCaches.resize(data.viewId + 1);
                }
                auto& descriptorCache = storage.descriptorCaches[data.viewId];

                VkBuffer splatData = data.gsplatScene->getSplatDataBuffer().getHandle();
                VkBuffer splatIndices = context.getBuffer(data.splatIndices)->getHandle();
                VkBuffer gsplatDrawIndirect = context.getBuffer(data.gsplatDrawIndirect)->getHandle();
                VkBuffer splatDistances = context.getBuffer(data.splatDistances)->getHandle();
                if (descriptorCache.needUpdate(splatData, splatIndices, gsplatDrawIndirect, splatDistances))
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
                    };
                    descriptorCache.update(
                        renderBackend,
                        bindings,
                        [&](vk::DescriptorWriter& writer)
                        {
                            writer.writeBuffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, splatData, 0, VK_WHOLE_SIZE);
                            writer.writeBuffer(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, splatIndices, 0, VK_WHOLE_SIZE);
                            writer.writeBuffer(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, gsplatDrawIndirect, 0, VK_WHOLE_SIZE);
                            writer.writeBuffer(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, splatDistances, 0, VK_WHOLE_SIZE);
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

                if (firstCulling)
                {
                    cmd.fillBuffer(gsplatDrawIndirect, offsetof(VkDrawIndirectCommand, vertexCount), sizeof(uint32_t), 4);
                }
                cmd.fillBuffer(gsplatDrawIndirect, offsetof(VkDrawIndirectCommand, instanceCount), sizeof(uint32_t), 0);
                VkBufferMemoryBarrier2 bufferBarrier = {};
                bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                bufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
                bufferBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                bufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                bufferBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bufferBarrier.buffer = gsplatDrawIndirect;
                bufferBarrier.offset = 0;
                bufferBarrier.size = VK_WHOLE_SIZE;
                VkBufferMemoryBarrier2 barriers[] = { bufferBarrier };
                cmd.pipelineBarrier2(barriers, {});

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
                uint32_t splatCount = data.gsplatScene->getTotalSplatCount();
                cmd.pushConstants(
                    storage.pipeline->getLayout().getHandle(),
                    VK_SHADER_STAGE_ALL,
                    0,
                    sizeof(uint32_t),
                    &splatCount
                );
                cmd.dispatch(uint32_t(std::ceil(splatCount / 256.0f)), 1, 1);
            }
        );
    }

    struct GSplatSortData
    {
        SceneRendererContext* rendererContext;
        uint32_t viewId;
        const GSplatScene* gsplatScene;
        rg::BufferHandle gsplatDrawIndirect;
        rg::BufferHandle splatDistances;
        rg::BufferHandle splatIndices;
        rg::BufferHandle splatSorterStorage;
    };

    class Sorter
    {
    public:
        Sorter(const vk::Device& device) :
            mDevice(device)
        {
            VrdxSorterCreateInfo sorterInfo = {};
            sorterInfo.physicalDevice = device.getPhysicalDevice().getHandle();
            sorterInfo.device = device.getHandle();
            sorterInfo.pipelineCache = VK_NULL_HANDLE;
            vrdxCreateSorter(&sorterInfo, &mSorter);
        }

        ~Sorter()
        {
            vrdxDestroySorter(mSorter);
        }

        VrdxSorter getHandle() const noexcept
        {
            return mSorter;
        }

        VrdxSorterStorageRequirements getSorterKeyValueStorageRequirements(uint32_t numElements)
        {
            VrdxSorterStorageRequirements requirements = {};
            vrdxGetSorterKeyValueStorageRequirements(mSorter, numElements, &requirements);
            return requirements;
        }

    private:
        const vk::Device& mDevice;
        VrdxSorter mSorter = VK_NULL_HANDLE;
    };

    struct GSplatSortStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<vk::Pipeline> pipeline;
        std::shared_ptr<Sorter> sorter;
    };

    void GSplatPass::addGSplatSortPass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneView& view,
        const GSplatScene& gsplatScene
    )
    {
        renderGraph.addComputePass<GSplatSortData>(
            "GSplatSort"_hs,
            [&](rg::RenderGraph::Builder& builder, GSplatSortData& data)
            {
                data.rendererContext = &rendererContext;
                data.viewId = view.getViewId();
                data.gsplatScene = &gsplatScene;

                auto& blackboard = builder.getBlackboard();

                auto gsplatDrawIndirect = blackboard.get<rg::BufferHandle>("GSplatDrawIndirect"_hs(data.viewId));
                data.gsplatDrawIndirect = builder.accessBuffer(gsplatDrawIndirect, rg::BufferUsage::Storage, rg::AccessType::Read_Only);

                auto splatDistances = blackboard.get<rg::BufferHandle>("SplatDistances"_hs(data.viewId));
                data.splatDistances = builder.accessBuffer(splatDistances, rg::BufferUsage::Storage, rg::AccessType::Read_Write);

                auto splatIndices = blackboard.get<rg::BufferHandle>("SplatIndices"_hs(data.viewId));
                data.splatIndices = builder.accessBuffer(splatIndices, rg::BufferUsage::Storage, rg::AccessType::Read_Write);
                blackboard.put("SplatIndices"_hs(data.viewId), data.splatIndices);

                static auto& storage = rendererContext.getPassStorage().getOrCreate<GSplatPass, GSplatSortStorage>();
                if (!storage.sorter)
                {
                    auto& device = rendererContext.getRenderBackend().getDevice();
                    storage.sorter = std::make_shared<Sorter>(device);
                }

                uint32_t splatCount = gsplatScene.getTotalSplatCount();
                auto requirements = storage.sorter->getSorterKeyValueStorageRequirements(splatCount);

                auto splatSorterStorage = builder.createBuffer("SplatSorterStorage"_hs(data.viewId), rg::BufferDescription{ requirements.size, requirements.usage });
                data.splatSorterStorage = builder.accessBuffer(splatSorterStorage, rg::BufferUsage::Storage, rg::AccessType::Write_Only);

            },
            [](rg::ExecuteContext& context, GSplatSortData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<GSplatPass, GSplatSortStorage>();
                auto& cmd = context.getCommandBuffer();
                uint32_t splatCount = data.gsplatScene->getTotalSplatCount();
                vrdxCmdSortKeyValueIndirect(
                    cmd.getHandle(),
                    storage.sorter->getHandle(),
                    splatCount,
                    context.getBuffer(data.gsplatDrawIndirect)->getHandle(),
                    offsetof(VkDrawIndirectCommand, instanceCount),
                    context.getBuffer(data.splatDistances)->getHandle(),
                    0,
                    context.getBuffer(data.splatIndices)->getHandle(),
                    0,
                    context.getBuffer(data.splatSorterStorage)->getHandle(),
                    0,
                    VK_NULL_HANDLE,
                    0
                );
            }
        );
    }

    struct GSplatRenderData
    {
        SceneRendererContext* rendererContext;
        uint32_t viewId;
        float4 viewport;
        const GSplatScene* gsplatScene;
        rg::TextureHandle sceneColor;
        rg::TextureHandle sceneDepth;
        rg::BufferHandle splatIndices;
        rg::BufferHandle gsplatDrawIndirect;
    };

    struct GSplatRenderStorage
    {
        std::shared_ptr<PassShader> shader;
        std::vector<DescriptorCache<2>> perViewCaches;
        TRef<vk::Pipeline> pipeline;
    };

    void GSplatPass::addGSplatRenderPass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneView& view,
        const GSplatScene& gsplatScene
    )
    {
        renderGraph.addGraphicsPass<GSplatRenderData>(
            "GSplatRender"_hs,
            [&](rg::RenderGraph::Builder& builder, GSplatRenderData& data)
            {
                data.rendererContext = &rendererContext;
                data.viewId = view.getViewId();
                data.viewport = view.getViewport();
                data.gsplatScene = &gsplatScene;

                auto& blackboard = builder.getBlackboard();
                auto sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(data.viewId));
                data.sceneColor = builder.addColorAttachment(sceneColor, VK_ATTACHMENT_LOAD_OP_LOAD);
                blackboard.put("SceneColor"_hs(data.viewId), data.sceneColor);

                auto sceneDepth = blackboard.get<rg::TextureHandle>("SceneDepth"_hs(data.viewId));
                data.sceneDepth = builder.addDepthStencilAttachment(
                    sceneDepth,
                    VK_ATTACHMENT_LOAD_OP_LOAD,
                    false,
                    VK_ATTACHMENT_LOAD_OP_LOAD,
                    false
                );
                blackboard.put("SceneDepth"_hs(data.viewId), data.sceneDepth);

                auto splatIndices = blackboard.get<rg::BufferHandle>("SplatIndices"_hs(data.viewId));
                data.splatIndices = builder.accessBuffer(splatIndices, rg::BufferUsage::Storage, rg::AccessType::Read_Only);

                auto gsplatDrawIndirect = blackboard.get<rg::BufferHandle>("GSplatDrawIndirect"_hs(data.viewId));
                data.gsplatDrawIndirect = builder.accessBuffer(gsplatDrawIndirect, rg::BufferUsage::Indirect, rg::AccessType::Read_Only);
            },
            [](rg::ExecuteContext& context, GSplatRenderData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<GSplatPass, GSplatRenderStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "GSplat",
                        SHADER_DIR "/GSplat/GSplat.slang"
                    );
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& descriptorCache = storage.perViewCaches[data.viewId];

                VkBuffer splatData = data.gsplatScene->getSplatDataBuffer().getHandle();
                VkBuffer splatIndices = context.getBuffer(data.splatIndices)->getHandle();
                if (descriptorCache.needUpdate(splatData, splatIndices))
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}
                    };
                    descriptorCache.update(
                        renderBackend,
                        bindings,
                        [&](vk::DescriptorWriter& writer)
                        {
                            writer.writeBuffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, splatData, 0, VK_WHOLE_SIZE);
                            writer.writeBuffer(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, splatIndices, 0, VK_WHOLE_SIZE);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createGSplatRenderPipeline(
                        *data.rendererContext,
                        storage.shader->getOrCreateVariant({}),
                        descriptorCache.getDescriptorSet()->getLayout()
                    );
                }

                auto& cmd = context.getCommandBuffer();

                cmd.bindPipeline(storage.pipeline->getBindPoint(), storage.pipeline->getHandle());

                setViewportAndScissor(cmd, data.viewport);

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

                cmd.drawIndirect(
                    context.getBuffer(data.gsplatDrawIndirect)->getHandle(),
                    0,
                    1,
                    sizeof(VkDrawIndirectCommand)
                );
            }
        );
    }

    struct GSplatDepthData
    {
        SceneRendererContext* rendererContext;
        uint32_t viewId;
        float4 viewport;
        const GSplatScene* gsplatScene;
        rg::TextureHandle gsplatDepth;
        rg::BufferHandle splatIndices;
        rg::BufferHandle gsplatDrawIndirect;
    };

    struct GSplatDepthStorage
    {
        std::shared_ptr<PassShader> shader;
        std::vector<DescriptorCache<3>> perViewCaches;
        TRef<vk::Pipeline> pipeline;
    };

    void GSplatPass::addGSplatDepthPass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneView& view,
        const GSplatScene& gsplatScene
    )
    {
        renderGraph.addGraphicsPass<GSplatDepthData>(
            "GSplatDepth"_hs,
            [&](rg::RenderGraph::Builder& builder, GSplatDepthData& data)
            {
                builder.markSideEffect();
                data.rendererContext = &rendererContext;
                data.viewId = view.getViewId();
                data.viewport = view.getViewport();
                data.gsplatScene = &gsplatScene;

                uint32_t width = uint32_t(data.viewport.z);
                uint32_t height = uint32_t(data.viewport.w);

                auto& blackboard = builder.getBlackboard();

                auto gsplatDepth = builder.createTexture(
                    "GSplatDepth"_hs(data.viewId),
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_R32G32B32A32_SFLOAT,
                        VkExtent3D{ width, height, 1 },
                        1, 1, 1
                    }
                );
                data.gsplatDepth = builder.addColorAttachment(gsplatDepth, VK_ATTACHMENT_LOAD_OP_CLEAR);
                blackboard.put("GSplatDepth"_hs(data.viewId), data.gsplatDepth);

                auto splatIndices = blackboard.get<rg::BufferHandle>("SplatIndices"_hs(data.viewId));
                data.splatIndices = builder.accessBuffer(splatIndices, rg::BufferUsage::Storage, rg::AccessType::Read_Only);

                auto gsplatDrawIndirect = blackboard.get<rg::BufferHandle>("GSplatDrawIndirect"_hs(data.viewId));
                data.gsplatDrawIndirect = builder.accessBuffer(gsplatDrawIndirect, rg::BufferUsage::Indirect | rg::BufferUsage::Storage, rg::AccessType::Read_Only);
            },
            [](rg::ExecuteContext& context, GSplatDepthData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<GSplatPass, GSplatDepthStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "GSplatDepth",
                        SHADER_DIR "/GSplat/GSplatDepth.slang"
                    );
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& descriptorCache = storage.perViewCaches[data.viewId];

                VkBuffer splatData = data.gsplatScene->getSplatDataBuffer().getHandle();
                VkBuffer splatIndices = context.getBuffer(data.splatIndices)->getHandle();
                VkBuffer gsplatDrawIndirect = context.getBuffer(data.gsplatDrawIndirect)->getHandle();
                if (descriptorCache.needUpdate(splatData, splatIndices, gsplatDrawIndirect))
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
                        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}
                    };
                    descriptorCache.update(
                        renderBackend,
                        bindings,
                        [&](vk::DescriptorWriter& writer)
                        {
                            writer.writeBuffer(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, splatData, 0, VK_WHOLE_SIZE);
                            writer.writeBuffer(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, splatIndices, 0, VK_WHOLE_SIZE);
                            writer.writeBuffer(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, gsplatDrawIndirect, 0, VK_WHOLE_SIZE);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createGSplatDepthPipeline(
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

                cmd.drawIndirect(
                    gsplatDrawIndirect,
                    0,
                    1,
                    sizeof(VkDrawIndirectCommand)
                );
            }
        );
    }

    struct GSplatRelightingData
    {
        SceneRendererContext* rendererContext;
        uint32_t viewId;
        float4 viewport;
        rg::TextureHandle sceneColor;
        rg::TextureHandle gsplatDepth;
    };

    struct GSplatRelightingStorage
    {
        std::shared_ptr<PassShader> shader;
        std::vector<DescriptorCache<1>> perViewCaches;
        TRef<vk::Pipeline> pipeline;
    };

    void GSplatPass::addGSplatRelightingPass(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneView& view,
        const GSplatScene& gsplatScene
    )
    {
        renderGraph.addGraphicsPass<GSplatRelightingData>(
            "Relighting"_hs,
            [&](rg::RenderGraph::Builder& builder, GSplatRelightingData& data)
            {
                builder.markSideEffect();
                data.rendererContext = &rendererContext;
                data.viewId = view.getViewId();
                data.viewport = view.getViewport();

                auto& blackboard = builder.getBlackboard();
                auto sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(data.viewId));
                data.sceneColor = builder.addColorAttachment(sceneColor, VK_ATTACHMENT_LOAD_OP_LOAD);
                blackboard.put("SceneColor"_hs(data.viewId), data.sceneColor);

                auto gsplatDepth = blackboard.get<rg::TextureHandle>("GSplatDepth"_hs(data.viewId));
                data.gsplatDepth = builder.sample(gsplatDepth);
            },
            [](rg::ExecuteContext& context, GSplatRelightingData& data)
            {
                static auto& storage = data.rendererContext->getPassStorage().getOrCreate<GSplatPass, GSplatRelightingStorage>();

                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "GSplatRelighting",
                        SHADER_DIR "/GSplat/Relighting.slang"
                    );
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& descriptorCache = storage.perViewCaches[data.viewId];

                VkImageView gsplatDepth = context.getTexture(data.gsplatDepth)->getHandle();
                if (descriptorCache.needUpdate(gsplatDepth))
                {
                    VkDescriptorSetLayoutBinding bindings[] = {
                        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
                    };
                    descriptorCache.update(
                        renderBackend,
                        bindings,
                        [&](vk::DescriptorWriter& writer)
                        {
                            writer.writeImage(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, gsplatDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createGSplatRelightingPipeline(
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

    vk::Pipeline* GSplatPass::createGSplatRenderPipeline(
        SceneRendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const vk::DescriptorSetLayout& descriptorSetLayout
    )
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, descriptorSetLayout.getHandle());
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.topology = static_cast<uint32_t>(Topology::Triangle_Fan);
        pipelineKey.pipelineState.samples = static_cast<uint32_t>(SampleCount::Count_1);
        pipelineKey.pipelineState.polygonMode = static_cast<uint32_t>(PolygonMode::Fill);
        pipelineKey.pipelineState.cullMode = static_cast<uint32_t>(CullMode::Back);
        pipelineKey.pipelineState.frontFace = static_cast<uint32_t>(FrontFace::Counter_Clockwise);
        pipelineKey.pipelineState.depthTestEnable = 1;
        pipelineKey.pipelineState.depthWriteEnable = 0;
        pipelineKey.pipelineState.depthCompareOp = static_cast<uint32_t>(CompareOp::Greater_Or_Equal);
        pipelineKey.pipelineState.stencilTestEnable = 0;
        pipelineKey.pipelineState.stencilState = StencilState::get();
        pipelineKey.pipelineState.colorBlendState = ColorBlendState::get <
            AttachmentBlendState{ VK_TRUE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, vk::kColorComponentAll }
        > ();
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_R16G16B16A16_SFLOAT
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

    vk::Pipeline* GSplatPass::createGSplatDepthPipeline(
        SceneRendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const vk::DescriptorSetLayout& descriptorSetLayout
    )
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, descriptorSetLayout.getHandle());
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.topology = static_cast<uint32_t>(Topology::Triangle_Fan);
        pipelineKey.pipelineState.samples = static_cast<uint32_t>(SampleCount::Count_1);
        pipelineKey.pipelineState.polygonMode = static_cast<uint32_t>(PolygonMode::Fill);
        pipelineKey.pipelineState.cullMode = static_cast<uint32_t>(CullMode::Back);
        pipelineKey.pipelineState.frontFace = static_cast<uint32_t>(FrontFace::Counter_Clockwise);
        pipelineKey.pipelineState.depthTestEnable = 1;
        pipelineKey.pipelineState.depthWriteEnable = 0;
        pipelineKey.pipelineState.depthCompareOp = static_cast<uint32_t>(CompareOp::Greater_Or_Equal);
        pipelineKey.pipelineState.stencilTestEnable = 0;
        pipelineKey.pipelineState.stencilState = StencilState::get();
        pipelineKey.pipelineState.colorBlendState = ColorBlendState::get <
            AttachmentBlendState{ VK_TRUE, VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_OP_ADD, vk::kColorComponentAll }
        > ();
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_R32G32B32A32_SFLOAT
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

    vk::Pipeline* GSplatPass::createGSplatRelightingPipeline(
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
            AttachmentBlendState{ VK_TRUE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, vk::kColorComponentAll }
        > ();
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            VK_FORMAT_UNDEFINED,
            VK_FORMAT_R16G16B16A16_SFLOAT
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}

