#include "ShadowMaskPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>

#include <ShadowMask.generated.h>

namespace hyp
{
    // 和 Shader/Material/ShadowMask.slang 里的 ShadowMaskParameters 字段顺序/宽度必须严格对应,
    // 两边各自定义、不共享头文件(和这个 pass 里其它资源绑定的做法一致)
    static constexpr uint32_t kMaxCascadeCount = DirectionalLightProxy::MaxCascadeCount;
    static constexpr uint32_t kCascadeVectorCount = (kMaxCascadeCount + 3u) / 4u;

    // UE-compatible raw CSM receiver bias. The uploaded shader value is 1 - bias, so grazing
    // receivers get a wider soft comparison interval without exposing another per-light property.
    static constexpr float kShadowReceiverBias = 0.9f;

    struct ShadowMaskParameters
    {
        float4x4 viewToLightClip[kMaxCascadeCount];
        float4 renderSplitFar[kCascadeVectorCount];
        float4 depthBias[kCascadeVectorCount];
        // 非末级在 [fadeStart, fadeEnd] 混合到下一张有效的 cascade；末级在同一区间淡到全亮。
        float4 fadeStart[kCascadeVectorCount];
        float4 fadeEnd[kCascadeVectorCount];
        float3 lightDirectionWS;
        float receiverBias;
        uint32_t cascadeCount;
        uint32_t cascadeResolution;
        uint32_t shadowFilterQuality;
        uint32_t padding0;
    };
    static_assert(sizeof(ShadowMaskParameters) == 672);

    struct ShadowMaskData
    {
        float4 viewport;
        const DirectionalLightProxy* directionalLight;
        const CameraProxy* camera;
        rg::TextureHandle sceneDepthD;
        rg::TextureHandle shadowDepth;
        rg::TextureHandle gbufferA;
        rg::TextureHandle shadowMask;
    };

    // 按方向光的 RenderProxy 索引分开缓存: 每盏光自己的级联矩阵/分裂距离都不同, 不能共用一份
    // uniform buffer/bind group——和 DirectLightingPass 等 pass 按 viewId 分 perViewCaches 是
    // 同一个道理, 只是这里按光源索引分
    struct ShadowMaskLightStorage
    {
        TRef<rhi::Buffer> parametersBuffer;
        BindGroupCache<5> bindGroupCache;
    };

    struct ShadowMaskStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<ShadowMaskLightStorage> perLightCaches;
    };

    void ShadowMaskPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const CameraProxy& camera,
        const DirectionalLightProxy& directionalLight
    )
    {
        // 名字非空只说明这个相机这一帧算过 getShadowSceneViewGroups(), 不代表阴影深度 atlas
        // 真的被写过——CascadeShadowPipeline::addToRenderGraph 在 shadowDepthBucket 全空时
        // (比如场景里没有投影物体)会提前 return, 压根不会调用 CascadeShadowDepthPass, 也就不会
        // blackboard.put 这张纹理。这里必须直接查 blackboard 里的纹理句柄是否有效, 而不是只看
        // 名字是否为空, 否则会对着一个从没写过的资源 builder.sample()
        HString shadowDepthName = directionalLight.getShadowDepthName(&camera);
        if (shadowDepthName.empty())
            return;
        rg::TextureHandle shadowDepth = renderGraph.getBlackboard().get<rg::TextureHandle>(shadowDepthName);
        if (!shadowDepth.isValid())
            return;

        renderGraph.addGraphicsPass<ShadowMaskData>(
            "ShadowMask"_hs,
            [&](rg::RenderGraph::Builder& builder, ShadowMaskData& data)
            {
                builder.markSideEffect();

                data.viewport = view.getViewport();
                data.directionalLight = &directionalLight;
                data.camera = &camera;

                auto& blackboard = builder.getBlackboard();

                rg::TextureHandle sceneDepthD = blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(view.getViewId()));
                data.sceneDepthD = builder.sample(sceneDepthD);
                data.shadowDepth = builder.sample(shadowDepth);

                rg::TextureHandle gbufferA = blackboard.get<rg::TextureHandle>("GBufferA"_hs(view.getViewId()));
                data.gbufferA = builder.sample(gbufferA);

                HString shadowMaskName = directionalLight.getShadowMaskName(&camera);
                uint32_t width = static_cast<uint32_t>(data.viewport.z);
                uint32_t height = static_cast<uint32_t>(data.viewport.w);
                rhi::TextureDesc shadowMaskDesc;
                shadowMaskDesc.type = rhi::TextureType::Texture_2D;
                shadowMaskDesc.format = rhi::TextureFormat::R8_UNORM;
                shadowMaskDesc.extent = { width, height, 1 };
                shadowMaskDesc.mipCount = 1;
                shadowMaskDesc.layerCount = 1;
                shadowMaskDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto shadowMask = builder.createTexture(shadowMaskName, shadowMaskDesc);
                data.shadowMask = builder.addColorAttachment(shadowMask, rhi::AttachmentLoadOp::Dont_Care);
                blackboard.put(shadowMaskName, data.shadowMask);
            },
            [&rendererContext](rg::ExecuteContext& context, ShadowMaskData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<ShadowMaskPass, ShadowMaskStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ShadowMask",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(ShadowMask, Vertex),
                            GET_PASS_SHADER_BYTECODE(ShadowMask, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kShadowMask_Bindings);
                }

                uint32_t lightIndex = data.directionalLight->getIndex();
                if (storage.perLightCaches.size() <= lightIndex)
                {
                    storage.perLightCaches.resize(lightIndex + 1);
                }
                auto& lightStorage = storage.perLightCaches[lightIndex];

                if (!lightStorage.parametersBuffer)
                {
                    rhi::BufferDesc bufferDesc;
                    bufferDesc.size = sizeof(ShadowMaskParameters);
                    bufferDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::Transfer_Dst;
                    lightStorage.parametersBuffer = renderBackend.getDevice().createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
                }

                ShadowMaskParameters parameters{};
                std::span<const CascadeShadowData> cascades = data.directionalLight->getCascadeShadowData(data.camera);
                parameters.cascadeCount = static_cast<uint32_t>(std::min<size_t>(cascades.size(), kMaxCascadeCount));
                parameters.cascadeResolution = data.directionalLight->getCascadeResolution();
                parameters.lightDirectionWS = data.directionalLight->getDirection();
                parameters.receiverBias = 1.0f - kShadowReceiverBias;
                parameters.shadowFilterQuality = static_cast<uint32_t>(data.directionalLight->getShadowFilterQuality());
                for (uint32_t i = 0; i < parameters.cascadeCount; ++i)
                {
                    uint32_t vectorIndex = i / 4u;
                    uint32_t componentIndex = i % 4u;
                    parameters.viewToLightClip[i] = cascades[i].viewToLightClip;
                    parameters.renderSplitFar[vectorIndex][componentIndex] = cascades[i].renderSplitFar;
                    parameters.depthBias[vectorIndex][componentIndex] = cascades[i].depthBias;
                    parameters.fadeStart[vectorIndex][componentIndex] = cascades[i].fadeStart;
                    parameters.fadeEnd[vectorIndex][componentIndex] = cascades[i].fadeEnd;
                }

                auto& cmdList = context.getCommandList();

                // Uniform buffer 上传需要在 render pass 之外做(barrier/copyBuffer 不能出现在
                // active render pass 里), 所以这个 pass 用 Skip_Begin_Pass, 自己在这里手动控制
                // beginPass() 的时机——和 CascadeShadowDepthPass 上传 instance 索引 buffer 是
                // 同一个理由、同一套 forceAllocate->memcpy->barrier->copyBuffer->barrier 写法
                auto& stagingAllocator = renderBackend.getStagingAllocator();
                auto allocation = stagingAllocator.forceAllocate(sizeof(ShadowMaskParameters));
                std::memcpy(allocation.mappedData, &parameters, sizeof(ShadowMaskParameters));

                rhi::BufferBarrier preBarrier;
                preBarrier.buffer = lightStorage.parametersBuffer.get();
                preBarrier.offset = 0;
                preBarrier.size = sizeof(ShadowMaskParameters);
                preBarrier.oldState = rhi::ResourceState::Shader_Uniform;
                preBarrier.newState = rhi::ResourceState::Transfer_Dst;
                cmdList.insertBarriers({ &preBarrier, 1 });

                rhi::BufferCopyRegion copyRegion;
                copyRegion.srcOffset = allocation.offset;
                copyRegion.dstOffset = 0;
                copyRegion.size = sizeof(ShadowMaskParameters);
                cmdList.copyBuffer(allocation.buffer, lightStorage.parametersBuffer.get(), { &copyRegion, 1 });

                rhi::BufferBarrier postBarrier;
                postBarrier.buffer = lightStorage.parametersBuffer.get();
                postBarrier.offset = 0;
                postBarrier.size = sizeof(ShadowMaskParameters);
                postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
                postBarrier.newState = rhi::ResourceState::Shader_Uniform;
                cmdList.insertBarriers({ &postBarrier, 1 });

                const rhi::TextureView* sceneDepthD = context.getTexture(data.sceneDepthD);
                const rhi::TextureView* shadowDepth = context.getTexture(data.shadowDepth);
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                rhi::Sampler* shadowPointClampSampler = renderBackend.getSamplerCache().getPointClampNoMip();

                if (lightStorage.bindGroupCache.needUpdate(
                    sceneDepthD,
                    shadowDepth,
                    gbufferA,
                    lightStorage.parametersBuffer.get(),
                    shadowPointClampSampler
                ))
                {
                    lightStorage.bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kShadowMask_Bindings);
                            writer.setTexture("SceneDepthImage", sceneDepthD);
                            writer.setTexture("ShadowDepthImage", shadowDepth);
                            writer.setTexture("GbufferAImage", gbufferA);
                            writer.setBuffer("Parameters", lightStorage.parametersBuffer.get(), 0, sizeof(ShadowMaskParameters));
                            writer.setSampler("ShadowPointClampSampler", shadowPointClampSampler);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout
                    );
                }

                context.beginPass();
                setViewportAndScissor(cmdList, data.viewport);
                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    lightStorage.bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.draw(3, 1, 0, 0);
            },
            rg::PassFlags::Skip_Begin_Pass
        );
    }

    rhi::Pipeline* ShadowMaskPass::createPipeline(
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
            rhi::TextureFormat::R8_UNORM
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
