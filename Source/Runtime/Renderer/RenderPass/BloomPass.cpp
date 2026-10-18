#include "BloomPass.h"
#include "RenderPassUtils.h"

#include <BloomDownsample.generated.h>
#include <BloomUpsample.generated.h>

#include <bit>

namespace hyp
{
    namespace
    {
        // BloomSettings::iterations 的上限——决定了下面按 mip 索引区分身份的静态 HString 数组
        // 要开多大, 8 级降采样已经远超实际会用到的范围(半分辨率图再降 8 次早就到 1x1 了)
        constexpr uint32_t kMaxBloomIterations = 8;

        struct BloomDownsampleConstants
        {
            float2 srcTexelSize;
            uint32_t useKarisAverage;
            float pad;
        };
        static_assert(sizeof(BloomDownsampleConstants) == 16);

        struct BloomUpsampleConstants
        {
            float2 srcTexelSize;
            float radius;
            float weight;
            // 非零表示这一步要叠加到 dst mip 已有的内容上, 对应 shader 里的 read-modify-write
            // 分支——升采样从 render pass(硬件 (One,One) blend)迁移成 compute pass(storage
            // image 写)之后, 混合方式没法再交给 blend state, 得由 shader 自己按这个标志判断。
            uint32_t additiveBlend;
        };
        static_assert(sizeof(BloomUpsampleConstants) == 20);

        // 升采样每一步在已有真实内容的 mip 上做加法混合(见 BloomUpsample.slang 里 weight 的
        // 注释), 权重 0.5 的等比衰减让累加结果收敛, 不随 iterations 线性暴涨。最后一步写进
        // 从未被写过的专属 mip0, 起点是 0, 不需要衰减
        constexpr float kUpsampleAccumulateWeight = 0.5f;
        constexpr float kUpsampleFinalWeight = 1.0f;

        struct BloomDownsampleData
        {
            uint32_t viewId;
            uint32_t step;
            uint2 dispatchGroupCount;
            BloomDownsampleConstants constants;
            rg::TextureHandle src;
            rg::TextureHandle dst;
        };

        struct BloomUpsampleData
        {
            uint32_t viewId;
            uint32_t step;
            uint2 dispatchGroupCount;
            BloomUpsampleConstants constants;
            rg::TextureHandle src;
            rg::TextureHandle dst;
        };

        struct BloomPassStorage
        {
            std::shared_ptr<PassShader> downsampleShader;
            TRef<rhi::BindGroupLayout> downsampleLayout;
            TRef<rhi::Pipeline> downsamplePipeline;

            std::shared_ptr<PassShader> upsampleShader;
            TRef<rhi::BindGroupLayout> upsampleLayout;
            TRef<rhi::Pipeline> upsamplePipeline;

            std::vector<std::vector<BindGroupCache<3>>> downsampleCaches;
            std::vector<std::vector<BindGroupCache<3>>> upsampleCaches;
        };

        // BloomPyramid 每一级 mip 的 subtexture 名字——mip 索引已经在字符串里区分开了,
        // 剩下的 HString::operator()(number) 用来按 viewId 再区分不同视口各自的金字塔
        const HString kMipNames[kMaxBloomIterations + 1] = {
            "BloomPyramidMip0"_hs,
            "BloomPyramidMip1"_hs,
            "BloomPyramidMip2"_hs,
            "BloomPyramidMip3"_hs,
            "BloomPyramidMip4"_hs,
            "BloomPyramidMip5"_hs,
            "BloomPyramidMip6"_hs,
            "BloomPyramidMip7"_hs,
            "BloomPyramidMip8"_hs,
        };

        uint2 mipResolution(uint2 baseResolution, uint32_t mip)
        {
            return uint2(
                std::max(baseResolution.x >> mip, 1u),
                std::max(baseResolution.y >> mip, 1u));
        }

        uint2 dispatchGroupCountFor(uint2 resolution)
        {
            return uint2(
                std::max((resolution.x + 7u) / 8u, 1u),
                std::max((resolution.y + 7u) / 8u, 1u));
        }

        // halfResolution 能有意义地对半降采样几次(降到 1x1 为止)——不看这个直接按用户设置的
        // iterations 跑满, 分辨率小的时候后面几级会在已经是 1x1 的纹理上反复空转(mipResolution()
        // 的 max(...,1u) 只保证不出现 0 大小, 不会主动少跑几级)。std::bit_width(x)-1 就是
        // floor(log2(x)), min 维度为 0/1 时钳到至少 1, 跟原来"iterations 至少是 1"的下限保持一致。
        uint32_t maxIterationsForResolution(uint2 resolution)
        {
            uint32_t minDim = std::min(resolution.x, resolution.y);
            if (minDim <= 1u)
                return 1u;
            return std::max(static_cast<uint32_t>(std::bit_width(minDim)) - 1u, 1u);
        }
    }

    void BloomPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view,
        PostProcessState& postProcess)
    {
        const uint32_t viewId = view.getViewId();
        const BloomSettings& bloomSettings = postProcess.getPostProcessSettings().bloom;
        const float upsampleRadius = bloomSettings.upsampleRadius;

        const float4 viewport = view.getViewport();
        const uint2 halfResolution(
            (static_cast<uint32_t>(viewport.z) + 1u) / 2u,
            (static_cast<uint32_t>(viewport.w) + 1u) / 2u);

        // 先按用户设置钳到 [1, kMaxBloomIterations], 再钳到这个分辨率实际能撑住的级数——
        // 小视口(比如编辑器缩略图、极端窄的分屏)不应该跑满 8 级降采样, 后面几级只是在 1x1
        // 纹理上空转, 见 maxIterationsForResolution() 的注释。
        const uint32_t iterations = std::min(
            std::clamp(bloomSettings.iterations, 1u, kMaxBloomIterations),
            maxIterationsForResolution(halfResolution));

        // BloomPyramid 的 mip0 是专属给最终半分辨率输出用的("BloomTexture"), 从不参与降采样,
        // 只在升采样链路的最后一步被写入一次——所以整张纹理开 iterations+1 级 mip, 但降采样只
        // 产出 mip1..mipIterations, mip0 单独留到最后。
        //
        // 降采样/升采样都在同一张 BloomPyramid 上, 每一步都是"读一级 mip、写另一级 mip"——如果
        // 用 render pass(color attachment)实现, 读写会落在同一个 WebGPU synchronization scope
        // 里被判定为非法(整个 render pass 算一个 scope, 不看是不是不同 mip 子资源); 改成
        // compute pass 之后, WebGPU 规范里每一次 dispatch 各自是独立的 usage scope, 不同 mip
        // 子资源的读写不会冲突, 所以这里全部用 storage image 读写, 不用 color attachment。
        rg::TextureHandle pyramid;

        for (uint32_t step = 0; step < iterations; ++step)
        {
            renderGraph.addComputePass<BloomDownsampleData>(
                "BloomDownsample"_hs(step),
                [&, step](rg::RenderGraph::Builder& builder, BloomDownsampleData& data)
                {
                    const uint32_t dstMip = step + 1u;
                    const uint2 dstResolution = mipResolution(halfResolution, dstMip);
                    const uint2 srcResolution = mipResolution(halfResolution, step);

                    data.viewId = viewId;
                    data.step = step;
                    data.dispatchGroupCount = dispatchGroupCountFor(dstResolution);
                    data.constants = {
                        float2(1.0f / float(srcResolution.x), 1.0f / float(srcResolution.y)),
                        step == 0u ? 1u : 0u,
                        0.0f
                    };

                    if (step == 0u)
                    {
                        rhi::TextureDesc pyramidDesc;
                        pyramidDesc.type = rhi::TextureType::Texture_2D;
                        pyramidDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                        pyramidDesc.extent = { halfResolution.x, halfResolution.y, 1u };
                        pyramidDesc.mipCount = iterations + 1u;
                        pyramidDesc.layerCount = 1u;
                        pyramidDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                        pyramid = builder.createTexture("BloomPyramid"_hs(viewId), pyramidDesc);

                        rg::TextureHandle sceneColorHalfRes = builder.getBlackboard().get<rg::TextureHandle>(
                            "SceneColorHalfRes"_hs(viewId));
                        data.src = builder.sample(sceneColorHalfRes);
                    }
                    else
                    {
                        rhi::TextureViewDesc srcDesc;
                        srcDesc.type = rhi::TextureViewType::Texture_View_2D;
                        srcDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                        srcDesc.range.aspect = rhi::TextureAspect::Color;
                        srcDesc.range.baseMip = step;
                        srcDesc.range.mipCount = 1;
                        srcDesc.range.baseLayer = 0;
                        srcDesc.range.layerCount = 1;
                        rg::TextureHandle srcMip = builder.createSubTexture(kMipNames[step](viewId), pyramid, srcDesc);
                        data.src = builder.sample(srcMip);
                    }

                    rhi::TextureViewDesc dstDesc;
                    dstDesc.type = rhi::TextureViewType::Texture_View_2D;
                    dstDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                    dstDesc.range.aspect = rhi::TextureAspect::Color;
                    dstDesc.range.baseMip = dstMip;
                    dstDesc.range.mipCount = 1;
                    dstDesc.range.baseLayer = 0;
                    dstDesc.range.layerCount = 1;
                    rg::TextureHandle dstMipHandle = builder.createSubTexture(kMipNames[dstMip](viewId), pyramid, dstDesc);
                    data.dst = builder.storage(dstMipHandle, rg::AccessType::Write_Only);
                    pyramid = builder.syncTextures(data.dst, pyramid);
                },
                [&rendererContext](rg::ExecuteContext& context, BloomDownsampleData& data)
                {
                    static auto& storage = rendererContext.getPassStorage().getOrCreate<BloomPass, BloomPassStorage>();
                    auto& renderBackend = context.getRenderBackend();

                    if (!storage.downsampleShader)
                    {
                        storage.downsampleShader = std::make_shared<PassShader>(
                            renderBackend.getDevice(),
                            "BloomDownsample",
                            makeComputeBytecode(
                                renderBackend.getRHIBackend(),
                                GET_PASS_SHADER_BYTECODE(BloomDownsample, Compute)));
                        storage.downsampleLayout = createPassBindGroupLayout(
                            renderBackend,
                            generated::kBloomDownsample_Bindings);
                        storage.downsamplePipeline = createComputePipeline(
                            rendererContext,
                            storage.downsampleShader->getVariant(),
                            *storage.downsampleLayout);
                    }

                    if (storage.downsampleCaches.size() <= data.viewId)
                        storage.downsampleCaches.resize(data.viewId + 1u);
                    auto& viewCaches = storage.downsampleCaches[data.viewId];
                    if (viewCaches.size() <= data.step)
                        viewCaches.resize(data.step + 1u);
                    auto& cache = viewCaches[data.step];

                    rhi::TextureView* src = context.getTexture(data.src);
                    rhi::TextureView* dst = context.getTexture(data.dst);
                    rhi::Sampler* sampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                    if (cache.needUpdate(sampler, src, dst))
                    {
                        cache.update(renderBackend, *storage.downsampleLayout, [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kBloomDownsample_Bindings);
                            writer.setSampler("LinearClampSampler", sampler);
                            writer.setTexture("SrcTexture", src);
                            writer.setTexture("DstTexture", dst);
                            writer.apply(bindGroup);
                        });
                    }

                    auto& cmdList = context.getCommandList();
                    cmdList.bindPipeline(storage.downsamplePipeline.get());
                    cmdList.setBindGroup(
                        2,
                        rhi::PipelineBindPoint::Compute,
                        &storage.downsamplePipeline->getLayout(),
                        cache.getBindGroup(),
                        {});
                    cmdList.setImmediateData(
                        &storage.downsamplePipeline->getLayout(),
                        0,
                        std::span<const uint8_t>(
                            reinterpret_cast<const uint8_t*>(&data.constants),
                            sizeof(data.constants)));
                    cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
                }
            );
        }

        for (uint32_t step = 0; step < iterations; ++step)
        {
            renderGraph.addComputePass<BloomUpsampleData>(
                "BloomUpsample"_hs(step),
                [&, step](rg::RenderGraph::Builder& builder, BloomUpsampleData& data)
                {
                    const uint32_t srcMip = iterations - step;
                    const uint32_t dstMip = srcMip - 1u;
                    const bool finalStep = dstMip == 0u;
                    const uint2 srcResolution = mipResolution(halfResolution, srcMip);
                    const uint2 dstResolution = mipResolution(halfResolution, dstMip);

                    data.viewId = viewId;
                    data.step = step;
                    data.dispatchGroupCount = dispatchGroupCountFor(dstResolution);
                    data.constants = {
                        float2(1.0f / float(srcResolution.x), 1.0f / float(srcResolution.y)),
                        upsampleRadius,
                        finalStep ? kUpsampleFinalWeight : kUpsampleAccumulateWeight,
                        finalStep ? 0u : 1u
                    };

                    rhi::TextureViewDesc srcDesc;
                    srcDesc.type = rhi::TextureViewType::Texture_View_2D;
                    srcDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                    srcDesc.range.aspect = rhi::TextureAspect::Color;
                    srcDesc.range.baseMip = srcMip;
                    srcDesc.range.mipCount = 1;
                    srcDesc.range.baseLayer = 0;
                    srcDesc.range.layerCount = 1;
                    rg::TextureHandle srcMipHandle = builder.createSubTexture(kMipNames[srcMip](viewId), pyramid, srcDesc);
                    data.src = builder.sample(srcMipHandle);

                    rhi::TextureViewDesc dstDesc;
                    dstDesc.type = rhi::TextureViewType::Texture_View_2D;
                    dstDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                    dstDesc.range.aspect = rhi::TextureAspect::Color;
                    dstDesc.range.baseMip = dstMip;
                    dstDesc.range.mipCount = 1;
                    dstDesc.range.baseLayer = 0;
                    dstDesc.range.layerCount = 1;
                    rg::TextureHandle dstMipHandle = builder.createSubTexture(kMipNames[dstMip](viewId), pyramid, dstDesc);
                    // 除最后一步外, dst mip 在降采样阶段已经写过真实数据, shader 里要读旧值做
                    // 加法混合(见 PC.additiveBlend), 所以这里按 Read_Write 声明, 不是 Write_Only
                    data.dst = builder.storage(dstMipHandle, finalStep ? rg::AccessType::Write_Only : rg::AccessType::Read_Write);
                    pyramid = builder.syncTextures(data.dst, pyramid);

                    if (finalStep)
                    {
                        builder.getBlackboard().put("BloomTexture"_hs(viewId), data.dst);
                    }
                },
                [&rendererContext](rg::ExecuteContext& context, BloomUpsampleData& data)
                {
                    static auto& storage = rendererContext.getPassStorage().getOrCreate<BloomPass, BloomPassStorage>();
                    auto& renderBackend = context.getRenderBackend();

                    if (!storage.upsampleShader)
                    {
                        storage.upsampleShader = std::make_shared<PassShader>(
                            renderBackend.getDevice(),
                            "BloomUpsample",
                            makeComputeBytecode(
                                renderBackend.getRHIBackend(),
                                GET_PASS_SHADER_BYTECODE(BloomUpsample, Compute)));
                        storage.upsampleLayout = createPassBindGroupLayout(
                            renderBackend,
                            generated::kBloomUpsample_Bindings);
                        storage.upsamplePipeline = createComputePipeline(
                            rendererContext,
                            storage.upsampleShader->getVariant(),
                            *storage.upsampleLayout);
                    }

                    if (storage.upsampleCaches.size() <= data.viewId)
                        storage.upsampleCaches.resize(data.viewId + 1u);
                    auto& viewCaches = storage.upsampleCaches[data.viewId];
                    if (viewCaches.size() <= data.step)
                        viewCaches.resize(data.step + 1u);
                    auto& cache = viewCaches[data.step];

                    rhi::TextureView* src = context.getTexture(data.src);
                    rhi::TextureView* dst = context.getTexture(data.dst);
                    rhi::Sampler* sampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                    if (cache.needUpdate(sampler, src, dst))
                    {
                        cache.update(renderBackend, *storage.upsampleLayout, [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kBloomUpsample_Bindings);
                            writer.setSampler("LinearClampSampler", sampler);
                            writer.setTexture("SrcTexture", src);
                            writer.setTexture("DstTexture", dst);
                            writer.apply(bindGroup);
                        });
                    }

                    auto& cmdList = context.getCommandList();
                    cmdList.bindPipeline(storage.upsamplePipeline.get());
                    cmdList.setBindGroup(
                        2,
                        rhi::PipelineBindPoint::Compute,
                        &storage.upsamplePipeline->getLayout(),
                        cache.getBindGroup(),
                        {});
                    cmdList.setImmediateData(
                        &storage.upsamplePipeline->getLayout(),
                        0,
                        std::span<const uint8_t>(
                            reinterpret_cast<const uint8_t*>(&data.constants),
                            sizeof(data.constants)));
                    cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
                }
            );
        }
    }
}
