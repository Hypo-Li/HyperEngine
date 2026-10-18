#include "PostProcessState.h"

#include <RenderBase/RenderBackend.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hyp
{
    static constexpr uint32_t kColorGradingLutSize = 32;

    PostProcessState::PostProcessState(RenderBackend& renderBackend, uint32_t viewId, const PostProcessSettings& settings) :
        mRenderBackend(renderBackend),
        mViewId(viewId)
    {
        // 不能只在成员初始化列表里赋值 mSettings——mJitterEnabled 是从
        // PostProcessSettings::antiAliasing 派生出来的独立字段, 只有 setPostProcessSettings()
        // 这一处会同步它(见该函数注释), 构造函数如果绕过这个函数直接赋值,
        // 传进来的初始 AntiAliasingSettings 就永远不会真正生效
        setPostProcessSettings(settings);

        auto& device = mRenderBackend.getDevice();

        rhi::TextureDesc lutDesc;
        lutDesc.type = rhi::TextureType::Texture_3D;
        lutDesc.format = rhi::TextureFormat::RGBA8_UNORM;
        lutDesc.extent = { kColorGradingLutSize, kColorGradingLutSize, kColorGradingLutSize };
        lutDesc.mipCount = 1;
        lutDesc.layerCount = 1;
        lutDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
        mColorGradingLut = device.createTexture(lutDesc, rhi::MemoryType::Gpu_Only);
        mColorGradingLutView = mColorGradingLut->createView(rhi::makeTextureDefaultViewDesc(lutDesc));

        rhi::BufferDesc exposureDesc;
        exposureDesc.size = sizeof(float4);
        exposureDesc.usage = rhi::BufferUsage::Storage |
            rhi::BufferUsage::Transfer_Src |
            rhi::BufferUsage::Transfer_Dst;
        mAutoExposureDataBuffer = device.createBuffer(exposureDesc, rhi::MemoryType::Gpu_Only);
        mAutoExposureDataBuffer->setDebugName("AutoExposureData"_hs(mViewId));

        rhi::BufferDesc readbackDesc;
        readbackDesc.size = sizeof(float4);
        readbackDesc.usage = rhi::BufferUsage::Transfer_Dst;
        for (uint32_t i = 0; i < mAutoExposureReadbackBuffers.size(); ++i)
        {
            auto& readbackBuffer = mAutoExposureReadbackBuffers[i];
            readbackBuffer = device.createBuffer(
                readbackDesc,
                rhi::MemoryType::Gpu_To_Cpu);
            // ring 里三个 buffer 分别带上环内下标——同名的话, 校验层/图形调试器报错时没法区分
            // 具体是哪一个物理 buffer。
            readbackBuffer->setDebugName("AutoExposureReadback"_hs(mViewId)(i));
        }
    }

    PostProcessState::~PostProcessState() noexcept
    {
        mRenderBackend.getDeferredTaskQueue().push(
            [lut = std::move(mColorGradingLut), lutView = std::move(mColorGradingLutView),
             autoExposure = std::move(mAutoExposureDataBuffer),
             autoExposureReadbacks = std::move(mAutoExposureReadbackBuffers),
             taaHistory0 = std::move(mTAAHistory[0]), taaHistory1 = std::move(mTAAHistory[1]),
             aoHistory0 = std::move(mAOHistory[0]), aoHistory1 = std::move(mAOHistory[1]),
             sceneDepthHistory = std::move(mSceneDepthHistory),
             diffuseIndirectHistory0 = std::move(mDiffuseIndirectHistory[0]),
             diffuseIndirectHistory1 = std::move(mDiffuseIndirectHistory[1]),
             shortRangeAOHistory0 = std::move(mShortRangeAOHistory[0]),
             shortRangeAOHistory1 = std::move(mShortRangeAOHistory[1]),
             screenProbeLightingHistoryRadiance = std::move(mScreenProbeLightingHistoryRadiance),
             screenProbeLightingHistorySceneDepth = std::move(mScreenProbeLightingHistorySceneDepth),
             reflectionSpecularHistory0 = std::move(mReflectionSpecularHistory[0]),
             reflectionSpecularHistory1 = std::move(mReflectionSpecularHistory[1]),
             reflectionNumFramesHistory0 = std::move(mReflectionNumFramesHistory[0]),
             reflectionNumFramesHistory1 = std::move(mReflectionNumFramesHistory[1]),
             roughSpecularIndirectHistory0 = std::move(mRoughSpecularIndirectHistory[0]),
             roughSpecularIndirectHistory1 = std::move(mRoughSpecularIndirectHistory[1])]() mutable
            {
                lutView = nullptr;
                lut = nullptr;
                autoExposure = nullptr;
                for (auto& readback : autoExposureReadbacks)
                    readback = nullptr;
                taaHistory0.view = nullptr;
                taaHistory0.texture = nullptr;
                taaHistory1.view = nullptr;
                taaHistory1.texture = nullptr;
                aoHistory0.view = nullptr;
                aoHistory0.texture = nullptr;
                aoHistory1.view = nullptr;
                aoHistory1.texture = nullptr;
                sceneDepthHistory.view = nullptr;
                sceneDepthHistory.texture = nullptr;
                diffuseIndirectHistory0.view = nullptr;
                diffuseIndirectHistory0.texture = nullptr;
                diffuseIndirectHistory1.view = nullptr;
                diffuseIndirectHistory1.texture = nullptr;
                shortRangeAOHistory0.view = nullptr;
                shortRangeAOHistory0.texture = nullptr;
                shortRangeAOHistory1.view = nullptr;
                shortRangeAOHistory1.texture = nullptr;
                screenProbeLightingHistoryRadiance.view = nullptr;
                screenProbeLightingHistoryRadiance.texture = nullptr;
                screenProbeLightingHistorySceneDepth.view = nullptr;
                screenProbeLightingHistorySceneDepth.texture = nullptr;
                reflectionSpecularHistory0.view = nullptr;
                reflectionSpecularHistory0.texture = nullptr;
                reflectionSpecularHistory1.view = nullptr;
                reflectionSpecularHistory1.texture = nullptr;
                reflectionNumFramesHistory0.view = nullptr;
                reflectionNumFramesHistory0.texture = nullptr;
                reflectionNumFramesHistory1.view = nullptr;
                reflectionNumFramesHistory1.texture = nullptr;
                roughSpecularIndirectHistory0.view = nullptr;
                roughSpecularIndirectHistory0.texture = nullptr;
                roughSpecularIndirectHistory1.view = nullptr;
                roughSpecularIndirectHistory1.texture = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    void PostProcessState::setPostProcessSettings(const PostProcessSettings& settings) noexcept
    {
        const bool taaMethodChanged =
            mSettings.antiAliasing.method != settings.antiAliasing.method;
        const bool aoEnabled =
            !mSettings.ambientOcclusion.enabled && settings.ambientOcclusion.enabled;
        // ColorGradingSettings 还没有接完整, 暂时不做逐字段比较, 调用即视为"设置变了"; 调用方
        // (编辑器 UI / 反序列化)需要自己保证不在参数没变的情况下调用, 避免每帧都重建 LUT
        mSettings = settings;
        mSettings.exposure.enabled = true;
        if (taaMethodChanged)
            resetTAAHistory();
        if (aoEnabled)
            resetAOHistory();
        mColorGradingLutDirty = true;
        mAutoExposureHistoryValid = false;
        // jitter 开关直接跟着 AA 方式走, 不需要调用方（编辑器 UI / 反序列化）额外调
        // setJitterEnabled()——两者本来就是同一个开关的两种表现形式, 分开设容易出现只改了一边、
        // TAA 没开却一直在抖动(或者反过来 resolve 在跑但没有抖动可消)的不一致状态。
        mJitterEnabled = settings.antiAliasing.method == AntiAliasingMethod::TAA;
    }

    void PostProcessState::notifyCameraCut() noexcept
    {
        resetTAAHistory();
        resetAOHistory();
        resetSceneDepthHistory();
        resetDiffuseIndirectHistory();
        resetScreenProbeLightingHistory();
    }

    void PostProcessState::prepareColorGradingLut(rhi::CommandList& cmdList)
    {
        if (mColorGradingLutInitialized)
            return;

        rhi::TextureBarrier barrier;
        barrier.texture = mColorGradingLut.get();
        barrier.range = mColorGradingLutView->getDesc().range;
        barrier.oldState = rhi::ResourceState::None;
        barrier.newState = rhi::ResourceState::Shader_Sampled;
        cmdList.insertBarriers({}, { &barrier, 1 });

        mColorGradingLutInitialized = true;
    }

    void PostProcessState::prepareAutoExposureData(rhi::CommandList& cmdList)
    {
        if (mAutoExposureDataInitialized)
            return;

        constexpr float4 kInitialExposure(1.0f);
        auto allocation = mRenderBackend.getStagingAllocator().forceAllocate(sizeof(kInitialExposure));
        std::memcpy(allocation.mappedData, &kInitialExposure, sizeof(kInitialExposure));

        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mAutoExposureDataBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = sizeof(kInitialExposure);
        preBarrier.oldState = rhi::ResourceState::None;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        rhi::BufferCopyRegion copyRegion;
        copyRegion.srcOffset = allocation.offset;
        copyRegion.dstOffset = 0;
        copyRegion.size = sizeof(kInitialExposure);
        cmdList.copyBuffer(allocation.buffer, mAutoExposureDataBuffer.get(), { &copyRegion, 1 });

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mAutoExposureDataBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = sizeof(kInitialExposure);
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
        cmdList.insertBarriers({ &postBarrier, 1 });

        mAutoExposureDataInitialized = true;
    }

    PostProcessState::PreExposureResult PostProcessState::updatePreExposure(uint32_t frameIndex) noexcept
    {
        if (mPreExposureFrameIndex != frameIndex)
        {
            mPreExposureFrameIndex = frameIndex;
            mPrevPreExposure = mPreExposure;
            if (!mSettings.exposure.enabled)
            {
                mPreExposure = 1.0f;
            }
            else if (mSettings.exposure.mode == ExposureMode::Manual)
            {
                constexpr float kLensAttenuation = 0.78f;
                constexpr float kISOSaturationSpeedConstant = 0.78f;
                const float luminanceMax = kISOSaturationSpeedConstant / kLensAttenuation;
                const float aperture = std::max(mSettings.exposure.aperture, 0.001f);
                const float shutterTime = std::max(mSettings.exposure.shutterSpeed, 0.000001f);
                const float iso = std::max(mSettings.exposure.iso, 1.0f);
                const float ev100 = std::log2(aperture * aperture / shutterTime * 100.0f / iso);
                mPreExposure = std::exp2(mSettings.exposure.exposureCompensation) /
                    (luminanceMax * std::exp2(ev100));
            }
            else
            {
                mPreExposure = *mAutoExposureReadbackValue;
            }
            mPreExposure = std::max(mPreExposure, 0.0001f);
        }

        return { mPreExposure, mPrevPreExposure };
    }

    void PostProcessState::ensureTAAHistory(uint2 extent)
    {
        if (extent.x == 0 || extent.y == 0)
            return;
        if (extent == mTAAHistoryExtent && mTAAHistory[0].texture)
            return;

        auto& device = mRenderBackend.getDevice();
        rhi::TextureDesc historyDesc;
        historyDesc.type = rhi::TextureType::Texture_2D;
        historyDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
        historyDesc.extent = { extent.x, extent.y, 1 };
        historyDesc.mipCount = 1;
        historyDesc.layerCount = 1;
        historyDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;

        HistoryTexture newHistories[2];
        for (auto& history : newHistories)
        {
            history.texture = device.createTexture(historyDesc, rhi::MemoryType::Gpu_Only);
            history.view = history.texture->createView(rhi::makeTextureDefaultViewDesc(historyDesc));
        }

        // TextureView only keeps a raw reference to its Texture. Keep each old pair alive while
        // installing the replacement, then retire it after all in-flight frames have completed.
        HistoryTexture oldHistory0 = std::move(mTAAHistory[0]);
        HistoryTexture oldHistory1 = std::move(mTAAHistory[1]);
        mTAAHistory[0] = std::move(newHistories[0]);
        mTAAHistory[1] = std::move(newHistories[1]);

        if (oldHistory0.texture || oldHistory1.texture)
        {
            mRenderBackend.getDeferredTaskQueue().push(
                [history0 = std::move(oldHistory0), history1 = std::move(oldHistory1)]() mutable
                {
                    history0.view = nullptr;
                    history0.texture = nullptr;
                    history1.view = nullptr;
                    history1.texture = nullptr;
                },
                kMaxFramesInFlight
            );
        }

        mTAAHistoryExtent = extent;
        mTAAHistoryWriteIndex = 0;
        mTAAHistoryInitialized = false;
        // 尺寸变了(或者第一次创建)意味着旧纹理里的内容(如果有的话)跟这一帧的分辨率对不上,
        // 不能再拿去混——下一次 TAAPass 只能直接输出当前帧颜色, 直到这一帧写完一次新的 history
        mTAAHistoryValid = false;
    }

    void PostProcessState::prepareTAAHistory(rhi::CommandList& cmdList)
    {
        if (mTAAHistoryInitialized || !mTAAHistory[0].texture || !mTAAHistory[1].texture)
            return;

        rhi::TextureBarrier barriers[2];
        for (uint32_t i = 0; i < 2; ++i)
        {
            barriers[i].texture = mTAAHistory[i].texture.get();
            barriers[i].range = mTAAHistory[i].view->getDesc().range;
            barriers[i].oldState = rhi::ResourceState::None;
            barriers[i].newState = rhi::ResourceState::Shader_Sampled;
        }
        cmdList.insertBarriers({}, { barriers, 2 });

        mTAAHistoryInitialized = true;
    }

    void PostProcessState::ensureAOHistory(uint2 extent)
    {
        if (extent.x == 0 || extent.y == 0)
            return;
        if (extent == mAOHistoryExtent && mAOHistory[0].texture)
            return;

        auto& device = mRenderBackend.getDevice();
        rhi::TextureDesc historyDesc;
        historyDesc.type = rhi::TextureType::Texture_2D;
        historyDesc.format = rhi::TextureFormat::R8_UNORM;
        historyDesc.extent = { extent.x, extent.y, 1 };
        historyDesc.mipCount = 1;
        historyDesc.layerCount = 1;
        // Storage(不是 Color_Attachment)——AOTemporalFilterPass 是 compute pass, 通过 UAV
        // 写入这张纹理, 不是 graphics attachment 输出
        historyDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;

        HistoryTexture newHistories[2];
        for (auto& history : newHistories)
        {
            history.texture = device.createTexture(historyDesc, rhi::MemoryType::Gpu_Only);
            history.view = history.texture->createView(rhi::makeTextureDefaultViewDesc(historyDesc));
        }

        HistoryTexture oldHistory0 = std::move(mAOHistory[0]);
        HistoryTexture oldHistory1 = std::move(mAOHistory[1]);
        mAOHistory[0] = std::move(newHistories[0]);
        mAOHistory[1] = std::move(newHistories[1]);

        if (oldHistory0.texture || oldHistory1.texture)
        {
            mRenderBackend.getDeferredTaskQueue().push(
                [history0 = std::move(oldHistory0), history1 = std::move(oldHistory1)]() mutable
                {
                    history0.view = nullptr;
                    history0.texture = nullptr;
                    history1.view = nullptr;
                    history1.texture = nullptr;
                },
                kMaxFramesInFlight
            );
        }

        mAOHistoryExtent = extent;
        mAOHistoryWriteIndex = 0;
        mAOHistoryInitialized = false;
        mAOHistoryValid = false;
    }

    void PostProcessState::prepareAOHistory(rhi::CommandList& cmdList)
    {
        if (mAOHistoryInitialized || !mAOHistory[0].texture || !mAOHistory[1].texture)
            return;

        rhi::TextureBarrier barriers[2];
        for (uint32_t i = 0; i < 2; ++i)
        {
            barriers[i].texture = mAOHistory[i].texture.get();
            barriers[i].range = mAOHistory[i].view->getDesc().range;
            barriers[i].oldState = rhi::ResourceState::None;
            barriers[i].newState = rhi::ResourceState::Shader_Sampled;
        }
        cmdList.insertBarriers({}, { barriers, 2 });

        mAOHistoryInitialized = true;
    }

    void PostProcessState::ensureSceneDepthHistory(uint2 extent)
    {
        if (extent.x == 0 || extent.y == 0)
            return;
        if (extent == mSceneDepthHistoryExtent && mSceneDepthHistory.texture)
            return;

        auto& device = mRenderBackend.getDevice();
        rhi::TextureDesc historyDesc;
        historyDesc.type = rhi::TextureType::Texture_2D;
        historyDesc.format = rhi::TextureFormat::R32_SFLOAT;
        historyDesc.extent = { extent.x, extent.y, 1 };
        historyDesc.mipCount = 1;
        historyDesc.layerCount = 1;
        historyDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;

        HistoryTexture newHistory;
        newHistory.texture = device.createTexture(historyDesc, rhi::MemoryType::Gpu_Only);
        newHistory.view = newHistory.texture->createView(rhi::makeTextureDefaultViewDesc(historyDesc));

        HistoryTexture oldHistory = std::move(mSceneDepthHistory);
        mSceneDepthHistory = std::move(newHistory);

        if (oldHistory.texture)
        {
            mRenderBackend.getDeferredTaskQueue().push(
                [history = std::move(oldHistory)]() mutable
                {
                    history.view = nullptr;
                    history.texture = nullptr;
                },
                kMaxFramesInFlight
            );
        }

        mSceneDepthHistoryExtent = extent;
        mSceneDepthHistoryInitialized = false;
        mSceneDepthHistoryValid = false;
    }

    void PostProcessState::prepareSceneDepthHistory(rhi::CommandList& cmdList)
    {
        if (mSceneDepthHistoryInitialized || !mSceneDepthHistory.texture)
            return;

        rhi::TextureBarrier barrier;
        barrier.texture = mSceneDepthHistory.texture.get();
        barrier.range = mSceneDepthHistory.view->getDesc().range;
        barrier.oldState = rhi::ResourceState::None;
        barrier.newState = rhi::ResourceState::Shader_Sampled;
        cmdList.insertBarriers({}, { &barrier, 1 });

        mSceneDepthHistoryInitialized = true;
    }

    void PostProcessState::ensureDiffuseIndirectHistory(uint2 extent)
    {
        if (extent.x == 0 || extent.y == 0)
            return;
        if (extent == mDiffuseIndirectHistoryExtent && mDiffuseIndirectHistory[0].texture)
            return;

        auto& device = mRenderBackend.getDevice();
        rhi::TextureDesc historyDesc;
        historyDesc.type = rhi::TextureType::Texture_2D;
        historyDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
        historyDesc.extent = { extent.x, extent.y, 1 };
        historyDesc.mipCount = 1;
        historyDesc.layerCount = 1;
        // Storage(不是 Color_Attachment)——ScreenProbeTemporalFilterPass 是 compute pass,
        // 通过 UAV 写入这张纹理
        historyDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;

        HistoryTexture newHistories[2];
        for (auto& history : newHistories)
        {
            history.texture = device.createTexture(historyDesc, rhi::MemoryType::Gpu_Only);
            history.view = history.texture->createView(rhi::makeTextureDefaultViewDesc(historyDesc));
        }

        HistoryTexture oldHistory0 = std::move(mDiffuseIndirectHistory[0]);
        HistoryTexture oldHistory1 = std::move(mDiffuseIndirectHistory[1]);
        mDiffuseIndirectHistory[0] = std::move(newHistories[0]);
        mDiffuseIndirectHistory[1] = std::move(newHistories[1]);

        if (oldHistory0.texture || oldHistory1.texture)
        {
            mRenderBackend.getDeferredTaskQueue().push(
                [history0 = std::move(oldHistory0), history1 = std::move(oldHistory1)]() mutable
                {
                    history0.view = nullptr;
                    history0.texture = nullptr;
                    history1.view = nullptr;
                    history1.texture = nullptr;
                },
                kMaxFramesInFlight
            );
        }

        mDiffuseIndirectHistoryExtent = extent;
        mDiffuseIndirectHistoryWriteIndex = 0;
        mDiffuseIndirectHistoryInitialized = false;
        mDiffuseIndirectHistoryValid = false;
    }

    void PostProcessState::prepareDiffuseIndirectHistory(rhi::CommandList& cmdList)
    {
        if (mDiffuseIndirectHistoryInitialized || !mDiffuseIndirectHistory[0].texture || !mDiffuseIndirectHistory[1].texture)
            return;

        rhi::TextureBarrier barriers[2];
        for (uint32_t i = 0; i < 2; ++i)
        {
            barriers[i].texture = mDiffuseIndirectHistory[i].texture.get();
            barriers[i].range = mDiffuseIndirectHistory[i].view->getDesc().range;
            barriers[i].oldState = rhi::ResourceState::None;
            barriers[i].newState = rhi::ResourceState::Shader_Sampled;
        }
        cmdList.insertBarriers({}, { barriers, 2 });

        mDiffuseIndirectHistoryInitialized = true;
    }

    void PostProcessState::ensureShortRangeAOHistory(uint2 extent)
    {
        if (extent.x == 0 || extent.y == 0)
            return;
        if (extent == mShortRangeAOHistoryExtent && mShortRangeAOHistory[0].texture)
            return;

        auto& device = mRenderBackend.getDevice();
        rhi::TextureDesc historyDesc;
        historyDesc.type = rhi::TextureType::Texture_2D;
        historyDesc.format = rhi::TextureFormat::R32_UINT;
        historyDesc.extent = { extent.x, extent.y, 1 };
        historyDesc.mipCount = 1;
        historyDesc.layerCount = 1;
        historyDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;

        HistoryTexture newHistories[2];
        for (auto& history : newHistories)
        {
            history.texture = device.createTexture(historyDesc, rhi::MemoryType::Gpu_Only);
            history.view = history.texture->createView(rhi::makeTextureDefaultViewDesc(historyDesc));
        }

        HistoryTexture oldHistory0 = std::move(mShortRangeAOHistory[0]);
        HistoryTexture oldHistory1 = std::move(mShortRangeAOHistory[1]);
        mShortRangeAOHistory[0] = std::move(newHistories[0]);
        mShortRangeAOHistory[1] = std::move(newHistories[1]);

        if (oldHistory0.texture || oldHistory1.texture)
        {
            mRenderBackend.getDeferredTaskQueue().push(
                [history0 = std::move(oldHistory0), history1 = std::move(oldHistory1)]() mutable
                {
                    history0.view = nullptr;
                    history0.texture = nullptr;
                    history1.view = nullptr;
                    history1.texture = nullptr;
                },
                kMaxFramesInFlight
            );
        }

        mShortRangeAOHistoryExtent = extent;
        mShortRangeAOHistoryWriteIndex = 0;
        mShortRangeAOHistoryInitialized = false;
    }

    void PostProcessState::prepareShortRangeAOHistory(rhi::CommandList& cmdList)
    {
        if (mShortRangeAOHistoryInitialized || !mShortRangeAOHistory[0].texture || !mShortRangeAOHistory[1].texture)
            return;

        rhi::TextureBarrier barriers[2];
        for (uint32_t i = 0; i < 2; ++i)
        {
            barriers[i].texture = mShortRangeAOHistory[i].texture.get();
            barriers[i].range = mShortRangeAOHistory[i].view->getDesc().range;
            barriers[i].oldState = rhi::ResourceState::None;
            barriers[i].newState = rhi::ResourceState::Shader_Sampled;
        }
        cmdList.insertBarriers({}, { barriers, 2 });

        mShortRangeAOHistoryInitialized = true;
    }

    void PostProcessState::ensureRoughSpecularIndirectHistory(uint2 extent)
    {
        if (extent.x == 0 || extent.y == 0)
            return;
        if (extent == mRoughSpecularIndirectHistoryExtent && mRoughSpecularIndirectHistory[0].texture)
            return;

        auto& device = mRenderBackend.getDevice();
        rhi::TextureDesc historyDesc;
        historyDesc.type = rhi::TextureType::Texture_2D;
        historyDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
        historyDesc.extent = { extent.x, extent.y, 1 };
        historyDesc.mipCount = 1;
        historyDesc.layerCount = 1;
        historyDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;

        HistoryTexture newHistories[2];
        for (auto& history : newHistories)
        {
            history.texture = device.createTexture(historyDesc, rhi::MemoryType::Gpu_Only);
            history.view = history.texture->createView(rhi::makeTextureDefaultViewDesc(historyDesc));
        }

        HistoryTexture oldHistory0 = std::move(mRoughSpecularIndirectHistory[0]);
        HistoryTexture oldHistory1 = std::move(mRoughSpecularIndirectHistory[1]);
        mRoughSpecularIndirectHistory[0] = std::move(newHistories[0]);
        mRoughSpecularIndirectHistory[1] = std::move(newHistories[1]);

        if (oldHistory0.texture || oldHistory1.texture)
        {
            mRenderBackend.getDeferredTaskQueue().push(
                [history0 = std::move(oldHistory0), history1 = std::move(oldHistory1)]() mutable
                {
                    history0.view = nullptr;
                    history0.texture = nullptr;
                    history1.view = nullptr;
                    history1.texture = nullptr;
                },
                kMaxFramesInFlight
            );
        }

        mRoughSpecularIndirectHistoryExtent = extent;
        mRoughSpecularIndirectHistoryWriteIndex = 0;
        mRoughSpecularIndirectHistoryInitialized = false;
    }

    void PostProcessState::prepareRoughSpecularIndirectHistory(rhi::CommandList& cmdList)
    {
        if (mRoughSpecularIndirectHistoryInitialized || !mRoughSpecularIndirectHistory[0].texture || !mRoughSpecularIndirectHistory[1].texture)
            return;

        rhi::TextureBarrier barriers[2];
        for (uint32_t i = 0; i < 2; ++i)
        {
            barriers[i].texture = mRoughSpecularIndirectHistory[i].texture.get();
            barriers[i].range = mRoughSpecularIndirectHistory[i].view->getDesc().range;
            barriers[i].oldState = rhi::ResourceState::None;
            barriers[i].newState = rhi::ResourceState::Shader_Sampled;
        }
        cmdList.insertBarriers({}, { barriers, 2 });

        mRoughSpecularIndirectHistoryInitialized = true;
    }

    void PostProcessState::ensureScreenProbeLightingHistory(uint2 radianceExtent, uint2 depthExtent)
    {
        if (radianceExtent.x == 0 || radianceExtent.y == 0 || depthExtent.x == 0 || depthExtent.y == 0)
            return;
        if (radianceExtent == mScreenProbeLightingHistoryRadianceExtent
            && depthExtent == mScreenProbeLightingHistoryDepthExtent
            && mScreenProbeLightingHistoryRadiance.texture
            && mScreenProbeLightingHistorySceneDepth.texture)
            return;

        auto& device = mRenderBackend.getDevice();

        rhi::TextureDesc radianceDesc;
        radianceDesc.type = rhi::TextureType::Texture_2D;
        radianceDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
        radianceDesc.extent = { radianceExtent.x, radianceExtent.y, 1 };
        radianceDesc.mipCount = 1;
        radianceDesc.layerCount = 1;
        radianceDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;

        rhi::TextureDesc depthDesc;
        depthDesc.type = rhi::TextureType::Texture_2D;
        depthDesc.format = rhi::TextureFormat::R32_SFLOAT;
        depthDesc.extent = { depthExtent.x, depthExtent.y, 1 };
        depthDesc.mipCount = 1;
        depthDesc.layerCount = 1;
        depthDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;

        HistoryTexture newRadiance;
        newRadiance.texture = device.createTexture(radianceDesc, rhi::MemoryType::Gpu_Only);
        newRadiance.view = newRadiance.texture->createView(rhi::makeTextureDefaultViewDesc(radianceDesc));

        HistoryTexture newDepth;
        newDepth.texture = device.createTexture(depthDesc, rhi::MemoryType::Gpu_Only);
        newDepth.view = newDepth.texture->createView(rhi::makeTextureDefaultViewDesc(depthDesc));

        HistoryTexture oldRadiance = std::move(mScreenProbeLightingHistoryRadiance);
        HistoryTexture oldDepth = std::move(mScreenProbeLightingHistorySceneDepth);
        mScreenProbeLightingHistoryRadiance = std::move(newRadiance);
        mScreenProbeLightingHistorySceneDepth = std::move(newDepth);

        if (oldRadiance.texture || oldDepth.texture)
        {
            mRenderBackend.getDeferredTaskQueue().push(
                [radiance = std::move(oldRadiance), depth = std::move(oldDepth)]() mutable
                {
                    radiance.view = nullptr;
                    radiance.texture = nullptr;
                    depth.view = nullptr;
                    depth.texture = nullptr;
                },
                kMaxFramesInFlight
            );
        }

        mScreenProbeLightingHistoryRadianceExtent = radianceExtent;
        mScreenProbeLightingHistoryDepthExtent = depthExtent;
        mScreenProbeLightingHistoryInitialized = false;
        mScreenProbeLightingHistoryValid = false;
    }

    void PostProcessState::prepareScreenProbeLightingHistory(rhi::CommandList& cmdList)
    {
        if (mScreenProbeLightingHistoryInitialized
            || !mScreenProbeLightingHistoryRadiance.texture
            || !mScreenProbeLightingHistorySceneDepth.texture)
            return;

        rhi::TextureBarrier barriers[2];
        barriers[0].texture = mScreenProbeLightingHistoryRadiance.texture.get();
        barriers[0].range = mScreenProbeLightingHistoryRadiance.view->getDesc().range;
        barriers[0].oldState = rhi::ResourceState::None;
        barriers[0].newState = rhi::ResourceState::Shader_Sampled;
        barriers[1].texture = mScreenProbeLightingHistorySceneDepth.texture.get();
        barriers[1].range = mScreenProbeLightingHistorySceneDepth.view->getDesc().range;
        barriers[1].oldState = rhi::ResourceState::None;
        barriers[1].newState = rhi::ResourceState::Shader_Sampled;
        cmdList.insertBarriers({}, { barriers, 2 });

        mScreenProbeLightingHistoryInitialized = true;
    }

    void PostProcessState::ensureReflectionHistory(uint2 extent)
    {
        if (extent.x == 0 || extent.y == 0)
            return;
        if (extent == mReflectionHistoryExtent && mReflectionSpecularHistory[0].texture)
            return;

        auto& device = mRenderBackend.getDevice();

        rhi::TextureDesc specularDesc;
        specularDesc.type = rhi::TextureType::Texture_2D;
        specularDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
        specularDesc.extent = { extent.x, extent.y, 1 };
        specularDesc.mipCount = 1;
        specularDesc.layerCount = 1;
        specularDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;

        rhi::TextureDesc numFramesDesc;
        numFramesDesc.type = rhi::TextureType::Texture_2D;
        numFramesDesc.format = rhi::TextureFormat::R8_UNORM;
        numFramesDesc.extent = { extent.x, extent.y, 1 };
        numFramesDesc.mipCount = 1;
        numFramesDesc.layerCount = 1;
        numFramesDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;

        HistoryTexture newSpecular[2];
        HistoryTexture newNumFrames[2];
        for (uint32_t i = 0; i < 2; ++i)
        {
            newSpecular[i].texture = device.createTexture(specularDesc, rhi::MemoryType::Gpu_Only);
            newSpecular[i].view = newSpecular[i].texture->createView(rhi::makeTextureDefaultViewDesc(specularDesc));
            newNumFrames[i].texture = device.createTexture(numFramesDesc, rhi::MemoryType::Gpu_Only);
            newNumFrames[i].view = newNumFrames[i].texture->createView(rhi::makeTextureDefaultViewDesc(numFramesDesc));
        }

        HistoryTexture oldSpecular0 = std::move(mReflectionSpecularHistory[0]);
        HistoryTexture oldSpecular1 = std::move(mReflectionSpecularHistory[1]);
        HistoryTexture oldNumFrames0 = std::move(mReflectionNumFramesHistory[0]);
        HistoryTexture oldNumFrames1 = std::move(mReflectionNumFramesHistory[1]);
        mReflectionSpecularHistory[0] = std::move(newSpecular[0]);
        mReflectionSpecularHistory[1] = std::move(newSpecular[1]);
        mReflectionNumFramesHistory[0] = std::move(newNumFrames[0]);
        mReflectionNumFramesHistory[1] = std::move(newNumFrames[1]);

        if (oldSpecular0.texture || oldSpecular1.texture || oldNumFrames0.texture || oldNumFrames1.texture)
        {
            mRenderBackend.getDeferredTaskQueue().push(
                [specular0 = std::move(oldSpecular0), specular1 = std::move(oldSpecular1),
                 numFrames0 = std::move(oldNumFrames0), numFrames1 = std::move(oldNumFrames1)]() mutable
                {
                    specular0.view = nullptr;
                    specular0.texture = nullptr;
                    specular1.view = nullptr;
                    specular1.texture = nullptr;
                    numFrames0.view = nullptr;
                    numFrames0.texture = nullptr;
                    numFrames1.view = nullptr;
                    numFrames1.texture = nullptr;
                },
                kMaxFramesInFlight
            );
        }

        mReflectionHistoryExtent = extent;
        mReflectionHistoryWriteIndex = 0;
        mReflectionHistoryInitialized = false;
        mReflectionHistoryValid = false;
    }

    void PostProcessState::prepareReflectionHistory(rhi::CommandList& cmdList)
    {
        if (mReflectionHistoryInitialized
            || !mReflectionSpecularHistory[0].texture || !mReflectionSpecularHistory[1].texture
            || !mReflectionNumFramesHistory[0].texture || !mReflectionNumFramesHistory[1].texture)
            return;

        rhi::TextureBarrier barriers[4];
        barriers[0].texture = mReflectionSpecularHistory[0].texture.get();
        barriers[0].range = mReflectionSpecularHistory[0].view->getDesc().range;
        barriers[1].texture = mReflectionSpecularHistory[1].texture.get();
        barriers[1].range = mReflectionSpecularHistory[1].view->getDesc().range;
        barriers[2].texture = mReflectionNumFramesHistory[0].texture.get();
        barriers[2].range = mReflectionNumFramesHistory[0].view->getDesc().range;
        barriers[3].texture = mReflectionNumFramesHistory[1].texture.get();
        barriers[3].range = mReflectionNumFramesHistory[1].view->getDesc().range;
        for (auto& barrier : barriers)
        {
            barrier.oldState = rhi::ResourceState::None;
            barrier.newState = rhi::ResourceState::Shader_Sampled;
        }
        cmdList.insertBarriers({}, { barriers, 4 });

        mReflectionHistoryInitialized = true;
    }
}
