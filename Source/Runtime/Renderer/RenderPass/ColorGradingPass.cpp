#include "ColorGradingPass.h"
#include "RenderPassUtils.h"

#include <ColorGradingLut.generated.h>
#include <ColorGrading.generated.h>

namespace hyp
{
    void ColorGradingPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view,
        PostProcessState& postProcess
    )
    {
        // LUT 纹理常驻在 PostProcessState 上, 不是本帧新建的资源。import 时永远声明成 Shader_Sampled——
        // apply pass 每帧都以采样它结束, RenderGraph 会在最后一次访问后自动把它转回这个 declare 的
        // 状态, 所以这个声明本身也一直是真的。第一次真正使用前, PostProcessState::prepareColorGradingLut
        // 已经在 Upload transfer pass 里手动把它从 Undefined 转到了 Shader_Sampled。
        rg::TextureHandle lut = renderGraph.importTexture(
            "ColorGradingLut"_hs(postProcess.getViewId()),
            &postProcess.getColorGradingLutView(),
            rhi::ResourceState::Shader_Sampled
        );

        if (postProcess.isColorGradingLutDirty())
        {
            lut = addColorGradingLutPass(renderGraph, rendererContext, lut, postProcess);
            postProcess.clearColorGradingLutDirty();
        }

        addColorGradingApplyPass(renderGraph, rendererContext, view, lut, postProcess);
    }

    struct ColorGradingLutData
    {
        uint32_t viewId;
        uint32_t toneMapper;
        rg::TextureHandle lut;
    };

    struct ColorGradingLutStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<1>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    rg::TextureHandle ColorGradingPass::addColorGradingLutPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        rg::TextureHandle lut,
        PostProcessState& postProcess
    )
    {
        auto& pass = renderGraph.addComputePass<ColorGradingLutData>(
            "ColorGradingLut"_hs,
            [&](rg::RenderGraph::Builder& builder, ColorGradingLutData& data)
            {
                data.viewId = postProcess.getViewId();
                data.toneMapper = static_cast<uint32_t>(postProcess.getPostProcessSettings().toneMapping.toneMapper);
                data.lut = builder.storage(lut, rg::AccessType::Write_Only);
            },
            [&rendererContext](rg::ExecuteContext& context, ColorGradingLutData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<ColorGradingPass, ColorGradingLutStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ColorGradingLut",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ColorGradingLut, Compute))
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kColorGradingLut_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                rhi::TextureView* lut = context.getTexture(data.lut);
                if (bindGroupCache.needUpdate(lut))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kColorGradingLut_Bindings);
                            writer.setTexture("ColorGradingLutImage", lut);
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
                        reinterpret_cast<const uint8_t*>(&data.toneMapper),
                        sizeof(data.toneMapper)
                    )
                );
                // 32^3 / numthreads(4,4,4)
                cmdList.dispatch(8, 8, 8);
            }
        );
        return pass.getData().lut;
    }

    struct ColorGradingConstants
    {
        float bloomIntensity;
    };
    static_assert(sizeof(ColorGradingConstants) == 4);

    struct ColorGradingApplyData
    {
        uint32_t viewId;
        float4 viewport;
        ColorGradingConstants constants;
        rg::TextureHandle lut;
        rg::TextureHandle sceneColor;
        rg::BufferHandle autoExposure;
        rg::TextureHandle bloomTexture;
        bool hasBloomTexture;
    };

    struct ColorGradingApplyStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<5>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void ColorGradingPass::addColorGradingApplyPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view,
        rg::TextureHandle lut,
        PostProcessState& postProcess
    )
    {
        renderGraph.addGraphicsPass<ColorGradingApplyData>(
            "ColorGrading"_hs,
            [&](rg::RenderGraph::Builder& builder, ColorGradingApplyData& data)
            {
                data.viewId = view.getViewId();
                data.viewport = view.getViewport();
                data.constants = { postProcess.getPostProcessSettings().bloom.intensity };

                data.lut = builder.sample(lut);

                auto& blackboard = builder.getBlackboard();
                auto sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(data.viewId));
                data.sceneColor = builder.sample(sceneColor);

                // Exposure 关闭时 AutoExposurePass 整个不跑, blackboard 里没有这个条目——直接 import
                // PostProcessState 常驻的 buffer, 它此时还停在 PostProcessState::prepareAutoExposureData()
                // 写入的初始值 float4(1,1,0,1), 换算出的 exposureCorrection 恒等于 1
                auto autoExposure = blackboard.get<rg::BufferHandle>("AutoExposureData"_hs(data.viewId));
                if (!autoExposure.isValid())
                {
                    autoExposure = builder.importBuffer(
                        "AutoExposureData"_hs(data.viewId),
                        &postProcess.getAutoExposureDataBuffer());
                }
                data.autoExposure = builder.accessBuffer(
                    autoExposure,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Read_Only);

                // BloomPass 关掉/没跑过时 blackboard 里没有这个条目, 回退到 execute 里的黑纹理
                auto bloomTexture = blackboard.get<rg::TextureHandle>("BloomTexture"_hs(data.viewId));
                data.hasBloomTexture = bloomTexture.isValid();
                if (data.hasBloomTexture)
                {
                    data.bloomTexture = builder.sample(bloomTexture);
                }

                // Tone Mapping 之后的结果重新写回一张同规格的新纹理, 再覆盖 blackboard 里的
                // "SceneColor"——和 ResolveSceneColor(CopyToRenderTargetPass.cpp)同一种写法:
                // 不能在同一个 pass 里既采样又写同一张附件
                const uint2 extent(
                    static_cast<uint32_t>(data.viewport.z),
                    static_cast<uint32_t>(data.viewport.w)
                );
                rhi::TextureDesc gradedSceneColorDesc;
                gradedSceneColorDesc.type = rhi::TextureType::Texture_2D;
                gradedSceneColorDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                gradedSceneColorDesc.extent = { extent.x, extent.y, 1 };
                gradedSceneColorDesc.mipCount = 1;
                gradedSceneColorDesc.layerCount = 1;
                gradedSceneColorDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto gradedSceneColor = builder.createTexture("GradedSceneColor"_hs(data.viewId), gradedSceneColorDesc);
                gradedSceneColor = builder.addColorAttachment(gradedSceneColor, rhi::AttachmentLoadOp::Dont_Care);
                blackboard.put("SceneColor"_hs(data.viewId), gradedSceneColor);
            },
            [&rendererContext](rg::ExecuteContext& context, ColorGradingApplyData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<ColorGradingPass, ColorGradingApplyStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ColorGrading",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(ColorGrading, Vertex),
                            GET_PASS_SHADER_BYTECODE(ColorGrading, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kColorGrading_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                rhi::TextureView* lut = context.getTexture(data.lut);
                rhi::TextureView* sceneColor = context.getTexture(data.sceneColor);
                rhi::Buffer* autoExposure = context.getBuffer(data.autoExposure);
                rhi::TextureView* bloomTexture = data.hasBloomTexture
                    ? context.getTexture(data.bloomTexture)
                    : &renderBackend.getDefaultResources().getBlackTextureView();

                if (bindGroupCache.needUpdate(linearClampNoMipSampler, lut, sceneColor, autoExposure, bloomTexture))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kColorGrading_Bindings);
                            writer.setSampler("LutSampler", linearClampNoMipSampler);
                            writer.setTexture("ColorGradingLutTexture", lut);
                            writer.setTexture("SceneColor", sceneColor);
                            writer.setBuffer("AutoExposureData", autoExposure);
                            writer.setTexture("BloomTexture", bloomTexture);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createColorGradingApplyPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout
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
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(&data.constants),
                        sizeof(data.constants))
                );
                cmdList.draw(3, 1, 0, 0);
            }
        );
    }

    rhi::Pipeline* ColorGradingPass::createColorGradingApplyPipeline(
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
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            rhi::TextureFormat::Undefined,
            rhi::TextureFormat::RGBA16_SFLOAT
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
