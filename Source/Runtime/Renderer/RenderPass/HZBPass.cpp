#include "HZBPass.h"
#include "RenderPassUtils.h"

#include <HZBSeed.generated.h>
#include <HZBReduce.generated.h>

#include <algorithm>
#include <bit>

namespace hyp
{
    namespace
    {
        // HString::operator()(uint32_t) 是覆盖 mNumber 这一个槽位, 不是哈希合并(见
        // HString.cpp:209-214)——mip 必须跟原来一样烤进字面量本身, operator() 这一个数字槽位
        // 留给 viewId。mip0(baseExtent)尺寸封顶 kMaxHZBBaseExtentDim(8192px, 远超现实分辨率),
        // kMaxMipCount 是 floor(log2(8192))=13, 两者在 getMipCount() 里严格对应, 不是凑巧不越界
        constexpr uint32_t kMaxHZBBaseExtentDim = 8192u;
        constexpr uint32_t kMaxMipCount = 13u;
        const HString kMipNames[kMaxMipCount] = {
            "SceneHZBMip0"_hs, "SceneHZBMip1"_hs, "SceneHZBMip2"_hs, "SceneHZBMip3"_hs,
            "SceneHZBMip4"_hs, "SceneHZBMip5"_hs, "SceneHZBMip6"_hs, "SceneHZBMip7"_hs,
            "SceneHZBMip8"_hs, "SceneHZBMip9"_hs, "SceneHZBMip10"_hs, "SceneHZBMip11"_hs,
            "SceneHZBMip12"_hs,
        };

        HString mipTextureName(uint32_t mip, uint32_t viewId)
        {
            return kMipNames[mip](viewId);
        }

        uint2 mipExtent(uint2 baseExtent, uint32_t mip)
        {
            return uint2(
                std::max(baseExtent.x >> mip, 1u),
                std::max(baseExtent.y >> mip, 1u));
        }

        uint2 dispatchGroupCountFor(uint2 extent)
        {
            return uint2(
                std::max((extent.x + 7u) / 8u, 1u),
                std::max((extent.y + 7u) / 8u, 1u));
        }

        rhi::TextureViewDesc makeMipViewDesc(uint32_t mip)
        {
            rhi::TextureViewDesc desc;
            desc.type = rhi::TextureViewType::Texture_View_2D;
            desc.format = rhi::TextureFormat::RG32_SFLOAT;
            desc.range.aspect = rhi::TextureAspect::Color;
            desc.range.baseMip = mip;
            desc.range.mipCount = 1u;
            desc.range.baseLayer = 0u;
            desc.range.layerCount = 1u;
            return desc;
        }

        struct HZBSeedData
        {
            uint32_t viewId;
            uint2 dispatchGroupCount;
            rg::TextureHandle sceneDepth;
            rg::TextureHandle dst;
        };

        struct HZBReduceData
        {
            uint32_t viewId;
            uint32_t dstMip;
            uint2 dispatchGroupCount;
            rg::TextureHandle src;
            rg::TextureHandle dst;
        };

        struct HZBPassStorage
        {
            std::shared_ptr<PassShader> seedShader;
            TRef<rhi::BindGroupLayout> seedLayout;
            TRef<rhi::Pipeline> seedPipeline;
            std::vector<BindGroupCache<2>> seedCaches;

            std::shared_ptr<PassShader> reduceShader;
            TRef<rhi::BindGroupLayout> reduceLayout;
            TRef<rhi::Pipeline> reducePipeline;
            std::vector<std::vector<BindGroupCache<2>>> reduceCaches;
        };
    }

    uint2 HZBPass::getBaseExtent(uint2 sceneExtent)
    {
        // 先向上取到 2 的幂再取半, 跟 UE SceneTextureReductions.cpp:136-138 一致: HZBTrace.slang
        // 的遍历按 exp2(mip) * (1/baseExtent) 算 tile 尺寸并据此算边界平面, 只有 baseExtent 是
        // 2 的幂时这个尺寸才跟纹理各级 mip 的真实尺寸(baseExtent >> mip, 向下取整)一致
        return uint2(
            std::max(std::bit_ceil(sceneExtent.x) >> 1, 1u),
            std::max(std::bit_ceil(sceneExtent.y) >> 1, 1u));
    }

    uint32_t HZBPass::getMipCount(uint2 baseExtent)
    {
        // 跟 UE SceneTextureReductions.cpp 的 floor(log2(max(HZBSize.x, HZBSize.y))) 同一个
        // 公式——bit_width(x) 对正整数 x 恰好是 floor(log2(x))+1, 减 1 就是 floor(log2(x)),
        // 整数运算, 不会有 std::log2 在整数幂边界的浮点舍入问题。maxDim 先 clamp 到
        // kMaxHZBBaseExtentDim, 保证算出来的 numMips 天然 <= kMaxMipCount(两者按同一个公式
        // 对应), 不是靠事后再 min 一次凑不越界
        const uint32_t maxDim = std::min(std::max({ baseExtent.x, baseExtent.y, 1u }), kMaxHZBBaseExtentDim);
        return std::max(static_cast<uint32_t>(std::bit_width(maxDim)) - 1u, 1u);
    }

    void HZBPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view)
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 sceneExtent(
            static_cast<uint32_t>(viewport.z),
            static_cast<uint32_t>(viewport.w));
        const uint2 baseExtent = getBaseExtent(sceneExtent);
        const uint32_t numMips = getMipCount(baseExtent);

        rg::TextureHandle hzb;
        renderGraph.addComputePass<HZBSeedData>(
            "HZBSeed"_hs,
            [&](rg::RenderGraph::Builder& builder, HZBSeedData& data)
            {
                data.viewId = viewId;
                data.dispatchGroupCount = dispatchGroupCountFor(baseExtent);
                data.sceneDepth = builder.sample(
                    builder.getBlackboard().get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));

                rhi::TextureDesc hzbDesc;
                hzbDesc.type = rhi::TextureType::Texture_2D;
                hzbDesc.format = rhi::TextureFormat::RG32_SFLOAT;
                hzbDesc.extent = { baseExtent.x, baseExtent.y, 1u };
                hzbDesc.mipCount = numMips;
                hzbDesc.layerCount = 1u;
                hzbDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                hzb = builder.createTexture("SceneHZB"_hs(viewId), hzbDesc);

                rg::TextureHandle mip0 = builder.createSubTexture(
                    mipTextureName(0u, viewId), hzb, makeMipViewDesc(0u));
                data.dst = builder.storage(mip0, rg::AccessType::Write_Only);
                hzb = builder.syncTextures(data.dst, hzb);

                // baseExtent 已经小到 getMipCount() 只给得出 1 级 mip 时, reduce 循环一次都不会
                // 跑, 发布 blackboard 的责任落到这里, 不能指望循环最后一次迭代
                if (numMips <= 1u)
                    builder.getBlackboard().put("SceneHZB"_hs(viewId), hzb);
            },
            [&rendererContext](rg::ExecuteContext& context, HZBSeedData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<HZBPass, HZBPassStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.seedShader)
                {
                    storage.seedShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "HZBSeed",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(HZBSeed, Compute)));
                    storage.seedLayout = createPassBindGroupLayout(renderBackend, generated::kHZBSeed_Bindings);
                    storage.seedPipeline = createComputePipeline(
                        rendererContext, storage.seedShader->getVariant(), *storage.seedLayout);
                }

                if (storage.seedCaches.size() <= data.viewId)
                    storage.seedCaches.resize(data.viewId + 1u);
                auto& cache = storage.seedCaches[data.viewId];

                rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                rhi::TextureView* dst = context.getTexture(data.dst);
                if (cache.needUpdate(sceneDepth, dst))
                {
                    cache.update(renderBackend, *storage.seedLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kHZBSeed_Bindings);
                        writer.setTexture("SceneDepth", sceneDepth);
                        writer.setTexture("SceneHZBMip0", dst);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.seedPipeline.get());
                cmdList.setBindGroup(
                    2, rhi::PipelineBindPoint::Compute,
                    &storage.seedPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            });

        for (uint32_t dstMip = 1u; dstMip < numMips; ++dstMip)
        {
            renderGraph.addComputePass<HZBReduceData>(
                "HZBReduce"_hs(dstMip),
                [&, dstMip](rg::RenderGraph::Builder& builder, HZBReduceData& data)
                {
                    data.viewId = viewId;
                    data.dstMip = dstMip;
                    data.dispatchGroupCount = dispatchGroupCountFor(mipExtent(baseExtent, dstMip));

                    rg::TextureHandle src = builder.createSubTexture(
                        mipTextureName(dstMip - 1u, viewId), hzb, makeMipViewDesc(dstMip - 1u));
                    data.src = builder.sample(src);

                    rg::TextureHandle dst = builder.createSubTexture(
                        mipTextureName(dstMip, viewId), hzb, makeMipViewDesc(dstMip));
                    data.dst = builder.storage(dst, rg::AccessType::Write_Only);
                    hzb = builder.syncTextures(data.dst, hzb);

                    if (dstMip + 1u == numMips)
                        builder.getBlackboard().put("SceneHZB"_hs(viewId), hzb);
                },
                [&rendererContext](rg::ExecuteContext& context, HZBReduceData& data)
                {
                    static auto& storage = rendererContext.getPassStorage().getOrCreate<HZBPass, HZBPassStorage>();
                    auto& renderBackend = context.getRenderBackend();

                    if (!storage.reduceShader)
                    {
                        storage.reduceShader = std::make_shared<PassShader>(
                            renderBackend.getDevice(),
                            "HZBReduce",
                            makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(HZBReduce, Compute)));
                        storage.reduceLayout = createPassBindGroupLayout(renderBackend, generated::kHZBReduce_Bindings);
                        storage.reducePipeline = createComputePipeline(
                            rendererContext, storage.reduceShader->getVariant(), *storage.reduceLayout);
                    }

                    if (storage.reduceCaches.size() <= data.viewId)
                        storage.reduceCaches.resize(data.viewId + 1u);
                    auto& viewCaches = storage.reduceCaches[data.viewId];
                    if (viewCaches.size() <= data.dstMip)
                        viewCaches.resize(data.dstMip + 1u);
                    auto& cache = viewCaches[data.dstMip];

                    rhi::TextureView* src = context.getTexture(data.src);
                    rhi::TextureView* dst = context.getTexture(data.dst);
                    if (cache.needUpdate(src, dst))
                    {
                        cache.update(renderBackend, *storage.reduceLayout, [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kHZBReduce_Bindings);
                            writer.setTexture("SrcHZBMip", src);
                            writer.setTexture("DstHZBMip", dst);
                            writer.apply(bindGroup);
                        });
                    }

                    auto& cmdList = context.getCommandList();
                    cmdList.bindPipeline(storage.reducePipeline.get());
                    cmdList.setBindGroup(
                        2, rhi::PipelineBindPoint::Compute,
                        &storage.reducePipeline->getLayout(), cache.getBindGroup(), {});
                    cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
                });
        }
    }
}
