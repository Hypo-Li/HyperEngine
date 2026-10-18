#include "GSplatPass.h"
#include "RenderPassUtils.h"

#include <algorithm>

#include <Culling.generated.h>
#include <GSplat.generated.h>
#include <GSplatDepth.generated.h>
#include <RadixSortUpsweep.generated.h>
#include <RadixSortSpine.generated.h>
#include <RadixSortDownsweep.generated.h>

namespace hyp
{
    namespace
    {
        // 256 基数(8 bit/pass),32 位 key 需要 4 个 pass,和 Shader/Common/RadixSort/Constants.slang 保持一致
        constexpr uint32_t kRadixSortRadix = 256;
        constexpr uint32_t kRadixSortWorkgroupSize = 256;
        constexpr uint32_t kRadixSortPartitionDivision = 8;
        constexpr uint32_t kRadixSortPartitionSize = kRadixSortPartitionDivision * kRadixSortWorkgroupSize;
        constexpr uint32_t kRadixSortPassCount = 4;

        void setRadixSortPass(rhi::CommandList& cmdList, const rhi::PipelineLayout& layout, uint32_t pass)
        {
            cmdList.setImmediateData(&layout, 0, std::span(reinterpret_cast<const uint8_t*>(&pass), sizeof(pass)));
        }

        // 和 createComputePipeline(RenderPassUtils.h)不同,这里不套全局的 group0(GlobalData)/
        // group1(View)——Downsweep 自己就要 7 个 storage buffer,再加上全局那两组会超过
        // WebGPU 单 stage 8 个 storage buffer 的限制,而 Downsweep 其实也用不到全局那两组,所以只
        // 给它自己的 BindGroupLayout 建一个精简的 PipelineLayout,放在 group 0
        rhi::Pipeline* createStandaloneComputePipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& passLayout)
        {
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

            PipelineCache::PipelineLayoutKey layoutKey{};
            layoutKey.bindGroupLayouts[0] = &passLayout;
            layoutKey.bindGroupLayouts[1] = nullptr;
            layoutKey.bindGroupLayouts[2] = nullptr;
            layoutKey.bindGroupLayouts[3] = nullptr;
            rhi::PipelineLayout* pipelineLayout = pipelineCache.getOrCreateLayout(layoutKey);

            PipelineCache::ComputeKey key;
            key.shader = shaderVariant;
            key.pipelineLayout = pipelineLayout;
            return pipelineCache.getOrCreateComputePipeline(key);
        }

        // Downsweep 用的精简 PipelineLayout 只有一个 BindGroupLayout(放在 group 0),和其它 pass
        // 用的"group0=全局数据、group1=场景视图、group2=pass 自己"这套不兼容。绑定 Downsweep 的
        // pipeline 会让驱动认为 group0/1 上原来绑的内容不再兼容,必须在切回其它 pipeline(下一轮
        // Upsweep,或者 GSplatSort 结束后的 GSplatRender)之前重新绑定,否则后面的 pass 会因为
        // group0/1 没有有效绑定而出错
        void rebindGlobalBindGroups(
            rg::ExecuteContext& context,
            RendererContext& rendererContext,
            uint32_t viewId)
        {
            auto& renderBackend = context.getRenderBackend();
            auto& cmdList = context.getCommandList();
            auto& globalBindings = renderBackend.getGlobalBindings();

            cmdList.setBindGroup(
                0,
                rhi::PipelineBindPoint::Compute,
                &globalBindings.getPipelineLayout(),
                &globalBindings.getGlobalDataBindGroup(),
                {}
            );

            const rhi::BindGroup& viewBindGroup = rendererContext.getViewDataManager().getBindGroup();
            uint32_t dynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(viewId);
            cmdList.setBindGroup(
                1,
                rhi::PipelineBindPoint::Compute,
                &rendererContext.getGlobalPipelineLayout(),
                &viewBindGroup,
                { &dynamicOffset, 1 }
            );
        }
    }
    struct DrawIndirectCommand
    {
        uint32_t vertexCount;
        uint32_t instanceCount;
        uint32_t firstVertex;
        uint32_t firstInstance;
    };

    void GSplatPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const GSplatScene& gsplatScene
    )
    {
        addGSplatCullingPass(renderGraph, rendererContext, scene, view, gsplatScene);
        addGSplatSortPass(renderGraph, rendererContext, view, gsplatScene);
        addGSplatRenderPass(renderGraph, rendererContext, scene, view, gsplatScene);
        //addGSplatDepthPass(renderGraph, rendererContext, scene, view, gsplatScene);
    }

    struct GSplatCullingData
    {
        uint32_t viewId;
        const rhi::BindGroup* instanceDataBindGroup;
        const GSplatScene* gsplatScene;
        rg::BufferHandle gsplatDrawIndirect;
        rg::BufferHandle splatIndices;
        rg::BufferHandle splatDistances;
    };

    struct GSplatCullingStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<4>> descriptorCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void GSplatPass::addGSplatCullingPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const GSplatScene& gsplatScene
    )
    {
        renderGraph.addComputePass<GSplatCullingData>(
            "GSplatCulling"_hs,
            [&](rg::RenderGraph::Builder& builder, GSplatCullingData& data)
            {
                data.viewId = view.getViewId();
                data.instanceDataBindGroup = &scene.getInstanceDataBindGroup();
                data.gsplatScene = &gsplatScene;
                auto& blackboard = builder.getBlackboard();

                auto splatIndices = builder.createBuffer(
                    "SplatIndices"_hs(data.viewId),
                    rhi::BufferDesc{ sizeof(uint32_t) * gsplatScene.getTotalSplatCount(), rhi::BufferUsage::Storage }
                );
                data.splatIndices = builder.accessBuffer(
                    splatIndices,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Write_Only
                );
                blackboard.put("SplatIndices"_hs(data.viewId), data.splatIndices);

                auto gsplatDrawIndirect = builder.createBuffer(
                    "GSplatDrawIndirect"_hs(data.viewId),
                    rhi::BufferDesc{ sizeof(DrawIndirectCommand), rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect | rhi::BufferUsage::Transfer_Dst }
                );
                data.gsplatDrawIndirect = builder.accessBuffer(
                    gsplatDrawIndirect,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Write_Only
                );
                blackboard.put("GSplatDrawIndirect"_hs(data.viewId), data.gsplatDrawIndirect);

                auto splatDistances = builder.createBuffer(
                    "SplatDistances"_hs(data.viewId),
                    rhi::BufferDesc{ sizeof(float) * gsplatScene.getTotalSplatCount(), rhi::BufferUsage::Storage }
                );
                data.splatDistances = builder.accessBuffer(
                    splatDistances,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Write_Only
                );
                blackboard.put("SplatDistances"_hs(data.viewId), data.splatDistances);
            },
            [&rendererContext](rg::ExecuteContext& context, GSplatCullingData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<GSplatPass, GSplatCullingStorage>();

                auto& renderBackend = context.getRenderBackend();

                bool firstCulling = storage.shader == nullptr;
                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "GSplatCulling",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(Culling, Compute))
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kCulling_Bindings);
                }

                if (storage.descriptorCaches.size() <= data.viewId)
                {
                    storage.descriptorCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.descriptorCaches[data.viewId];

                const rhi::Buffer* splatData = &data.gsplatScene->getSplatDataBuffer();
                const rhi::Buffer* splatIndices = context.getBuffer(data.splatIndices);
                const rhi::Buffer* gsplatDrawIndirect = context.getBuffer(data.gsplatDrawIndirect);
                const rhi::Buffer* splatDistances = context.getBuffer(data.splatDistances);
                if (bindGroupCache.needUpdate(splatData, splatIndices, gsplatDrawIndirect, splatDistances))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kCulling_Bindings);
                            writer.setBuffer("GPUSplatDataBuffer", splatData);
                            writer.setBuffer("VisibleSplatIndexBuffer", splatIndices);
                            writer.setBuffer("DrawIndirectCommandBuffer", gsplatDrawIndirect);
                            writer.setBuffer("VisibleSplatDistanceBuffer", splatDistances);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createComputePipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        bindGroupCache.getBindGroup()->getLayout(),
                        &rendererContext.getInstanceDataBindGroupLayout()
                    );
                }

                auto& cmdList = context.getCommandList();

                if (firstCulling)
                {
                    auto allocation = renderBackend.getStagingAllocator().forceAllocate(sizeof(uint32_t));
                    *static_cast<uint32_t*>(allocation.mappedData) = 4;
                    rhi::BufferCopyRegion region;
                    region.srcOffset = allocation.offset;
                    region.dstOffset = offsetof(DrawIndirectCommand, vertexCount);
                    region.size = sizeof(uint32_t);
                    cmdList.copyBuffer(allocation.buffer, gsplatDrawIndirect, { &region, 1 });
                }
                cmdList.clearBuffer(gsplatDrawIndirect, offsetof(DrawIndirectCommand, instanceCount), sizeof(uint32_t));

                rhi::BufferBarrier barrier;
                barrier.buffer = gsplatDrawIndirect;
                barrier.offset = 0;
                barrier.size = rhi::kWholeBuffer;
                barrier.oldState = rhi::ResourceState::Transfer_Dst;
                barrier.newState = rhi::ResourceState::Compute_Shader_Storage;
                cmdList.insertBarriers({ &barrier, 1 });

                context.beginPass();

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Compute,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.setBindGroup(3, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(), data.instanceDataBindGroup, {});
                uint32_t splatCount = data.gsplatScene->getTotalSplatCount();
                cmdList.setImmediateData(&storage.pipeline->getLayout(), 0, std::span(reinterpret_cast<const uint8_t*>(&splatCount), sizeof(splatCount)));
                cmdList.dispatch(uint32_t(std::ceil(splatCount / 256.0f)), 1, 1);
            },
            rg::PassFlags::Skip_Begin_Pass
        );
    }

    struct GSplatSortStorage
    {
        std::shared_ptr<PassShader> upsweepShader;
        std::shared_ptr<PassShader> spineShader;
        std::shared_ptr<PassShader> downsweepShader;

        TRef<rhi::BindGroupLayout> upsweepLayout;
        TRef<rhi::BindGroupLayout> spineLayout;
        TRef<rhi::BindGroupLayout> downsweepLayout;

        TRef<rhi::Pipeline> upsweepPipeline;
        TRef<rhi::Pipeline> spinePipeline;
        TRef<rhi::Pipeline> downsweepPipeline;

        struct PerView
        {
            // keysIn 只有两种可能(splatDistances 或 keysTemp),按 pass 号奇偶复用
            std::array<BindGroupCache<4>, 2> upsweepCaches;
            // Spine 不涉及 key/value ping-pong,4 个 pass 共用一份
            BindGroupCache<3> spineCache;
            // Downsweep 的 in/out 每个 pass 都不一样,4 个 pass 各留一份
            std::array<BindGroupCache<7>, kRadixSortPassCount> downsweepCaches;
        };
        std::vector<PerView> perViewCaches;
    };

    struct GSplatRadixSortUpsweepData
    {
        uint32_t viewId;
        uint32_t partitionCount;
        uint32_t pass;
        rg::BufferHandle gsplatDrawIndirect;
        rg::BufferHandle keysIn;
        rg::BufferHandle globalHistogram;
        rg::BufferHandle partitionHistogram;
    };

    // GSplatSort 拆成的 12 个独立 RenderGraph pass 之一(4 个 radix pass * Upsweep/Spine/Downsweep 三步)。
    // globalHistogram/partitionHistogram 用引用传入/传出:pass==0 时这两个 buffer 在这里第一次被创建
    // (只能在某个 pass 的 builder 里 createBuffer,选第一个摸到它们的 pass);pass>0 时用调用方传入的
    // 上一轮 Downsweep 更新过的 handle 继续声明访问,写完之后把新 handle 传回去给下一步(Spine)用
    void GSplatPass::addRadixSortUpsweepPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        uint32_t viewId,
        uint32_t partitionCount,
        uint32_t pass,
        rg::BufferHandle gsplatDrawIndirect,
        rg::BufferHandle keysIn,
        rg::BufferHandle& globalHistogram,
        rg::BufferHandle& partitionHistogram
    )
    {
        bool firstPass = (pass == 0);
        static const HString passNames[kRadixSortPassCount] = {
            "GSplatSortUpsweep0",
            "GSplatSortUpsweep1",
            "GSplatSortUpsweep2",
            "GSplatSortUpsweep3",
        };
        auto& computePass = renderGraph.addComputePass<GSplatRadixSortUpsweepData>(
            passNames[pass](viewId),
            [&](rg::RenderGraph::Builder& builder, GSplatRadixSortUpsweepData& data)
            {
                data.viewId = viewId;
                data.partitionCount = partitionCount;
                data.pass = pass;

                data.gsplatDrawIndirect = builder.accessBuffer(gsplatDrawIndirect, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.keysIn = builder.accessBuffer(keysIn, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

                if (firstPass)
                {
                    auto globalHistogramHandle = builder.createBuffer(
                        "GSplatSortGlobalHistogram"_hs(viewId),
                        rhi::BufferDesc{ sizeof(uint32_t) * kRadixSortRadix * kRadixSortPassCount, rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst }
                    );
                    data.globalHistogram = builder.accessBuffer(globalHistogramHandle, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);

                    auto partitionHistogramHandle = builder.createBuffer(
                        "GSplatSortPartitionHistogram"_hs(viewId),
                        rhi::BufferDesc{ sizeof(uint32_t) * kRadixSortRadix * partitionCount, rhi::BufferUsage::Storage }
                    );
                    data.partitionHistogram = builder.accessBuffer(partitionHistogramHandle, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);
                }
                else
                {
                    // Upsweep 是原子累加,GlobalHistogram 读写都有;PartitionHistogram 每趟都是整份覆盖写
                    data.globalHistogram = builder.accessBuffer(globalHistogram, rhi::BufferUsage::Storage, rg::AccessType::Read_Write);
                    data.partitionHistogram = builder.accessBuffer(partitionHistogram, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);
                }
            },
            [&rendererContext](rg::ExecuteContext& context, GSplatRadixSortUpsweepData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<GSplatPass, GSplatSortStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.upsweepShader)
                {
                    storage.upsweepShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "RadixSortUpsweep",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(RadixSortUpsweep, Compute))
                    );
                }
                if (!storage.upsweepLayout)
                {
                    storage.upsweepLayout = createPassBindGroupLayout(renderBackend, generated::kRadixSortUpsweep_Bindings);
                }
                if (!storage.upsweepPipeline)
                {
                    // 完全用不到全局的 group0(GlobalData)/group1(View),用不含这两组的精简
                    // PipelineLayout(见 createStandaloneComputePipeline 的注释),自己的 BindGroupLayout 放在 group 0
                    storage.upsweepPipeline = createStandaloneComputePipeline(rendererContext, storage.upsweepShader->getVariant(), *storage.upsweepLayout);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& perView = storage.perViewCaches[data.viewId];

                const rhi::Buffer* gsplatDrawIndirect = context.getBuffer(data.gsplatDrawIndirect);
                const rhi::Buffer* keysIn = context.getBuffer(data.keysIn);
                const rhi::Buffer* globalHistogram = context.getBuffer(data.globalHistogram);
                const rhi::Buffer* partitionHistogram = context.getBuffer(data.partitionHistogram);

                uint32_t side = data.pass % 2;
                auto& cache = perView.upsweepCaches[side];
                if (cache.needUpdate(gsplatDrawIndirect, globalHistogram, partitionHistogram, keysIn))
                {
                    cache.update(renderBackend, *storage.upsweepLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        // ElementCounts 直接借用 gsplatDrawIndirect 里 Culling 已经算好的可见 splat 数(instanceCount
                        // 字段),不需要单独维护一份 element count buffer,shader 侧读 ElementCounts[1]
                        PassBindGroupWriter writer(generated::kRadixSortUpsweep_Bindings);
                        writer.setBuffer("ElementCounts", gsplatDrawIndirect);
                        writer.setBuffer("GlobalHistogram", globalHistogram);
                        writer.setBuffer("PartitionHistogram", partitionHistogram);
                        writer.setBuffer("Keys", keysIn);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();

                if (data.pass == 0)
                {
                    // 整趟排序开始前清零 GlobalHistogram 一次(Upsweep 是原子累加,不是覆盖写)。
                    // clearBuffer 底层是 vkCmdFillBuffer,属于 transfer 写,RenderGraph 声明的 access 是
                    // Write_Only(compute),覆盖不到这次 transfer 写,所以这个屏障必须手动插,不能指望自动推导;
                    // 不清零的话 GlobalHistogram 里会残留上一帧(瞬态 buffer 池复用)的前缀和值,导致每个 radix
                    // 的基准偏移全错、scatter 到错误的高位偏移
                    rhi::BufferBarrier clearBarrier;
                    clearBarrier.buffer = globalHistogram;
                    clearBarrier.offset = 0;
                    clearBarrier.size = rhi::kWholeBuffer;
                    clearBarrier.oldState = rhi::ResourceState::Transfer_Dst;
                    clearBarrier.newState = rhi::ResourceState::Compute_Shader_Storage;
                    cmdList.clearBuffer(globalHistogram, 0, rhi::kWholeBuffer);
                    cmdList.insertBarriers({ &clearBarrier, 1 });
                    context.beginPass();
                }

                cmdList.bindPipeline(storage.upsweepPipeline.get());
                cmdList.setBindGroup(0, rhi::PipelineBindPoint::Compute, &storage.upsweepPipeline->getLayout(), cache.getBindGroup(), {});
                setRadixSortPass(cmdList, storage.upsweepPipeline->getLayout(), data.pass);
                cmdList.dispatch(data.partitionCount, 1, 1);
            },
            firstPass ? rg::PassFlags::Skip_Begin_Pass : rg::PassFlags::None
        );
        globalHistogram = computePass.getData().globalHistogram;
        partitionHistogram = computePass.getData().partitionHistogram;
    }

    struct GSplatRadixSortSpineData
    {
        uint32_t viewId;
        uint32_t pass;
        rg::BufferHandle gsplatDrawIndirect;
        rg::BufferHandle globalHistogram;
        rg::BufferHandle partitionHistogram;
    };

    void GSplatPass::addRadixSortSpinePass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        uint32_t viewId,
        uint32_t pass,
        rg::BufferHandle gsplatDrawIndirect,
        rg::BufferHandle& globalHistogram,
        rg::BufferHandle& partitionHistogram
    )
    {
        static const HString passNames[kRadixSortPassCount] = {
            "GSplatSortSpine0",
            "GSplatSortSpine1",
            "GSplatSortSpine2",
            "GSplatSortSpine3",
        };
        auto& computePass = renderGraph.addComputePass<GSplatRadixSortSpineData>(
            passNames[pass](viewId),
            [&](rg::RenderGraph::Builder& builder, GSplatRadixSortSpineData& data)
            {
                data.viewId = viewId;
                data.pass = pass;

                data.gsplatDrawIndirect = builder.accessBuffer(gsplatDrawIndirect, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.globalHistogram = builder.accessBuffer(globalHistogram, rhi::BufferUsage::Storage, rg::AccessType::Read_Write);
                data.partitionHistogram = builder.accessBuffer(partitionHistogram, rhi::BufferUsage::Storage, rg::AccessType::Read_Write);
            },
            [&rendererContext](rg::ExecuteContext& context, GSplatRadixSortSpineData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<GSplatPass, GSplatSortStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.spineShader)
                {
                    storage.spineShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "RadixSortSpine",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(RadixSortSpine, Compute))
                    );
                }
                if (!storage.spineLayout)
                {
                    storage.spineLayout = createPassBindGroupLayout(renderBackend, generated::kRadixSortSpine_Bindings);
                }
                if (!storage.spinePipeline)
                {
                    storage.spinePipeline = createStandaloneComputePipeline(rendererContext, storage.spineShader->getVariant(), *storage.spineLayout);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& perView = storage.perViewCaches[data.viewId];

                const rhi::Buffer* gsplatDrawIndirect = context.getBuffer(data.gsplatDrawIndirect);
                const rhi::Buffer* globalHistogram = context.getBuffer(data.globalHistogram);
                const rhi::Buffer* partitionHistogram = context.getBuffer(data.partitionHistogram);

                if (perView.spineCache.needUpdate(gsplatDrawIndirect, globalHistogram, partitionHistogram))
                {
                    perView.spineCache.update(renderBackend, *storage.spineLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kRadixSortSpine_Bindings);
                        writer.setBuffer("ElementCounts", gsplatDrawIndirect);
                        writer.setBuffer("GlobalHistogram", globalHistogram);
                        writer.setBuffer("PartitionHistogram", partitionHistogram);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.spinePipeline.get());
                cmdList.setBindGroup(0, rhi::PipelineBindPoint::Compute, &storage.spinePipeline->getLayout(), perView.spineCache.getBindGroup(), {});
                setRadixSortPass(cmdList, storage.spinePipeline->getLayout(), data.pass);
                cmdList.dispatch(kRadixSortRadix, 1, 1);
            }
        );
        globalHistogram = computePass.getData().globalHistogram;
        partitionHistogram = computePass.getData().partitionHistogram;
    }

    struct GSplatRadixSortDownsweepData
    {
        uint32_t viewId;
        uint32_t partitionCount;
        uint32_t pass;
        rg::BufferHandle gsplatDrawIndirect;
        rg::BufferHandle globalHistogram;
        rg::BufferHandle partitionHistogram;
        rg::BufferHandle keysIn;
        rg::BufferHandle keysOut;
        rg::BufferHandle valuesIn;
        rg::BufferHandle valuesOut;
    };

    void GSplatPass::addRadixSortDownsweepPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        uint32_t viewId,
        uint32_t splatCount,
        uint32_t partitionCount,
        uint32_t pass,
        rg::BufferHandle gsplatDrawIndirect,
        rg::BufferHandle globalHistogram,
        rg::BufferHandle partitionHistogram,
        rg::BufferHandle keysIn,
        rg::BufferHandle& keysOut,
        rg::BufferHandle valuesIn,
        rg::BufferHandle& valuesOut
    )
    {
        // keysOut/valuesOut(keyBuffers[1]/valueBuffers[1],即 GSplatSortKeysTemp/ValuesTemp)在整趟排序里
        // 第一次被写,是 pass==0 这一趟(outSide==1),要在这里 createBuffer;pass==2 outSide 同样是 1,
        // 但那时已经不是"第一次"了,直接对已有 handle 声明 Write_Only 即可(不需要再 create)
        bool createOutBuffers = (pass == 0);
        static const HString passNames[kRadixSortPassCount] = {
            "GSplatSortDownsweep0",
            "GSplatSortDownsweep1",
            "GSplatSortDownsweep2",
            "GSplatSortDownsweep3",
        };
        auto& computePass = renderGraph.addComputePass<GSplatRadixSortDownsweepData>(
            passNames[pass](viewId),
            [&](rg::RenderGraph::Builder& builder, GSplatRadixSortDownsweepData& data)
            {
                data.viewId = viewId;
                data.partitionCount = partitionCount;
                data.pass = pass;

                data.gsplatDrawIndirect = builder.accessBuffer(gsplatDrawIndirect, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                // Spine 已经把 GlobalHistogram/PartitionHistogram 都变成最终的前缀和了,Downsweep 只读
                data.globalHistogram = builder.accessBuffer(globalHistogram, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.partitionHistogram = builder.accessBuffer(partitionHistogram, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.keysIn = builder.accessBuffer(keysIn, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.valuesIn = builder.accessBuffer(valuesIn, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

                if (createOutBuffers)
                {
                    auto keysOutHandle = builder.createBuffer(
                        "GSplatSortKeysTemp"_hs(viewId),
                        rhi::BufferDesc{ sizeof(uint32_t) * std::max(1u, splatCount), rhi::BufferUsage::Storage }
                    );
                    data.keysOut = builder.accessBuffer(keysOutHandle, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);

                    auto valuesOutHandle = builder.createBuffer(
                        "GSplatSortValuesTemp"_hs(viewId),
                        rhi::BufferDesc{ sizeof(uint32_t) * std::max(1u, splatCount), rhi::BufferUsage::Storage }
                    );
                    data.valuesOut = builder.accessBuffer(valuesOutHandle, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);
                }
                else
                {
                    data.keysOut = builder.accessBuffer(keysOut, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);
                    data.valuesOut = builder.accessBuffer(valuesOut, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);
                }
            },
            [&rendererContext](rg::ExecuteContext& context, GSplatRadixSortDownsweepData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<GSplatPass, GSplatSortStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.downsweepShader)
                {
                    storage.downsweepShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "RadixSortDownsweep",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(RadixSortDownsweep, Compute))
                    );
                }
                if (!storage.downsweepLayout)
                {
                    storage.downsweepLayout = createPassBindGroupLayout(renderBackend, generated::kRadixSortDownsweep_Bindings);
                }
                if (!storage.downsweepPipeline)
                {
                    storage.downsweepPipeline = createStandaloneComputePipeline(rendererContext, storage.downsweepShader->getVariant(), *storage.downsweepLayout);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& perView = storage.perViewCaches[data.viewId];

                const rhi::Buffer* gsplatDrawIndirect = context.getBuffer(data.gsplatDrawIndirect);
                const rhi::Buffer* globalHistogram = context.getBuffer(data.globalHistogram);
                const rhi::Buffer* partitionHistogram = context.getBuffer(data.partitionHistogram);
                const rhi::Buffer* keysIn = context.getBuffer(data.keysIn);
                const rhi::Buffer* keysOut = context.getBuffer(data.keysOut);
                const rhi::Buffer* valuesIn = context.getBuffer(data.valuesIn);
                const rhi::Buffer* valuesOut = context.getBuffer(data.valuesOut);

                auto& cache = perView.downsweepCaches[data.pass];
                if (cache.needUpdate(gsplatDrawIndirect, globalHistogram, partitionHistogram, keysIn, keysOut, valuesIn, valuesOut))
                {
                    cache.update(renderBackend, *storage.downsweepLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kRadixSortDownsweep_Bindings);
                        writer.setBuffer("ElementCounts", gsplatDrawIndirect);
                        writer.setBuffer("GlobalHistogram", globalHistogram);
                        writer.setBuffer("PartitionHistogram", partitionHistogram);
                        writer.setBuffer("KeysIn", keysIn);
                        writer.setBuffer("KeysOut", keysOut);
                        writer.setBuffer("ValuesIn", valuesIn);
                        writer.setBuffer("ValuesOut", valuesOut);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.downsweepPipeline.get());
                cmdList.setBindGroup(0, rhi::PipelineBindPoint::Compute, &storage.downsweepPipeline->getLayout(), cache.getBindGroup(), {});
                setRadixSortPass(cmdList, storage.downsweepPipeline->getLayout(), data.pass);
                cmdList.dispatch(data.partitionCount, 1, 1);

                if (data.pass == kRadixSortPassCount - 1)
                {
                    // 整趟排序期间 Upsweep/Spine/Downsweep 用的都是不含全局 group0(GlobalData)/group1
                    // (View)的精简 PipelineLayout,紧接着的 GSplatRender pass(以及后面帧里其它
                    // 任何 pass)还是用正常的 group0/1/2 布局、并且不会自己重新绑定 group0/1(它们假设这
                    // 两组从帧开始就一直有效绑定),所以最后一趟 Downsweep 结束前必须重新绑一次,不能遗漏
                    rebindGlobalBindGroups(context, rendererContext, data.viewId);
                }
            }
        );
        keysOut = computePass.getData().keysOut;
        valuesOut = computePass.getData().valuesOut;
    }

    void GSplatPass::addGSplatSortPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view,
        const GSplatScene& gsplatScene
    )
    {
        uint32_t viewId = view.getViewId();
        uint32_t splatCount = gsplatScene.getTotalSplatCount();
        uint32_t partitionCount = std::max(1u, (splatCount + kRadixSortPartitionSize - 1) / kRadixSortPartitionSize);

        auto& blackboard = renderGraph.getBlackboard();

        // keyBuffers[0]/valueBuffers[0] = splatDistances/splatIndices(Culling 阶段产出的那一对);
        // keyBuffers[1]/valueBuffers[1] = keysTemp/valuesTemp(排序过程里用来 ping-pong 的临时 buffer,
        // 直到 Downsweep pass==0 才第一次创建)。pass 是偶数时 keysIn 是 0 号那一对,奇数时是 1 号那一对;
        // 4 个 pass(偶数个)排完之后,最终结果正好落回 0 号,也就是下游 GSplatRenderPass 已经在读的那两个 buffer
        rg::BufferHandle keyBuffers[2];
        keyBuffers[0] = blackboard.get<rg::BufferHandle>("SplatDistances"_hs(viewId));
        rg::BufferHandle valueBuffers[2];
        valueBuffers[0] = blackboard.get<rg::BufferHandle>("SplatIndices"_hs(viewId));
        rg::BufferHandle gsplatDrawIndirect = blackboard.get<rg::BufferHandle>("GSplatDrawIndirect"_hs(viewId));

        rg::BufferHandle globalHistogram;
        rg::BufferHandle partitionHistogram;

        for (uint32_t pass = 0; pass < kRadixSortPassCount; ++pass)
        {
            uint32_t inSide = pass % 2;
            uint32_t outSide = 1 - inSide;

            addRadixSortUpsweepPass(renderGraph, rendererContext, viewId, partitionCount, pass, gsplatDrawIndirect, keyBuffers[inSide], globalHistogram, partitionHistogram);
            addRadixSortSpinePass(renderGraph, rendererContext, viewId, pass, gsplatDrawIndirect, globalHistogram, partitionHistogram);
            addRadixSortDownsweepPass(renderGraph, rendererContext, viewId, splatCount, partitionCount, pass, gsplatDrawIndirect, globalHistogram, partitionHistogram, keyBuffers[inSide], keyBuffers[outSide], valueBuffers[inSide], valueBuffers[outSide]);
        }

        blackboard.put("SplatDistances"_hs(viewId), keyBuffers[0]);
        blackboard.put("SplatIndices"_hs(viewId), valueBuffers[0]);
        blackboard.put("GSplatDrawIndirect"_hs(viewId), gsplatDrawIndirect);
    }

    struct GSplatRenderData
    {
        uint32_t viewId;
        const rhi::BindGroup* instanceDataBindGroup;
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
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<2>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void GSplatPass::addGSplatRenderPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const GSplatScene& gsplatScene
    )
    {
        renderGraph.addGraphicsPass<GSplatRenderData>(
            "GSplatRender"_hs,
            [&](rg::RenderGraph::Builder& builder, GSplatRenderData& data)
            {
                data.viewId = view.getViewId();
                data.instanceDataBindGroup = &scene.getInstanceDataBindGroup();
                data.viewport = view.getViewport();
                data.gsplatScene = &gsplatScene;

                auto& blackboard = builder.getBlackboard();
                auto sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(data.viewId));
                data.sceneColor = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Load);
                blackboard.put("SceneColor"_hs(data.viewId), data.sceneColor);

                auto sceneDepth = blackboard.get<rg::TextureHandle>("SceneDepth"_hs(data.viewId));
                data.sceneDepth = builder.addDepthStencilAttachment(
                    sceneDepth,
                    rhi::AttachmentLoadOp::Load,
                    false,
                    rhi::AttachmentLoadOp::Load,
                    false
                );
                blackboard.put("SceneDepth"_hs(data.viewId), data.sceneDepth);

                auto splatIndices = blackboard.get<rg::BufferHandle>("SplatIndices"_hs(data.viewId));
                data.splatIndices = builder.accessBuffer(splatIndices, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

                auto gsplatDrawIndirect = blackboard.get<rg::BufferHandle>("GSplatDrawIndirect"_hs(data.viewId));
                data.gsplatDrawIndirect = builder.accessBuffer(gsplatDrawIndirect, rhi::BufferUsage::Indirect, rg::AccessType::Read_Only);
            },
            [&rendererContext](rg::ExecuteContext& context, GSplatRenderData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<GSplatPass, GSplatRenderStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "GSplat",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(GSplat, Vertex),
                            GET_PASS_SHADER_BYTECODE(GSplat, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kGSplat_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                const rhi::Buffer* splatData = &data.gsplatScene->getSplatDataBuffer();
                const rhi::Buffer* splatIndices = context.getBuffer(data.splatIndices);
                if (bindGroupCache.needUpdate(splatData, splatIndices))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kGSplat_Bindings);
                            writer.setBuffer("sGPUSplatDataBuffer", splatData);
                            writer.setBuffer("VisibleSplatIndexBuffer", splatIndices);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createGSplatRenderPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        bindGroupCache.getBindGroup()->getLayout()
                    );
                }

                auto& cmdList = context.getCommandList();

                context.beginPass();

                cmdList.bindPipeline(storage.pipeline.get());

                setViewportAndScissor(cmdList, data.viewport);

                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.setBindGroup(3, rhi::PipelineBindPoint::Graphics, &storage.pipeline->getLayout(), data.instanceDataBindGroup, {});

                const rhi::Buffer* gsplatDrawIndirect = context.getBuffer(data.gsplatDrawIndirect);
                cmdList.drawIndirect(
                    gsplatDrawIndirect,
                    0,
                    1,
                    sizeof(DrawIndirectCommand)
                );
            },
            rg::PassFlags::Skip_Begin_Pass
        );
    }

    struct GSplatDepthData
    {
        uint32_t viewId;
        const rhi::BindGroup* instanceDataBindGroup;
        float4 viewport;
        const GSplatScene* gsplatScene;
        rg::TextureHandle gsplatDepth;
        rg::BufferHandle splatIndices;
        rg::BufferHandle gsplatDrawIndirect;
    };

    struct GSplatDepthStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<3>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void GSplatPass::addGSplatDepthPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const GSplatScene& gsplatScene
    )
    {
        renderGraph.addGraphicsPass<GSplatDepthData>(
            "GSplatDepth"_hs,
            [&](rg::RenderGraph::Builder& builder, GSplatDepthData& data)
            {
                builder.markSideEffect();
                data.viewId = view.getViewId();
                data.instanceDataBindGroup = &scene.getInstanceDataBindGroup();
                data.viewport = view.getViewport();
                data.gsplatScene = &gsplatScene;

                uint32_t width = uint32_t(data.viewport.z);
                uint32_t height = uint32_t(data.viewport.w);

                auto& blackboard = builder.getBlackboard();

                rhi::TextureDesc gsplatDepthDesc;
                gsplatDepthDesc.type = rhi::TextureType::Texture_2D;
                gsplatDepthDesc.format = rhi::TextureFormat::RGBA32_SFLOAT;
                gsplatDepthDesc.extent = { width, height, 1 };
                gsplatDepthDesc.mipCount = 1;
                gsplatDepthDesc.layerCount = 1;
                gsplatDepthDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto gsplatDepth = builder.createTexture("GSplatDepth"_hs(data.viewId), gsplatDepthDesc);
                data.gsplatDepth = builder.addColorAttachment(gsplatDepth, rhi::AttachmentLoadOp::Clear);
                blackboard.put("GSplatDepth"_hs(data.viewId), data.gsplatDepth);

                auto splatIndices = blackboard.get<rg::BufferHandle>("SplatIndices"_hs(data.viewId));
                data.splatIndices = builder.accessBuffer(splatIndices, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

                auto gsplatDrawIndirect = blackboard.get<rg::BufferHandle>("GSplatDrawIndirect"_hs(data.viewId));
                data.gsplatDrawIndirect = builder.accessBuffer(gsplatDrawIndirect, rhi::BufferUsage::Indirect | rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
            },
            [&rendererContext](rg::ExecuteContext& context, GSplatDepthData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<GSplatPass, GSplatDepthStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "GSplatDepth",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(GSplatDepth, Vertex),
                            GET_PASS_SHADER_BYTECODE(GSplatDepth, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kGSplatDepth_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                const rhi::Buffer* splatData = &data.gsplatScene->getSplatDataBuffer();
                const rhi::Buffer* splatIndices = context.getBuffer(data.splatIndices);
                const rhi::Buffer* gsplatDrawIndirect = context.getBuffer(data.gsplatDrawIndirect);
                if (bindGroupCache.needUpdate(splatData, splatIndices, gsplatDrawIndirect))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kGSplatDepth_Bindings);
                            writer.setBuffer("sGPUSplatDataBuffer", splatData);
                            writer.setBuffer("VisibleSplatIndexBuffer", splatIndices);
                            writer.setBuffer("DrawIndirectBuffer", gsplatDrawIndirect);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createGSplatDepthPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        bindGroupCache.getBindGroup()->getLayout()
                    );
                }

                auto& cmdList = context.getCommandList();

                setViewportAndScissor(cmdList, data.viewport);

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.setBindGroup(3, rhi::PipelineBindPoint::Graphics, &storage.pipeline->getLayout(), data.instanceDataBindGroup, {});

                cmdList.drawIndirect(
                    gsplatDrawIndirect,
                    0,
                    1,
                    sizeof(DrawIndirectCommand)
                );
            }
        );
    }

    rhi::Pipeline* GSplatPass::createGSplatRenderPipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& bindGroupLayout
    )
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr, &rendererContext.getInstanceDataBindGroupLayout());
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        // 新 RHI 没有 Triangle_Fan(WebGPU 不支持),GSplat/GSplatDepth 顶点 shader 画的四边形改用
        // Triangle_Strip + strip 顺序的 kQuadPositions(见对应 .slang 文件),不能用 Triangle_List——
        // 4 个顶点按 list 只够凑 1 个三角形,会导致每个 splat 只画出四边形的一半
        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::billboard();
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.colorBlendState = ColorBlendState::get <
            rhi::ColorBlendAttachmentState{ true, rhi::BlendFactor::Src_Alpha, rhi::BlendFactor::One_Minus_Src_Alpha, rhi::BlendOp::Add,
            rhi::BlendFactor::Zero, rhi::BlendFactor::One, rhi::BlendOp::Add, 0xF }
        > ();
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            rhi::TextureFormat::D32_SFLOAT_S8_UINT,
            rhi::TextureFormat::RGBA16_SFLOAT
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

    rhi::Pipeline* GSplatPass::createGSplatDepthPipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& bindGroupLayout
    )
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr, &rendererContext.getInstanceDataBindGroupLayout());
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        // 新 RHI 没有 Triangle_Fan(WebGPU 不支持),GSplat/GSplatDepth 顶点 shader 画的四边形改用
        // Triangle_Strip + strip 顺序的 kQuadPositions(见对应 .slang 文件),不能用 Triangle_List——
        // 4 个顶点按 list 只够凑 1 个三角形,会导致每个 splat 只画出四边形的一半
        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::billboard();
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.colorBlendState = ColorBlendState::get <
            rhi::ColorBlendAttachmentState{ true, rhi::BlendFactor::Dst_Alpha, rhi::BlendFactor::One, rhi::BlendOp::Add,
            rhi::BlendFactor::Zero, rhi::BlendFactor::Src_Alpha, rhi::BlendOp::Add, 0xF }
        > ();
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            rhi::TextureFormat::Undefined,
            rhi::TextureFormat::RGBA32_SFLOAT
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
