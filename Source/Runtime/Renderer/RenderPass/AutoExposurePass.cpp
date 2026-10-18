#include "AutoExposurePass.h"
#include "RenderPassUtils.h"

#include <HistogramAtomic.generated.h>
#include <HistogramConvert.generated.h>
#include <AutoExposure.generated.h>

#include <algorithm>
#include <cmath>

namespace hyp
{
    namespace
    {
        static constexpr uint32_t kHistogramBucketCount = 64;
        static constexpr float kLensAttenuation = 0.78f;
        static constexpr float kISOSaturationSpeedConstant = 0.78f;
        static constexpr float kMiddleGrey = 0.18f;
        static constexpr float kExponentialTransitionDistance = 1.5f;
        static constexpr float kFrameTimeEpsilon = 1.0f / 60.0f;

        struct HistogramAtomicConstants
        {
            uint2 resolution;
            float histogramScale;
            float histogramBias;
            float minLuminance;
            float oneOverPreExposure;
            uint32_t padding0;
            uint32_t padding1;
        };
        static_assert(sizeof(HistogramAtomicConstants) == 32);

        struct HistogramConvertConstants
        {
            uint2 resolution;
            uint32_t padding0;
            uint32_t padding1;
        };
        static_assert(sizeof(HistogramConvertConstants) == 16);

        struct AutoExposureConstants
        {
            float histogramScale;
            float histogramBias;
            float exposureLowPercent;
            float exposureHighPercent;

            float minAverageLuminance;
            float maxAverageLuminance;
            float exposureSpeedUp;
            float exposureSpeedDown;

            float exponentialUpM;
            float exponentialDownM;
            float startDistance;
            float preExposure;

            float exposureCompensationScale;
            float manualExposureScale;
            uint32_t exposureMode;
            uint32_t forceTarget;
        };
        static_assert(sizeof(AutoExposureConstants) == 64);

        struct DerivedExposureParameters
        {
            HistogramAtomicConstants histogramAtomic;
            HistogramConvertConstants histogramConvert;
            AutoExposureConstants autoExposure;
        };

        float computeExponentialSlope(float speed)
        {
            float startTime = kExponentialTransitionDistance / speed;
            return kFrameTimeEpsilon /
                ((1.0f - std::exp2(-kFrameTimeEpsilon * speed)) * startTime);
        }

        DerivedExposureParameters deriveExposureParameters(
            const ExposureSettings& settings,
            uint2 resolution,
            float preExposure,
            bool historyValid)
        {
            const float luminanceMax = kISOSaturationSpeedConstant /
                std::max(kLensAttenuation, 0.01f);

            float histogramMinEV100 = settings.histogramMinEV100;
            float histogramMaxEV100 = std::max(
                settings.histogramMaxEV100,
                histogramMinEV100 + 1.0f);
            float histogramLogMin = std::log2(luminanceMax) + histogramMinEV100;
            float histogramLogMax = std::log2(luminanceMax) + histogramMaxEV100;
            float histogramScale = 1.0f / (histogramLogMax - histogramLogMin);
            float histogramBias = -histogramLogMin * histogramScale;

            float minEV100 = std::min(settings.minEV100, settings.maxEV100);
            float maxEV100 = std::max(settings.minEV100, settings.maxEV100);
            float minWhitePointLuminance = luminanceMax * std::exp2(minEV100);
            float maxWhitePointLuminance = luminanceMax * std::exp2(maxEV100);

            float speedUp = std::max(settings.adaptationSpeedUp, 0.001f);
            float speedDown = std::max(settings.adaptationSpeedDown, 0.001f);
            float lowPercent = std::clamp(settings.histogramLowPercent, 1.0f, 99.0f) * 0.01f;
            float highPercent = std::max(
                lowPercent,
                std::clamp(settings.histogramHighPercent, 1.0f, 99.0f) * 0.01f);
            float exposureCompensationScale = std::exp2(settings.exposureCompensation);

            float aperture = std::max(settings.aperture, 0.001f);
            float shutterTime = std::max(settings.shutterSpeed, 0.000001f);
            float iso = std::max(settings.iso, 1.0f);
            float manualEV100 = std::log2(
                aperture * aperture / shutterTime * 100.0f / iso);
            float manualExposureScale = exposureCompensationScale /
                (luminanceMax * std::exp2(manualEV100));

            DerivedExposureParameters result{};
            result.histogramAtomic = {
                resolution,
                histogramScale,
                histogramBias,
                std::exp2(histogramLogMin),
                1.0f / std::max(preExposure, 0.0001f),
                0u,
                0u
            };
            result.histogramConvert = { resolution, 0u, 0u };
            result.autoExposure = {
                histogramScale,
                histogramBias,
                lowPercent,
                highPercent,
                minWhitePointLuminance * kMiddleGrey,
                maxWhitePointLuminance * kMiddleGrey,
                speedUp,
                speedDown,
                computeExponentialSlope(speedUp),
                computeExponentialSlope(speedDown),
                kExponentialTransitionDistance,
                preExposure,
                exposureCompensationScale,
                manualExposureScale,
                static_cast<uint32_t>(settings.mode),
                (!historyValid || settings.mode == ExposureMode::Manual) ? 1u : 0u
            };
            return result;
        }

        rhi::Pipeline* createStandaloneComputePipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout)
        {
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
            PipelineCache::PipelineLayoutKey layoutKey{};
            layoutKey.bindGroupLayouts[0] = &bindGroupLayout;
            rhi::PipelineLayout* pipelineLayout = pipelineCache.getOrCreateLayout(layoutKey);

            PipelineCache::ComputeKey pipelineKey;
            pipelineKey.shader = shaderVariant;
            pipelineKey.pipelineLayout = pipelineLayout;
            return pipelineCache.getOrCreateComputePipeline(pipelineKey);
        }

        void rebindComputeGlobalBindGroups(
            rg::ExecuteContext& context,
            RendererContext& rendererContext,
            uint32_t viewId)
        {
            auto& cmdList = context.getCommandList();
            auto& globalBindings = context.getRenderBackend().getGlobalBindings();
            cmdList.setBindGroup(
                0,
                rhi::PipelineBindPoint::Compute,
                &globalBindings.getPipelineLayout(),
                &globalBindings.getGlobalDataBindGroup(),
                {});

            uint32_t dynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(viewId);
            cmdList.setBindGroup(
                1,
                rhi::PipelineBindPoint::Compute,
                &rendererContext.getGlobalPipelineLayout(),
                &rendererContext.getViewDataManager().getBindGroup(),
                { &dynamicOffset, 1 });
        }

        struct HistogramAtomicData
        {
            uint32_t viewId;
            HistogramAtomicConstants constants;
            rg::TextureHandle sceneColor;
            rg::BufferHandle histogramScatter;
        };

        struct HistogramConvertData
        {
            uint32_t viewId;
            HistogramConvertConstants constants;
            rg::BufferHandle histogramScatter;
            rg::BufferHandle histogram;
        };

        struct AutoExposureData
        {
            uint32_t viewId;
            AutoExposureConstants constants;
            rg::BufferHandle histogram;
            rg::BufferHandle autoExposure;
        };

        struct AutoExposureReadbackData
        {
            rg::BufferHandle autoExposure;
            rg::BufferHandle readback;
            TRef<rhi::Buffer> readbackBuffer;
            std::shared_ptr<float> readbackValue;
        };

        struct AutoExposurePassStorage
        {
            std::shared_ptr<PassShader> histogramAtomicShader;
            std::shared_ptr<PassShader> histogramConvertShader;
            std::shared_ptr<PassShader> autoExposureShader;

            TRef<rhi::BindGroupLayout> histogramAtomicLayout;
            TRef<rhi::BindGroupLayout> histogramConvertLayout;
            TRef<rhi::BindGroupLayout> autoExposureLayout;

            TRef<rhi::Pipeline> histogramAtomicPipeline;
            TRef<rhi::Pipeline> histogramConvertPipeline;
            TRef<rhi::Pipeline> autoExposurePipeline;

            std::vector<BindGroupCache<2>> histogramAtomicCaches;
            std::vector<BindGroupCache<2>> histogramConvertCaches;
            std::vector<BindGroupCache<2>> autoExposureCaches;
        };
    }

    void AutoExposurePass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        PostProcessState& postProcess)
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 resolution(
            static_cast<uint32_t>(viewport.z),
            static_cast<uint32_t>(viewport.w));
        const uint2 halfResolution(
            (resolution.x + 1u) / 2u,
            (resolution.y + 1u) / 2u);
        const DerivedExposureParameters parameters = deriveExposureParameters(
            postProcess.getPostProcessSettings().exposure,
            halfResolution,
            view.getPreExposure(),
            postProcess.isAutoExposureHistoryValid());

        rg::BufferHandle autoExposure = renderGraph.importBuffer(
            "AutoExposureData"_hs(viewId),
            &postProcess.getAutoExposureDataBuffer());

        // "SceneColorHalfRes" 由 RenderPipeline.cpp 里独立调用的 DownsamplePass 产出并存进
        // blackboard——这里直接取用, 不知道、也不需要知道 DownsamplePass 这个类的存在
        auto& histogramAtomicPass = renderGraph.addComputePass<HistogramAtomicData>(
            "HistogramAtomic"_hs(viewId),
            [&](rg::RenderGraph::Builder& builder, HistogramAtomicData& data)
            {
                data.viewId = viewId;
                data.constants = parameters.histogramAtomic;
                auto sceneColorHalfRes = builder.getBlackboard().get<rg::TextureHandle>(
                    "SceneColorHalfRes"_hs(viewId));
                data.sceneColor = builder.sample(sceneColorHalfRes);

                auto histogramScatter = builder.createBuffer(
                    "HistogramScatter"_hs(viewId),
                    rhi::BufferDesc{
                        sizeof(uint32_t) * kHistogramBucketCount * 2u,
                        rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst
                    });
                data.histogramScatter = builder.accessBuffer(
                    histogramScatter,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Write_Only);
            },
            [&rendererContext](rg::ExecuteContext& context, HistogramAtomicData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<
                    AutoExposurePass, AutoExposurePassStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.histogramAtomicShader)
                {
                    storage.histogramAtomicShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "HistogramAtomic",
                        makeComputeBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(HistogramAtomic, Compute)));
                    storage.histogramAtomicLayout = createPassBindGroupLayout(
                        renderBackend,
                        generated::kHistogramAtomic_Bindings);
                    storage.histogramAtomicPipeline = createStandaloneComputePipeline(
                        rendererContext,
                        storage.histogramAtomicShader->getVariant(),
                        *storage.histogramAtomicLayout);
                }

                if (storage.histogramAtomicCaches.size() <= data.viewId)
                    storage.histogramAtomicCaches.resize(data.viewId + 1u);
                auto& cache = storage.histogramAtomicCaches[data.viewId];

                rhi::TextureView* sceneColor = context.getTexture(data.sceneColor);
                rhi::Buffer* histogramScatter = context.getBuffer(data.histogramScatter);
                if (cache.needUpdate(sceneColor, histogramScatter))
                {
                    cache.update(renderBackend, *storage.histogramAtomicLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kHistogramAtomic_Bindings);
                        writer.setTexture("SceneColor", sceneColor);
                        writer.setBuffer("HistogramScatter", histogramScatter);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.clearBuffer(histogramScatter, 0, rhi::kWholeBuffer);
                rhi::BufferBarrier clearBarrier;
                clearBarrier.buffer = histogramScatter;
                clearBarrier.oldState = rhi::ResourceState::Transfer_Dst;
                clearBarrier.newState = rhi::ResourceState::Compute_Shader_Storage_Write;
                clearBarrier.offset = 0;
                clearBarrier.size = rhi::kWholeBuffer;
                cmdList.insertBarriers({ &clearBarrier, 1 });
                context.beginPass();

                cmdList.bindPipeline(storage.histogramAtomicPipeline.get());
                cmdList.setBindGroup(
                    0,
                    rhi::PipelineBindPoint::Compute,
                    &storage.histogramAtomicPipeline->getLayout(),
                    cache.getBindGroup(),
                    {});
                cmdList.setImmediateData(
                    &storage.histogramAtomicPipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(&data.constants),
                        sizeof(data.constants)));
                cmdList.dispatch(
                    (data.constants.resolution.x + 15u) / 16u,
                    (data.constants.resolution.y + 15u) / 16u,
                    1u);
            },
            rg::PassFlags::Skip_Begin_Pass);

        auto& histogramConvertPass = renderGraph.addComputePass<HistogramConvertData>(
            "HistogramConvert"_hs(viewId),
            [&](rg::RenderGraph::Builder& builder, HistogramConvertData& data)
            {
                data.viewId = viewId;
                data.constants = parameters.histogramConvert;
                data.histogramScatter = builder.accessBuffer(
                    histogramAtomicPass.getData().histogramScatter,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Read_Only);
                auto histogram = builder.createBuffer(
                    "Histogram"_hs(viewId),
                    rhi::BufferDesc{
                        sizeof(float) * kHistogramBucketCount,
                        rhi::BufferUsage::Storage
                    });
                data.histogram = builder.accessBuffer(
                    histogram,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Write_Only);
            },
            [&rendererContext, &scene](rg::ExecuteContext& context, HistogramConvertData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<
                    AutoExposurePass, AutoExposurePassStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.histogramConvertShader)
                {
                    storage.histogramConvertShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "HistogramConvert",
                        makeComputeBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(HistogramConvert, Compute)));
                    storage.histogramConvertLayout = createPassBindGroupLayout(
                        renderBackend,
                        generated::kHistogramConvert_Bindings);
                    storage.histogramConvertPipeline = createStandaloneComputePipeline(
                        rendererContext,
                        storage.histogramConvertShader->getVariant(),
                        *storage.histogramConvertLayout);
                }

                if (storage.histogramConvertCaches.size() <= data.viewId)
                    storage.histogramConvertCaches.resize(data.viewId + 1u);
                auto& cache = storage.histogramConvertCaches[data.viewId];
                rhi::Buffer* histogramScatter = context.getBuffer(data.histogramScatter);
                rhi::Buffer* histogram = context.getBuffer(data.histogram);
                if (cache.needUpdate(histogramScatter, histogram))
                {
                    cache.update(renderBackend, *storage.histogramConvertLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kHistogramConvert_Bindings);
                        writer.setBuffer("HistogramScatter", histogramScatter);
                        writer.setBuffer("Histogram", histogram);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.histogramConvertPipeline.get());
                cmdList.setBindGroup(
                    0,
                    rhi::PipelineBindPoint::Compute,
                    &storage.histogramConvertPipeline->getLayout(),
                    cache.getBindGroup(),
                    {});
                cmdList.setImmediateData(
                    &storage.histogramConvertPipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(&data.constants),
                        sizeof(data.constants)));
                cmdList.dispatch(1u, 1u, 1u);
                rebindComputeGlobalBindGroups(context, rendererContext, data.viewId);
            });

        auto& autoExposurePass = renderGraph.addComputePass<AutoExposureData>(
            "AutoExposure"_hs(viewId),
            [&](rg::RenderGraph::Builder& builder, AutoExposureData& data)
            {
                data.viewId = viewId;
                data.constants = parameters.autoExposure;
                data.histogram = builder.accessBuffer(
                    histogramConvertPass.getData().histogram,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Read_Only);
                data.autoExposure = builder.accessBuffer(
                    autoExposure,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Read_Write);
                builder.getBlackboard().put("AutoExposureData"_hs(viewId), data.autoExposure);
            },
            [&rendererContext, &postProcess](rg::ExecuteContext& context, AutoExposureData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<
                    AutoExposurePass, AutoExposurePassStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.autoExposureShader)
                {
                    storage.autoExposureShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "AutoExposure",
                        makeComputeBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(AutoExposure, Compute)));
                    storage.autoExposureLayout = createPassBindGroupLayout(
                        renderBackend,
                        generated::kAutoExposure_Bindings);
                    storage.autoExposurePipeline = createComputePipeline(
                        rendererContext,
                        storage.autoExposureShader->getVariant(),
                        *storage.autoExposureLayout);
                }

                if (storage.autoExposureCaches.size() <= data.viewId)
                    storage.autoExposureCaches.resize(data.viewId + 1u);
                auto& cache = storage.autoExposureCaches[data.viewId];
                rhi::Buffer* histogram = context.getBuffer(data.histogram);
                rhi::Buffer* autoExposureBuffer = context.getBuffer(data.autoExposure);
                if (cache.needUpdate(histogram, autoExposureBuffer))
                {
                    cache.update(renderBackend, *storage.autoExposureLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kAutoExposure_Bindings);
                        writer.setBuffer("Histogram", histogram);
                        writer.setBuffer("AutoExposureData", autoExposureBuffer);
                        writer.apply(bindGroup);
                    });
                }

                // Imported buffers do not currently carry a render-graph default state. The
                // previous frame ends with ColorGrading reading this buffer in the graphics stage,
                // so establish the cross-frame WAR dependency before beginning the compute pass.
                rhi::BufferBarrier historyBarrier;
                historyBarrier.buffer = autoExposureBuffer;
                historyBarrier.oldState = rhi::ResourceState::Shader_Storage_Read;
                historyBarrier.newState = rhi::ResourceState::Compute_Shader_Storage;
                historyBarrier.offset = 0;
                historyBarrier.size = rhi::kWholeBuffer;
                context.getCommandList().insertBarriers({ &historyBarrier, 1 });
                context.beginPass();

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.autoExposurePipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Compute,
                    &storage.autoExposurePipeline->getLayout(),
                    cache.getBindGroup(),
                    {});
                cmdList.setImmediateData(
                    &storage.autoExposurePipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(&data.constants),
                        sizeof(data.constants)));
                cmdList.dispatch(1u, 1u, 1u);
                postProcess.markAutoExposureHistoryValid();
            },
            rg::PassFlags::Skip_Begin_Pass);

        TRef<rhi::Buffer> readbackBuffer = postProcess.acquireAutoExposureReadbackBuffer();

        renderGraph.addTransferPass<AutoExposureReadbackData>(
            "AutoExposureReadback"_hs(viewId),
            [&](rg::RenderGraph::Builder& builder, AutoExposureReadbackData& data)
            {
                builder.markSideEffect();
                data.autoExposure = builder.accessBuffer(
                    autoExposurePass.getData().autoExposure,
                    rhi::BufferUsage::Transfer_Src,
                    rg::AccessType::Read_Only);
                auto readback = builder.importBuffer(
                    "AutoExposureReadbackBuffer"_hs(viewId),
                    readbackBuffer.get());
                data.readback = builder.accessBuffer(
                    readback,
                    rhi::BufferUsage::Transfer_Dst,
                    rg::AccessType::Write_Only);
                data.readbackBuffer = readbackBuffer;
                data.readbackValue = postProcess.getAutoExposureReadbackValue();
            },
            [&rendererContext](rg::ExecuteContext& context, AutoExposureReadbackData& data)
            {
                rhi::BufferCopyRegion copyRegion;
                copyRegion.srcOffset = 0;
                copyRegion.dstOffset = 0;
                copyRegion.size = sizeof(float4);
                context.getCommandList().copyBuffer(
                    context.getBuffer(data.autoExposure),
                    context.getBuffer(data.readback),
                    { &copyRegion, 1 });

                rendererContext.getRenderBackend().getDeferredTaskQueue().push(
                    [readback = std::move(data.readbackBuffer),
                     readbackValue = std::move(data.readbackValue)]() mutable
                    {
                        const auto* exposureData = static_cast<const float4*>(readback->map());
                        if (exposureData && std::isfinite(exposureData->x) && exposureData->x > 0.0f)
                            *readbackValue = exposureData->x;
                        if (exposureData)
                            readback->unmap();
                    },
                    kMaxFramesInFlight
                );
            }
        );
    }
}
