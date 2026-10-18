#pragma once
#include <array>
#include <memory>

#include <RHI/Texture.h>
#include <RHI/Buffer.h>
#include <RHI/CommandList.h>
#include <RenderBase/RenderBaseDefines.h>

#include "PostProcessSettings.h"

namespace hyp
{
    class RenderBackend;

    // 一个相机视图的后处理渲染状态: PostProcessSettings 本身 + 围绕它的常驻 GPU 资源
    // (Color Grading LUT、AutoExposure buffer、TAA/AO 的 ping-pong history)。跟 CameraProxy
    // 是组合关系, 不是通过 ProxyType/ProxyManager 管理生命周期的独立 Proxy——不叫 XXXProxy 是为了
    // 不让人误以为能用 proxyManager.getProxy() 拿到它, 见 CameraProxy::getPostProcessState()。
    class PostProcessState
    {
    public:
        PostProcessState(RenderBackend& renderBackend, uint32_t viewId, const PostProcessSettings& settings);

        ~PostProcessState() noexcept;

        PostProcessState(const PostProcessState&) = delete;
        PostProcessState& operator=(const PostProcessState&) = delete;

        uint32_t getViewId() const noexcept
        {
            return mViewId;
        }

        void setPostProcessSettings(const PostProcessSettings& settings) noexcept;

        const PostProcessSettings& getPostProcessSettings() const noexcept
        {
            return mSettings;
        }

        // 没有对应的 setJitterEnabled(): 这个开关完全由 setPostProcessSettings() 里的
        // AntiAliasingSettings::method 派生, 单独暴露一个 setter 只会让两者不同步
        bool isJitterEnabled() const noexcept
        {
            return mJitterEnabled;
        }

        // 相机瞬移/切换关卡/编辑器切场景这类"上一帧画面和这一帧毫无关系"的场合调用, 让 TAA/AO
        // 都不要拿 history 去混——具体哪些效果有时域历史、该怎么失效, 是这个类自己的内部知识,
        // 调用方(CameraProxy::markCameraCut())不需要逐个知道
        void notifyCameraCut() noexcept;

        // ColorGradingPass 用: 常驻的 Color Grading 3D LUT 只在 Tone Mapping / Color Grading
        // 参数变化时才需要重新生成一次, 平时的帧直接采样上一次生成的结果
        rhi::TextureView& getColorGradingLutView() const noexcept
        {
            return *mColorGradingLutView;
        }

        bool isColorGradingLutDirty() const noexcept
        {
            return mColorGradingLutDirty;
        }

        void clearColorGradingLutDirty() noexcept
        {
            mColorGradingLutDirty = false;
        }

        // LUT 纹理刚创建时是 Undefined layout, 但 import 进 render graph 时永远声明成
        // Shader_Sampled——RenderGraph 会在这张纹理本帧最后一次被用完后自动插入 barrier, 把它转回
        // import 时声明的 defaultState, 如果这里声明成 None/Undefined, 每帧末尾都会被转换到
        // Undefined layout 导致内容丢失。所以第一次真正使用前必须先手动插一次 None -> Shader_Sampled
        // 的 barrier(仿 TextureProxy::prepare()), 之后 declare 的 Shader_Sampled 才是真话。
        void prepareColorGradingLut(rhi::CommandList& cmdList);

        rhi::Buffer& getAutoExposureDataBuffer() const noexcept
        {
            return *mAutoExposureDataBuffer;
        }

        std::shared_ptr<float> getAutoExposureReadbackValue() const noexcept
        {
            return mAutoExposureReadbackValue;
        }

        TRef<rhi::Buffer> acquireAutoExposureReadbackBuffer() noexcept
        {
            TRef<rhi::Buffer> result = mAutoExposureReadbackBuffers[mAutoExposureReadbackIndex];
            mAutoExposureReadbackIndex =
                (mAutoExposureReadbackIndex + 1u) % mAutoExposureReadbackBuffers.size();
            return result;
        }

        // Initializes the camera-owned exposure buffer before its first render-graph import.
        void prepareAutoExposureData(rhi::CommandList& cmdList);

        bool isAutoExposureHistoryValid() const noexcept
        {
            return mAutoExposureHistoryValid;
        }

        void markAutoExposureHistoryValid() noexcept
        {
            mAutoExposureHistoryValid = true;
        }

        struct PreExposureResult
        {
            float current;
            float previous;
        };

        // CameraProxy::getSceneViewGroup() 每帧调用一次, 按 frameIndex 去重(同一帧内重复调用
        // 不会重复推进 previous)——语义和调用约束跟原来 CameraProxy::getSceneViewGroup() 里那段
        // 内联逻辑完全一致, 只是挪了地方。
        PreExposureResult updatePreExposure(uint32_t frameIndex) noexcept;

        // TAA 用: 两张常驻的 ping-pong history 纹理, 跟 mColorGradingLut 同一个套路——相机常驻,
        // 每帧 import 进 render graph, 不是 render graph 自己的 transient 资源, 否则跨帧就拿不到
        // 上一帧 resolve 完的结果。按当前 viewport 尺寸懒创建/重建(尺寸变了就重建并作废旧内容),
        // 只应该由 TAAPass 在 builder 阶段调用一次。extent 为 0(viewport 还没确定)时什么都不做。
        void ensureTAAHistory(uint2 extent);

        // history 是 render graph 外部持有的常驻纹理, import 时声明的默认状态是 Shader_Sampled。
        // 新建/resize 后先在 Upload pass 中完成一次真实的 Undefined -> Shader_Sampled 转换,
        // 后续 render graph 才能把这个默认状态当作可信的跨帧状态。
        void prepareTAAHistory(rhi::CommandList& cmdList);

        void resetTAAHistory() noexcept
        {
            mTAAHistoryValid = false;
        }

        bool isTAAHistoryValid() const noexcept
        {
            return mTAAHistoryValid;
        }

        rhi::TextureView& getTAAHistoryReadView() const noexcept
        {
            return *mTAAHistory[mTAAHistoryWriteIndex ^ 1].view;
        }

        rhi::TextureView& getTAAHistoryWriteView() const noexcept
        {
            return *mTAAHistory[mTAAHistoryWriteIndex].view;
        }

        // TAAPass 每帧 resolve 完之后调用一次, 翻转读写角色并标记 history 有效——放在这里而不是
        // 自动在 getTAAHistoryWriteView() 里翻转, 是因为 TAAPass 在同一帧的 builder/execute 两个
        // 阶段都需要读到同一份"这一帧该写哪张", 不能被提前翻转掉
        void swapTAAHistory() noexcept
        {
            mTAAHistoryWriteIndex ^= 1;
            mTAAHistoryValid = true;
        }

        // AO 用: 跟 mTAAHistory 并列的另一套常驻 ping-pong history, 单通道、compute pass 写入
        // (不是 graphics attachment), 语义和调用方式跟 TAA history 完全对应, 只是服务的是
        // AOTemporalFilterPass 而不是 TAAPass——两套历史互相独立, AO 的收敛速度/裁剪策略不会
        // 牵扯到 TAA, 反之亦然
        void ensureAOHistory(uint2 extent);

        void prepareAOHistory(rhi::CommandList& cmdList);

        void resetAOHistory() noexcept
        {
            mAOHistoryValid = false;
        }

        bool isAOHistoryValid() const noexcept
        {
            return mAOHistoryValid;
        }

        rhi::TextureView& getAOHistoryReadView() const noexcept
        {
            return *mAOHistory[mAOHistoryWriteIndex ^ 1].view;
        }

        rhi::TextureView& getAOHistoryWriteView() const noexcept
        {
            return *mAOHistory[mAOHistoryWriteIndex].view;
        }

        void swapAOHistory() noexcept
        {
            mAOHistoryWriteIndex ^= 1;
            mAOHistoryValid = true;
        }

        // 只有屏幕探针追踪运行时才写这张历史深度, 所以有效性按"上一帧刚写过"判断
        void ensureSceneDepthHistory(uint2 extent);

        void prepareSceneDepthHistory(rhi::CommandList& cmdList);

        void resetSceneDepthHistory() noexcept
        {
            mSceneDepthHistoryValid = false;
        }

        bool isSceneDepthHistoryValid() const noexcept
        {
            return mSceneDepthHistoryValid && mSceneDepthHistoryFrameIndex + 1u == mPreExposureFrameIndex;
        }

        rhi::TextureView& getSceneDepthHistoryView() const noexcept
        {
            return *mSceneDepthHistory.view;
        }

        void markSceneDepthHistoryWritten() noexcept
        {
            mSceneDepthHistoryValid = true;
            mSceneDepthHistoryFrameIndex = mPreExposureFrameIndex;
        }

        // 屏幕探针漫反射间接光时域重投影(M6)用: RGB 存上一帧混合后的 DiffuseIndirect, A 存
        // NumFramesAccumulated, 结构/调用方式跟 mAOHistory 完全对应(ping-pong、compute pass 写)
        void ensureDiffuseIndirectHistory(uint2 extent);

        void prepareDiffuseIndirectHistory(rhi::CommandList& cmdList);

        void resetDiffuseIndirectHistory() noexcept
        {
            mDiffuseIndirectHistoryValid = false;
        }

        bool isDiffuseIndirectHistoryValid() const noexcept
        {
            return mDiffuseIndirectHistoryValid;
        }

        rhi::TextureView& getDiffuseIndirectHistoryReadView() const noexcept
        {
            return *mDiffuseIndirectHistory[mDiffuseIndirectHistoryWriteIndex ^ 1].view;
        }

        rhi::TextureView& getDiffuseIndirectHistoryWriteView() const noexcept
        {
            return *mDiffuseIndirectHistory[mDiffuseIndirectHistoryWriteIndex].view;
        }

        void swapDiffuseIndirectHistory() noexcept
        {
            mDiffuseIndirectHistoryWriteIndex ^= 1;
            mDiffuseIndirectHistoryValid = true;
        }

        // 有效性沿用 isDiffuseIndirectHistoryValid()
        void ensureShortRangeAOHistory(uint2 extent);

        void prepareShortRangeAOHistory(rhi::CommandList& cmdList);

        rhi::TextureView& getShortRangeAOHistoryReadView() const noexcept
        {
            return *mShortRangeAOHistory[mShortRangeAOHistoryWriteIndex ^ 1].view;
        }

        rhi::TextureView& getShortRangeAOHistoryWriteView() const noexcept
        {
            return *mShortRangeAOHistory[mShortRangeAOHistoryWriteIndex].view;
        }

        void swapShortRangeAOHistory() noexcept
        {
            mShortRangeAOHistoryWriteIndex ^= 1;
        }

        // 屏幕探针粗糙镜面(R6 RoughSpecularIndirect)时域滤波用: RGB 存历史值, A 不用——
        // NumFramesAccumulated 沿用 mDiffuseIndirectHistory 那份, 不用再存一份。有效性同样
        // 沿用 isDiffuseIndirectHistoryValid(), 见 Document/Lumen_ScreenProbeGather_Plan.md §6.8c
        void ensureRoughSpecularIndirectHistory(uint2 extent);

        void prepareRoughSpecularIndirectHistory(rhi::CommandList& cmdList);

        rhi::TextureView& getRoughSpecularIndirectHistoryReadView() const noexcept
        {
            return *mRoughSpecularIndirectHistory[mRoughSpecularIndirectHistoryWriteIndex ^ 1].view;
        }

        rhi::TextureView& getRoughSpecularIndirectHistoryWriteView() const noexcept
        {
            return *mRoughSpecularIndirectHistory[mRoughSpecularIndirectHistoryWriteIndex].view;
        }

        void swapRoughSpecularIndirectHistory() noexcept
        {
            mRoughSpecularIndirectHistoryWriteIndex ^= 1;
        }

        // 结构化重要性采样(M7.2, Lighting PDF)用: 探针辐射度(滤波后)+ 探针线性深度两张历史,
        // 单缓冲, 跟 mSceneDepthHistory 同一个模式——读者(ScreenProbeImportanceSamplingPass 的
        // Lighting PDF pass)排在 Placement 之后、Filtering 之前, 写者(拷贝这一帧结果的 copy
        // pass)排在 Filtering 产出 ScreenProbeRadianceFiltered 之后, 读写顺序天然由这两个 pass
        // 各自的职责决定, 不需要 ping-pong。两张纹理总是一起 ensure/写入, 共用一份有效性状态。
        void ensureScreenProbeLightingHistory(uint2 radianceExtent, uint2 depthExtent);

        void prepareScreenProbeLightingHistory(rhi::CommandList& cmdList);

        void resetScreenProbeLightingHistory() noexcept
        {
            mScreenProbeLightingHistoryValid = false;
        }

        bool isScreenProbeLightingHistoryValid() const noexcept
        {
            return mScreenProbeLightingHistoryValid && mScreenProbeLightingHistoryFrameIndex + 1u == mPreExposureFrameIndex;
        }

        rhi::TextureView& getScreenProbeLightingHistoryRadianceView() const noexcept
        {
            return *mScreenProbeLightingHistoryRadiance.view;
        }

        rhi::TextureView& getScreenProbeLightingHistorySceneDepthView() const noexcept
        {
            return *mScreenProbeLightingHistorySceneDepth.view;
        }

        void markScreenProbeLightingHistoryWritten() noexcept
        {
            mScreenProbeLightingHistoryValid = true;
            mScreenProbeLightingHistoryFrameIndex = mPreExposureFrameIndex;
        }

        // Lumen 镜面反射时域滤波(R4)用: 一张 RGBA16F(rgb=镜面, a=亮度二阶矩)+ 一张 R8_UNORM
        // (累积帧数), ping-pong, 两张纹理共用同一份写索引/有效性状态——跟 mDiffuseIndirectHistory
        // 同一个模式。见 Document/Lumen_Reflections_Plan.md §3.4
        void ensureReflectionHistory(uint2 extent);

        void prepareReflectionHistory(rhi::CommandList& cmdList);

        void resetReflectionHistory() noexcept
        {
            mReflectionHistoryValid = false;
        }

        bool isReflectionHistoryValid() const noexcept
        {
            return mReflectionHistoryValid;
        }

        rhi::TextureView& getReflectionSpecularHistoryReadView() const noexcept
        {
            return *mReflectionSpecularHistory[mReflectionHistoryWriteIndex ^ 1].view;
        }

        rhi::TextureView& getReflectionSpecularHistoryWriteView() const noexcept
        {
            return *mReflectionSpecularHistory[mReflectionHistoryWriteIndex].view;
        }

        rhi::TextureView& getReflectionNumFramesHistoryReadView() const noexcept
        {
            return *mReflectionNumFramesHistory[mReflectionHistoryWriteIndex ^ 1].view;
        }

        rhi::TextureView& getReflectionNumFramesHistoryWriteView() const noexcept
        {
            return *mReflectionNumFramesHistory[mReflectionHistoryWriteIndex].view;
        }

        void swapReflectionHistory() noexcept
        {
            mReflectionHistoryWriteIndex ^= 1;
            mReflectionHistoryValid = true;
        }

    private:
        RenderBackend& mRenderBackend;
        uint32_t mViewId;

        PostProcessSettings mSettings;

        // 由 setPostProcessSettings() 里的 AntiAliasingSettings::method 派生, 默认值(None)
        // 天然对应关闭——PostProcessSettings 没被显式设过 TAA 之前, 不会平白多出抖动
        bool mJitterEnabled = false;

        TRef<rhi::Texture> mColorGradingLut;
        TRef<rhi::TextureView> mColorGradingLutView;
        bool mColorGradingLutDirty = true;
        bool mColorGradingLutInitialized = false;

        TRef<rhi::Buffer> mAutoExposureDataBuffer;
        // One extra slot is required because DeferredTaskQueue runs at endFrame(): a readback
        // deferred by kMaxFramesInFlight ticks is consumed after the matching GPU flight slot
        // has been waited, but before this ring wraps back to the same readback buffer.
        std::array<TRef<rhi::Buffer>, kMaxFramesInFlight + 1u> mAutoExposureReadbackBuffers;
        uint32_t mAutoExposureReadbackIndex = 0;
        std::shared_ptr<float> mAutoExposureReadbackValue = std::make_shared<float>(1.0f);
        float mPreExposure = 1.0f;
        float mPrevPreExposure = 1.0f;
        uint32_t mPreExposureFrameIndex = ~0u;
        bool mAutoExposureDataInitialized = false;
        bool mAutoExposureHistoryValid = false;

        // 常驻 ping-pong history 纹理的通用载体——TAA 和 AO 各自一套(mTAAHistory/mAOHistory),
        // 结构完全一样(一张纹理 + 一张 view), 没有必要各定义一个同形状的结构体
        struct HistoryTexture
        {
            TRef<rhi::Texture> texture;
            TRef<rhi::TextureView> view;
        };
        HistoryTexture mTAAHistory[2];
        uint2 mTAAHistoryExtent = { 0, 0 };
        uint32_t mTAAHistoryWriteIndex = 0;
        bool mTAAHistoryValid = false;
        bool mTAAHistoryInitialized = false;

        HistoryTexture mAOHistory[2];
        uint2 mAOHistoryExtent = { 0, 0 };
        uint32_t mAOHistoryWriteIndex = 0;
        bool mAOHistoryValid = false;
        bool mAOHistoryInitialized = false;

        HistoryTexture mSceneDepthHistory;
        uint2 mSceneDepthHistoryExtent = { 0, 0 };
        uint32_t mSceneDepthHistoryFrameIndex = 0;
        bool mSceneDepthHistoryValid = false;
        bool mSceneDepthHistoryInitialized = false;

        HistoryTexture mDiffuseIndirectHistory[2];
        uint2 mDiffuseIndirectHistoryExtent = { 0, 0 };
        uint32_t mDiffuseIndirectHistoryWriteIndex = 0;
        bool mDiffuseIndirectHistoryValid = false;
        bool mDiffuseIndirectHistoryInitialized = false;

        HistoryTexture mShortRangeAOHistory[2];
        uint2 mShortRangeAOHistoryExtent = { 0, 0 };
        uint32_t mShortRangeAOHistoryWriteIndex = 0;
        bool mShortRangeAOHistoryInitialized = false;

        HistoryTexture mRoughSpecularIndirectHistory[2];
        uint2 mRoughSpecularIndirectHistoryExtent = { 0, 0 };
        uint32_t mRoughSpecularIndirectHistoryWriteIndex = 0;
        bool mRoughSpecularIndirectHistoryInitialized = false;

        HistoryTexture mScreenProbeLightingHistoryRadiance;
        HistoryTexture mScreenProbeLightingHistorySceneDepth;
        uint2 mScreenProbeLightingHistoryRadianceExtent = { 0, 0 };
        uint2 mScreenProbeLightingHistoryDepthExtent = { 0, 0 };
        uint32_t mScreenProbeLightingHistoryFrameIndex = 0;
        bool mScreenProbeLightingHistoryValid = false;
        bool mScreenProbeLightingHistoryInitialized = false;

        HistoryTexture mReflectionSpecularHistory[2];
        HistoryTexture mReflectionNumFramesHistory[2];
        uint2 mReflectionHistoryExtent = { 0, 0 };
        uint32_t mReflectionHistoryWriteIndex = 0;
        bool mReflectionHistoryValid = false;
        bool mReflectionHistoryInitialized = false;
    };
}
