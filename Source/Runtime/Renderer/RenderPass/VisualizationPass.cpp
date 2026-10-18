#include "VisualizationPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>
#include <RenderBase/DefaultResources.h>
#include <RenderBase/GeometryProxy.h>
#include <RenderBase/VertexLayout.h>

#include <VisualizationIcon.generated.h>
#include <VisualizationPoint.generated.h>
#include <VisualizationLine.generated.h>
#include <VisualizationWireframeBox.generated.h>
#include <VisualizationWireframeSphere.generated.h>
#include <VisualizationWireframeCapsule.generated.h>
#include <VisualizationWireframeCylinder.generated.h>
#include <VisualizationWireframeLightCone.generated.h>
#include <VisualizationWireframeGeometry.generated.h>
#include <CopyDepth.generated.h>
#include <VisualizationComposite.generated.h>

#include <unordered_map>

namespace hyp
{
    namespace
    {
        // Visualization 自己的抗锯齿(MSAA)独立于主管线(TAA)之外, 只影响这一张专属的颜色/深度
        // 纹理, 不需要把 SceneColor/SceneDepth 也变成 MSAA。1u << kVisualizationMSAASampleLog2 必须
        // 等于 kVisualizationMSAASampleCount, 见 PipelineCache.cpp 里 GraphicsPipelineState::samples
        // 是 log2 编码而非字面采样数的转换。
        constexpr uint8_t kVisualizationMSAASampleCount = 4;
        constexpr uint32_t kVisualizationMSAASampleLog2 = 2;

        struct VisualizationPassData
        {
            uint32_t viewId;
            float4 viewport;
            const rhi::BindGroup* viewBindGroup;
            uint32_t viewDataDynamicOffset;
            rg::TextureHandle msaaColor;
            rg::TextureHandle msaaDepth;
            VisualizationRenderData renderData;
        };

        struct DepthExpandData
        {
            uint32_t viewId;
            bool hasSceneDepth;
            rg::TextureHandle src;
            rg::TextureHandle dst;
            float4 viewport;
        };

        struct DepthExpandStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> bindGroupLayout;
            std::vector<BindGroupCache<1>> perViewCaches;
            TRef<rhi::Pipeline> pipeline;
        };

        struct CompositeData
        {
            uint32_t viewId;
            float4 viewport;
            const rhi::BindGroup* viewBindGroup;
            uint32_t viewDataDynamicOffset;
            rg::TextureHandle msaaColor;
            rg::TextureHandle finalRT;
        };

        struct CompositeStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> bindGroupLayout;
            std::vector<BindGroupCache<1>> perViewCaches;
            std::unordered_map<uint32_t, TRef<rhi::Pipeline>> pipelines;
        };

        struct VisualizationPassStorage
        {
            std::shared_ptr<PassShader> iconShader;
            TRef<rhi::BindGroupLayout> iconBindGroupLayout;

            std::shared_ptr<PassShader> pointShader;
            TRef<rhi::BindGroupLayout> pointBindGroupLayout;

            std::shared_ptr<PassShader> lineShader;
            TRef<rhi::BindGroupLayout> lineBindGroupLayout;

            std::shared_ptr<PassShader> wireframeBoxShader;
            TRef<rhi::BindGroupLayout> wireframeBoxBindGroupLayout;

            std::shared_ptr<PassShader> wireframeSphereShader;
            TRef<rhi::BindGroupLayout> wireframeSphereBindGroupLayout;

            std::shared_ptr<PassShader> wireframeCapsuleShader;
            TRef<rhi::BindGroupLayout> wireframeCapsuleBindGroupLayout;

            std::shared_ptr<PassShader> wireframeCylinderShader;
            TRef<rhi::BindGroupLayout> wireframeCylinderBindGroupLayout;

            std::shared_ptr<PassShader> wireframeLightConeShader;
            TRef<rhi::BindGroupLayout> wireframeLightConeBindGroupLayout;

            std::shared_ptr<PassShader> wireframeGeometryShader;
            TRef<rhi::BindGroupLayout> wireframeGeometryBindGroupLayout;

            VisualizationShapePipelines pipelines;
            VisualizationShapeBindGroupCaches bindGroupCaches;
        };

        // 深度测试+写入都开着, 画进自己独立的 MSAA 颜色/深度纹理(不是 SceneColor/SceneDepth 本身,
        // 见 addToRenderGraph() 里的 msaaColor/msaaDepth), 不需要顾虑污染后续 pass 要用的深度值。
        // ignoreDepth 条目靠 shader 里把 svPosition.z 钉在 reversed-Z 最近值(1.0)、CPU 侧按距离
        // 排序来实现"始终置顶且组内正确前后排序", 见 VisualizationRenderData::build() 里
        // orderByDepthGroup 的注释, 不需要另外的 pipeline 状态。hasVertexInput 只有 5 个线框
        // unit shape(从 DefaultResources 烘焙的顶点/索引 buffer 里读 POSITION0)为真, icon/point/
        // line 是纯 SV_VertexID 现算, 不声明任何顶点输入
        rhi::Pipeline* createVisualizationPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout,
            rhi::PrimitiveTopology topology,
            bool hasVertexInput
        )
        {
            auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr);
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

            GraphicsPipelineState pipelineState;
            pipelineState.topology = static_cast<uint32_t>(topology);
            pipelineState.samples = kVisualizationMSAASampleLog2;
            pipelineState.cullMode = static_cast<uint32_t>(rhi::CullMode::None);
            pipelineState.frontFace = static_cast<uint32_t>(rhi::FrontFace::Counter_Clockwise);
            pipelineState.depthTestEnable = 1;
            pipelineState.depthWriteEnable = 1;
            pipelineState.depthCompareOp = static_cast<uint32_t>(rhi::CompareOp::Greater_Equal);
            pipelineState.stencilTestEnable = 0;
            pipelineState.stencilState = StencilState::get();
            pipelineState.colorBlendState = ColorBlendState::get<
                rhi::ColorBlendAttachmentState{ true, rhi::BlendFactor::Src_Alpha, rhi::BlendFactor::One_Minus_Src_Alpha, rhi::BlendOp::Add, rhi::BlendFactor::One, rhi::BlendFactor::Zero, rhi::BlendOp::Add, 0xF }
            >();
            pipelineState.shader = shaderVariant;
            if (hasVertexInput)
            {
                pipelineState.vertexLayout = VertexLayout(VertexLayoutType::Static_Mesh, VertexAttributeMask::Position);
            }
            // 线框是纯色 + alpha 混合, 不需要 HDR/浮点精度, RGBA8_UNORM 就够——现在这张
            // 颜色附件还叠了 4x MSAA, 用 RGBA16_SFLOAT 会让显存/带宽多花一倍
            pipelineState.attachmentFormats = AttachmentFormats::get<
                rhi::TextureFormat::D32_SFLOAT,
                rhi::TextureFormat::RGBA8_UNORM
            >();

            PipelineCache::GraphicsKey pipelineKey;
            pipelineKey.pipelineState = pipelineState;
            pipelineKey.pipelineLayout = pipelineLayout;
            return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
        }

        rhi::Pipeline* createDepthExpandPipeline(RendererContext& rendererContext, ShaderVariant shaderVariant, const rhi::BindGroupLayout& bindGroupLayout)
        {
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
            // CopyDepth 只读一张纹理, 不需要 ViewData/场景数据, 跟 PresentToRenderTargetPass
            // 用 CopyTexture 的方式一样, 只用一个槽位放自己这份 BindGroupLayout, 不走
            // createRenderPassPipelineLayout 那套"槽0=全局、槽1=场景视图"的标准 3 槽布局
            PipelineCache::PipelineLayoutKey layoutKey{};
            layoutKey.bindGroupLayouts[0] = &bindGroupLayout;
            TRef<rhi::PipelineLayout> pipelineLayout = pipelineCache.getOrCreateLayout(layoutKey);

            GraphicsPipelineState pipelineState = PipelineStatePresets::fullscreenTriangle();
            pipelineState.samples = kVisualizationMSAASampleLog2;
            pipelineState.shader = shaderVariant;
            // depthWriteEnable 只在 depthTestEnable=true 时才生效(Vulkan 规定), compareOp=Always
            // 让测试恒通过——这里只要"写入"这个副作用, 不需要真的比较, 见 CopyDepth.slang 的注释
            pipelineState.depthTestEnable = 1;
            pipelineState.depthWriteEnable = 1;
            pipelineState.depthCompareOp = static_cast<uint32_t>(rhi::CompareOp::Always);
            pipelineState.attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::D32_SFLOAT>();

            PipelineCache::GraphicsKey pipelineKey;
            pipelineKey.pipelineState = pipelineState;
            pipelineKey.pipelineLayout = pipelineLayout;
            return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
        }

        // 把这一帧的 SceneDepth(单采样)"复制"进一张独立的 MSAA 深度纹理: 普通 fragment shader
        // 每像素只调用一次, 写的 SV_Depth 值会被硬件广播给这个像素覆盖的所有采样点, 不需要
        // per-sample shading。复用 Common/CopyDepth.slang(已经在 DirectLightingPass.cpp 里用于
        // 同样的"给需要独立深度附件的几何体一份深度拷贝"场景, 见该文件里的用法和注释)。
        rg::TextureHandle addDepthExpandPass(rg::RenderGraph& renderGraph, RendererContext& rendererContext, const SceneView& view)
        {
            uint32_t viewId = view.getViewId();
            rg::TextureHandle result;
            renderGraph.addGraphicsPass<DepthExpandData>(
                "VisualizationDepthExpand"_hs,
                [&](rg::RenderGraph::Builder& builder, DepthExpandData& data)
                {
                    data.viewId = viewId;
                    data.viewport = view.getViewport();
                    auto& blackboard = builder.getBlackboard();
                    // 采样用纯 Depth aspect 的视图, 不能用 "SceneDepth" 那个 Depth+Stencil 组合
                    // 视图(Vulkan 规定 sampled view 的 aspectMask 只能选一个), 见
                    // DirectLightingPass.cpp 里 SceneDepthD 的同类用法。
                    rg::TextureHandle sceneDepthD = blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId));
                    // 只有正常延迟管线(GBufferPass)这一帧跑过才会有这张深度——Lumen 可视化模式下
                    // 整条 GBuffer 链路被跳过, 这个 key 从未 put() 过, 拿到的是默认构造的非法 handle
                    data.hasSceneDepth = sceneDepthD.isValid();
                    if (data.hasSceneDepth)
                        data.src = builder.sample(sceneDepthD);

                    rhi::TextureDesc depthDesc;
                    depthDesc.type = rhi::TextureType::Texture_2D;
                    depthDesc.format = rhi::TextureFormat::D32_SFLOAT;
                    depthDesc.extent = { static_cast<uint32_t>(data.viewport.z), static_cast<uint32_t>(data.viewport.w), 1 };
                    depthDesc.mipCount = 1;
                    depthDesc.layerCount = 1;
                    depthDesc.samples = kVisualizationMSAASampleCount;
                    depthDesc.usage = rhi::TextureUsage::Depth_Stencil_Attachment | rhi::TextureUsage::Sampled;
                    auto dst = builder.createTexture("VisualizationMSAADepth"_hs(viewId), depthDesc);
                    // 没有场景深度可拷贝时清成 reversed-Z 远平面(0.0), 让 gizmo 一律画在最上层不做遮挡剔除
                    rhi::AttachmentLoadOp depthLoadOp = data.hasSceneDepth ? rhi::AttachmentLoadOp::Dont_Care : rhi::AttachmentLoadOp::Clear;
                    data.dst = builder.addDepthStencilAttachment(dst, depthLoadOp, true, rhi::AttachmentLoadOp::Dont_Care, false);
                    result = data.dst;
                },
                [&rendererContext](rg::ExecuteContext& context, DepthExpandData& data)
                {
                    if (!data.hasSceneDepth)
                    {
                        // Clear loadOp 已经把这张深度清成远平面, 不需要真的画 CopyDepth
                        context.beginPass();
                        return;
                    }

                    static auto& storage = rendererContext.getPassStorage().getOrCreate<VisualizationPass, DepthExpandStorage>();
                    auto& renderBackend = context.getRenderBackend();

                    if (!storage.shader)
                    {
                        storage.shader = std::make_shared<PassShader>(
                            renderBackend.getDevice(), "CopyDepth",
                            makeVertexFragmentBytecode(
                                renderBackend.getRHIBackend(),
                                GET_PASS_SHADER_BYTECODE(CopyDepth, Vertex),
                                GET_PASS_SHADER_BYTECODE(CopyDepth, Fragment)
                            )
                        );
                    }
                    if (!storage.bindGroupLayout)
                    {
                        storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kCopyDepth_Bindings);
                    }
                    if (!storage.pipeline)
                    {
                        storage.pipeline = createDepthExpandPipeline(rendererContext, storage.shader->getVariant(), *storage.bindGroupLayout);
                    }

                    const rhi::TextureView* src = context.getTexture(data.src);
                    if (storage.perViewCaches.size() <= data.viewId)
                    {
                        storage.perViewCaches.resize(data.viewId + 1);
                    }
                    auto& bindGroupCache = storage.perViewCaches[data.viewId];
                    if (bindGroupCache.needUpdate(src))
                    {
                        bindGroupCache.update(
                            renderBackend, *storage.bindGroupLayout,
                            [&](rhi::BindGroup& bindGroup)
                            {
                                PassBindGroupWriter writer(generated::kCopyDepth_Bindings);
                                writer.setTexture("SourceDepthImage", src);
                                writer.apply(bindGroup);
                            }
                        );
                    }

                    auto& cmdList = context.getCommandList();
                    context.beginPass();
                    setViewportAndScissor(cmdList, data.viewport);
                    cmdList.bindPipeline(storage.pipeline.get());
                    cmdList.setBindGroup(0, rhi::PipelineBindPoint::Graphics, &storage.pipeline->getLayout(), bindGroupCache.getBindGroup(), {});
                    cmdList.draw(3, 1, 0, 0);

                    // 这个 pipeline layout 只有 1 个槽位, group 0 不是"全局数据"那份——用完
                    // 换回去, 否则本帧后面的 Visualization 主绘制/composite pass 会绑到这个残留状态上
                    auto& globalBindings = renderBackend.getGlobalBindings();
                    cmdList.setBindGroup(0, rhi::PipelineBindPoint::Graphics, &globalBindings.getPipelineLayout(), &globalBindings.getGlobalDataBindGroup(), {});
                },
                rg::PassFlags::Skip_Begin_Pass
            );
            return result;
        }

        rhi::Pipeline* createCompositePipeline(RendererContext& rendererContext, ShaderVariant shaderVariant, const rhi::BindGroupLayout& bindGroupLayout, rhi::TextureFormat dstFormat)
        {
            auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr);
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

            GraphicsPipelineState pipelineState = PipelineStatePresets::fullscreenTriangle();
            pipelineState.shader = shaderVariant;
            // colorWriteMask 只留 RGB(0x7, 不含 alpha 那一位)——这个 pass 画在 FinalRT 上,
            // FinalRT 的 alpha 是 ImGui 后续采样这张纹理要用的, 不能被这里"没画到线框的像素
            // alpha=0"这个事实覆盖掉, 只混合颜色、不动目标本来的 alpha。
            // VisualizationColor 里的 rgb 是预乘 alpha 颜色, srcColorBlendFactor 不能再乘一次 alpha
            pipelineState.colorBlendState = ColorBlendState::get<
                rhi::ColorBlendAttachmentState{ true, rhi::BlendFactor::One, rhi::BlendFactor::One_Minus_Src_Alpha, rhi::BlendOp::Add, rhi::BlendFactor::One, rhi::BlendFactor::Zero, rhi::BlendOp::Add, 0x7 }
            >();

            AttachmentFormats* attachmentFormats = nullptr;
            switch (dstFormat)
            {
            case rhi::TextureFormat::BGRA8_UNORM: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::BGRA8_UNORM>(); break;
            case rhi::TextureFormat::RGBA8_UNORM: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA8_UNORM>(); break;
            case rhi::TextureFormat::BGRA8_SRGB: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::BGRA8_SRGB>(); break;
            case rhi::TextureFormat::RGBA8_SRGB: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA8_SRGB>(); break;
            case rhi::TextureFormat::A2BGR10_UNORM_PACK32: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::A2BGR10_UNORM_PACK32>(); break;
            case rhi::TextureFormat::RGBA16_SFLOAT: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA16_SFLOAT>(); break;
            default: assert(false && "Unsupported destination format for Visualization composite pass");
            }
            pipelineState.attachmentFormats = attachmentFormats;

            PipelineCache::GraphicsKey pipelineKey;
            pipelineKey.pipelineState = pipelineState;
            pipelineKey.pipelineLayout = pipelineLayout;
            return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
        }

        // 把 MSAA 颜色纹理手动 resolve(shader 里逐采样点读平均, 见 Composite.slang)并按 alpha
        // 混合贴到最终呈现纹理上——不用 render pass 自带的 resolve attachment, 省掉一张单独的
        // 已解析纹理, 直接在这一步顺带把 resolve 和混合一起做完。
        void addCompositePass(rg::RenderGraph& renderGraph, RendererContext& rendererContext, const SceneProxy& scene, const SceneView& view, rg::TextureHandle msaaColor)
        {
            uint32_t viewId = view.getViewId();
            renderGraph.addGraphicsPass<CompositeData>(
                "VisualizationComposite"_hs,
                [&](rg::RenderGraph::Builder& builder, CompositeData& data)
                {
                    builder.markSideEffect();

                    data.viewId = viewId;
                    data.viewport = view.getViewport();
                    data.viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();
data.viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(viewId);
                    data.msaaColor = builder.sample(msaaColor);

                    auto& blackboard = builder.getBlackboard();
                    rg::TextureHandle finalRT = blackboard.get<rg::TextureHandle>(view.getFinalRTName());
                    data.finalRT = builder.addColorAttachment(finalRT, rhi::AttachmentLoadOp::Load);
                    blackboard.put(view.getFinalRTName(), data.finalRT);
                },
                [&rendererContext](rg::ExecuteContext& context, CompositeData& data)
                {
                    static auto& storage = rendererContext.getPassStorage().getOrCreate<VisualizationPass, CompositeStorage>();
                    auto& renderBackend = context.getRenderBackend();

                    if (!storage.shader)
                    {
                        storage.shader = std::make_shared<PassShader>(
                            renderBackend.getDevice(), "VisualizationComposite",
                            makeVertexFragmentBytecode(
                                renderBackend.getRHIBackend(),
                                GET_PASS_SHADER_BYTECODE(VisualizationComposite, Vertex),
                                GET_PASS_SHADER_BYTECODE(VisualizationComposite, Fragment)
                            )
                        );
                    }
                    if (!storage.bindGroupLayout)
                    {
                        storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kVisualizationComposite_Bindings);
                    }

                    const rhi::TextureView* msaaColor = context.getTexture(data.msaaColor);
                    const rhi::TextureView* finalRT = context.getTexture(data.finalRT);
                    const rhi::TextureFormat dstFormat = finalRT->getTexture().getDesc().format;

                    TRef<rhi::Pipeline>& pipeline = storage.pipelines[static_cast<uint32_t>(dstFormat)];
                    if (!pipeline)
                    {
                        pipeline = createCompositePipeline(rendererContext, storage.shader->getVariant(), *storage.bindGroupLayout, dstFormat);
                    }

                    if (storage.perViewCaches.size() <= data.viewId)
                    {
                        storage.perViewCaches.resize(data.viewId + 1);
                    }
                    auto& bindGroupCache = storage.perViewCaches[data.viewId];
                    if (bindGroupCache.needUpdate(msaaColor))
                    {
                        bindGroupCache.update(
                            renderBackend, *storage.bindGroupLayout,
                            [&](rhi::BindGroup& bindGroup)
                            {
                                PassBindGroupWriter writer(generated::kVisualizationComposite_Bindings);
                                writer.setTexture("VisualizationColor", msaaColor);
                                writer.apply(bindGroup);
                            }
                        );
                    }

                    auto& cmdList = context.getCommandList();
                    setViewportAndScissor(cmdList, data.viewport);
                    cmdList.bindPipeline(pipeline.get());
                    cmdList.setBindGroup(1, rhi::PipelineBindPoint::Graphics, &pipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                    cmdList.setBindGroup(2, rhi::PipelineBindPoint::Graphics, &pipeline->getLayout(), bindGroupCache.getBindGroup(), {});
                    cmdList.draw(3, 1, 0, 0);
                }
            );
        }
    }

    void VisualizationPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const CameraProxy& camera,
        VisualizationItems&& items
    )
    {
        if (items.empty())
            return;

        double3 cameraWorldPos = double3(glm::inverse(camera.getViewMatrix())[3]);
        VisualizationRenderData renderData = VisualizationRenderData::build(renderGraph, rendererContext, items, cameraWorldPos);
        if (renderData.empty())
            return;

        rg::TextureHandle msaaDepth = addDepthExpandPass(renderGraph, rendererContext, view);
        rg::TextureHandle msaaColorResult;

        renderGraph.addGraphicsPass<VisualizationPassData>(
            "Visualization"_hs,
            [&](rg::RenderGraph::Builder& builder, VisualizationPassData& data)
            {
                data.viewId = view.getViewId();
                data.viewport = view.getViewport();
                data.viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();
                data.viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(data.viewId);
                data.renderData = renderData;

                rhi::TextureDesc colorDesc;
                colorDesc.type = rhi::TextureType::Texture_2D;
                colorDesc.format = rhi::TextureFormat::RGBA8_UNORM;
                colorDesc.extent = { static_cast<uint32_t>(data.viewport.z), static_cast<uint32_t>(data.viewport.w), 1 };
                colorDesc.mipCount = 1;
                colorDesc.layerCount = 1;
                colorDesc.samples = kVisualizationMSAASampleCount;
                colorDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto msaaColor = builder.createTexture("VisualizationMSAAColor"_hs(data.viewId), colorDesc);
                data.msaaColor = builder.addColorAttachment(msaaColor, rhi::AttachmentLoadOp::Clear, rhi::ClearColorValue(float4(0.0f, 0.0f, 0.0f, 0.0f)));
                msaaColorResult = data.msaaColor;

                data.msaaDepth = builder.addDepthStencilAttachment(msaaDepth, rhi::AttachmentLoadOp::Load, true, rhi::AttachmentLoadOp::Dont_Care, false);

                builder.accessBuffer(renderData.getBuffer(), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
            },
            [&rendererContext](rg::ExecuteContext& context, VisualizationPassData& data)
            {
                // wireframeGeometry 引用的 GeometryProxy 可能还没被别的路径 prepare() 过(纯碰撞、
                // 没有配套 MeshRenderer 的网格不会被 SceneRenderer.cpp 那条按可见网格走的 prepare
                // 路径碰到), prepare() 内部要插 barrier, 不能在 render pass 内部做, 所以这个 pass
                // 传了 Skip_Begin_Pass, 自己控制 beginPass() 的时机, 必须先 prepare 完再开始 pass
                for (const VisualizationGeometryGroup& group : data.renderData.getGeometryGroups())
                {
                    group.geometry->prepare(context.getCommandList());
                }
                context.beginPass();

                // 这个 pass 从 Editor 自己的 enqueueLambda 里调用, 不在 DeferredPipeline::
                // addToRenderGraph() 那条会先绑好 set 1 的序列里, 不能假设 set 1 已经绑好, 自己
                // 绑一次——每个 shape 各自的 pipeline 在 set 0/1 上都是同一份 layout, 绑一次即可
                // 对后面所有 pipeline 有效。
                context.getCommandList().setBindGroup(1, rhi::PipelineBindPoint::Graphics, &rendererContext.getGlobalPipelineLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });

                static auto& storage = rendererContext.getPassStorage().getOrCreate<VisualizationPass, VisualizationPassStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.iconShader)
                {
                    storage.iconShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "VisualizationIcon",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(VisualizationIcon, Vertex),
                            GET_PASS_SHADER_BYTECODE(VisualizationIcon, Fragment)
                        )
                    );
                    storage.iconBindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kVisualizationIcon_Bindings);
                    storage.pipelines.icon.layout = storage.iconBindGroupLayout.get();
                    storage.pipelines.icon.pipeline = createVisualizationPipeline(
                        rendererContext, storage.iconShader->getVariant(), *storage.iconBindGroupLayout, rhi::PrimitiveTopology::Triangle_Strip, false
                    );
                }

                if (!storage.pointShader)
                {
                    storage.pointShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "VisualizationPoint",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(VisualizationPoint, Vertex),
                            GET_PASS_SHADER_BYTECODE(VisualizationPoint, Fragment)
                        )
                    );
                    storage.pointBindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kVisualizationPoint_Bindings);
                    storage.pipelines.point.layout = storage.pointBindGroupLayout.get();
                    storage.pipelines.point.pipeline = createVisualizationPipeline(
                        rendererContext, storage.pointShader->getVariant(), *storage.pointBindGroupLayout, rhi::PrimitiveTopology::Triangle_Strip, false
                    );
                }

                if (!storage.lineShader)
                {
                    storage.lineShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "VisualizationLine",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(VisualizationLine, Vertex),
                            GET_PASS_SHADER_BYTECODE(VisualizationLine, Fragment)
                        )
                    );
                    storage.lineBindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kVisualizationLine_Bindings);
                    storage.pipelines.line.layout = storage.lineBindGroupLayout.get();
                    storage.pipelines.line.pipeline = createVisualizationPipeline(
                        rendererContext, storage.lineShader->getVariant(), *storage.lineBindGroupLayout, rhi::PrimitiveTopology::Line_List, false
                    );
                }

                if (!storage.wireframeBoxShader)
                {
                    storage.wireframeBoxShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "VisualizationWireframeBox",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeBox, Vertex),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeBox, Fragment)
                        )
                    );
                    storage.wireframeBoxBindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kVisualizationWireframeBox_Bindings);
                    storage.pipelines.wireframeBox.layout = storage.wireframeBoxBindGroupLayout.get();
                    storage.pipelines.wireframeBox.pipeline = createVisualizationPipeline(
                        rendererContext, storage.wireframeBoxShader->getVariant(), *storage.wireframeBoxBindGroupLayout, rhi::PrimitiveTopology::Line_List, true
                    );
                }

                if (!storage.wireframeSphereShader)
                {
                    storage.wireframeSphereShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "VisualizationWireframeSphere",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeSphere, Vertex),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeSphere, Fragment)
                        )
                    );
                    storage.wireframeSphereBindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kVisualizationWireframeSphere_Bindings);
                    storage.pipelines.wireframeSphere.layout = storage.wireframeSphereBindGroupLayout.get();
                    storage.pipelines.wireframeSphere.pipeline = createVisualizationPipeline(
                        rendererContext, storage.wireframeSphereShader->getVariant(), *storage.wireframeSphereBindGroupLayout, rhi::PrimitiveTopology::Line_List, true
                    );
                }

                if (!storage.wireframeCapsuleShader)
                {
                    storage.wireframeCapsuleShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "VisualizationWireframeCapsule",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeCapsule, Vertex),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeCapsule, Fragment)
                        )
                    );
                    storage.wireframeCapsuleBindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kVisualizationWireframeCapsule_Bindings);
                    storage.pipelines.wireframeCapsule.layout = storage.wireframeCapsuleBindGroupLayout.get();
                    storage.pipelines.wireframeCapsule.pipeline = createVisualizationPipeline(
                        rendererContext, storage.wireframeCapsuleShader->getVariant(), *storage.wireframeCapsuleBindGroupLayout, rhi::PrimitiveTopology::Line_List, true
                    );
                }

                if (!storage.wireframeCylinderShader)
                {
                    storage.wireframeCylinderShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "VisualizationWireframeCylinder",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeCylinder, Vertex),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeCylinder, Fragment)
                        )
                    );
                    storage.wireframeCylinderBindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kVisualizationWireframeCylinder_Bindings);
                    storage.pipelines.wireframeCylinder.layout = storage.wireframeCylinderBindGroupLayout.get();
                    storage.pipelines.wireframeCylinder.pipeline = createVisualizationPipeline(
                        rendererContext, storage.wireframeCylinderShader->getVariant(), *storage.wireframeCylinderBindGroupLayout, rhi::PrimitiveTopology::Line_List, true
                    );
                }

                if (!storage.wireframeLightConeShader)
                {
                    storage.wireframeLightConeShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "VisualizationWireframeLightCone",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeLightCone, Vertex),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeLightCone, Fragment)
                        )
                    );
                    storage.wireframeLightConeBindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kVisualizationWireframeLightCone_Bindings);
                    storage.pipelines.wireframeLightCone.layout = storage.wireframeLightConeBindGroupLayout.get();
                    storage.pipelines.wireframeLightCone.pipeline = createVisualizationPipeline(
                        rendererContext, storage.wireframeLightConeShader->getVariant(), *storage.wireframeLightConeBindGroupLayout, rhi::PrimitiveTopology::Line_List, true
                    );
                }

                if (!storage.wireframeGeometryShader)
                {
                    storage.wireframeGeometryShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(), "VisualizationWireframeGeometry",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeGeometry, Vertex),
                            GET_PASS_SHADER_BYTECODE(VisualizationWireframeGeometry, Fragment)
                        )
                    );
                    storage.wireframeGeometryBindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kVisualizationWireframeGeometry_Bindings);
                    storage.pipelines.wireframeGeometry.layout = storage.wireframeGeometryBindGroupLayout.get();
                    // vertex-pulling, 不绑定顶点/索引 buffer(第二个 storage buffer 是手动按下标读的 GeometryProxy
                    // 数据, 不是固定管线的顶点抓取), hasVertexInput 传 false
                    storage.pipelines.wireframeGeometry.pipeline = createVisualizationPipeline(
                        rendererContext, storage.wireframeGeometryShader->getVariant(), *storage.wireframeGeometryBindGroupLayout, rhi::PrimitiveTopology::Line_List, false
                    );
                }

                setViewportAndScissor(context.getCommandList(), data.viewport);
                data.renderData.draw(context, rendererContext, storage.bindGroupCaches, storage.pipelines);
            },
            rg::PassFlags::Skip_Begin_Pass
        );

        addCompositePass(renderGraph, rendererContext, scene, view, msaaColorResult);
    }
}
