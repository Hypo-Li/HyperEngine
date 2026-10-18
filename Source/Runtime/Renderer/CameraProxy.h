#pragma once
#include <RenderBase/TextureProxy.h>
#include "SceneElementProxy.h"
#include "SceneView.h"
#include "RendererDefines.h"
#include "GlobalDistanceField.h"
#include "PostProcessSettings.h"
#include "PostProcessState.h"

#include <optional>

namespace hyp
{
    class CameraProxy : public SceneElementProxy
    {
    public:
        struct Desc
        {
            bool visible = true;
            uint32_t viewId;
            float4 outputRect;
            TextureProxy* renderTarget;
            HString renderTargetName;
            uint2 renderTargetExtent;
            OutputRouting outputRouting;
            PostProcessSettings postProcessSettings;
            double3 eye;
            float3 forward;
            float3 up;
            ProjectionType projectionType;
            ProjectionAxis projectionAxis;
            float fov;
            float orthoSize;
            float nearPlane;
            float farPlane;
        };

        CameraProxy(SceneProxy& sceneProxy, const Desc& desc);

        virtual ~CameraProxy() noexcept;

        uint32_t getViewId() const noexcept
        {
            return mViewId;
        }

        void setViewMatrix(const double3& eye, float3 forward, float3 up);

        const double4x4& getViewMatrix() const noexcept
        {
            return mViewMatrix;
        }

        // CardCapturePass 按相机距离算 card 分辨率用: 直接存 setViewMatrix() 传入的 eye,
        // 不用每次都对 mViewMatrix 求逆再取平移列——求距离这种粗粒度的启发式计算不需要, 白白
        // 多算一次 4x4 求逆
        const double3& getWorldPosition() const noexcept
        {
            return mWorldPosition;
        }

        // TAA motion vector 用：上一实际录制帧的 view matrix。setter 只更新 current，
        // getSceneViewGroup() 读完本帧的 current/previous 之后立刻推进它。
        const double4x4& getPrevViewMatrix() const noexcept
        {
            return mPrevViewMatrix;
        }

        void setPerspectiveParameters(ProjectionAxis axis, float fov, float nearPlane, float farPlane);

        void setOrthographicParameters(ProjectionAxis axis, float orthoSize, float nearPlane, float farPlane);

        const float4x4& getProjectionMatrix() const noexcept
        {
            return mProjectionMatrix;
        }

        // TAA motion vector 用：上一实际录制帧的无 jitter 投影矩阵。
        const float4x4& getPrevProjectionMatrix() const noexcept
        {
            return mPrevProjectionMatrix;
        }

        float getNearPlane() const noexcept
        {
            return mNearPlane;
        }

        float getFarPlane() const noexcept
        {
            return mFarPlane;
        }

        void setOutputRect(const float4& rect) noexcept;

        void setCullingMask(uint32_t mask) noexcept;

        TextureProxy* getRenderTarget() const noexcept
        {
            return mRenderTargetInfo.texture;
        }

        // call by swapchain or editor view
        void setRenderTargetName(HString name) noexcept;

        HString getRenderTargetName() const noexcept
        {
            return mRenderTargetInfo.name;
        }

        void setRenderTargetExtent(uint2 extent) noexcept;

        // 编辑器切换编辑/运行模式、或者把某个 Camera 的输出临时改绑到别的 RenderTarget 时用:
        // 一次性替换 texture/name/extent/outputRouting 四个字段, 并按新 extent 重算投影矩阵。
        void setRenderTarget(TextureProxy* texture, HString name, uint2 extent, OutputRouting outputRouting) noexcept;

        float getAspectRatio() const noexcept;

        bool isViewportEmpty() const noexcept;

        // 注意: 这个函数每帧每个相机只应该调用一次——它在返回前会把 mViewMatrix/mProjectionMatrix
        // 推进成下一帧的 mPrevViewMatrix/mPrevProjectionMatrix(见实现)
        SceneViewGroup getSceneViewGroup(uint32_t frameIndex) const noexcept;

        // getSceneViewGroup() 每帧算出来的 SceneView 顺手存一份在这里——Editor 自己的
        // enqueueLambda 排在 Scene::tick() 那次 enqueue 之后执行, 能直接读到这一帧刚缓存的值,
        // 不需要再另开一条数据通路。返回空指针说明这个相机这一帧(或还从来没有)被
        // gatherSceneViewGroups() 处理过。
        const SceneView* getLastSceneView() const noexcept
        {
            return mLastSceneView ? &*mLastSceneView : nullptr;
        }

        // 没有对应的 setJitterEnabled(): 这个开关完全由 PostProcessState::setPostProcessSettings()
        // 里的 AntiAliasingSettings::method 派生, 单独暴露一个 setter 只会让两者不同步
        bool isJitterEnabled() const noexcept
        {
            return mPostProcess.isJitterEnabled();
        }

        void setPostProcessSettings(const PostProcessSettings& settings) noexcept;

        // One-shot discontinuity event. The next SceneView uses current matrices
        // as previous matrices and temporal histories are discarded. Consumed by
        // getSceneViewGroup(), which clears mCameraCutPending after reading it.
        void markCameraCut() const noexcept
        {
            mCameraCutPending = true;
            mPostProcess.notifyCameraCut();
        }

        const PostProcessSettings& getPostProcessSettings() const noexcept
        {
            return mPostProcess.getPostProcessSettings();
        }

        // 后处理相关的 GPU 资源(Color Grading LUT、AutoExposure buffer、TAA/AO history)和
        // PostProcessSettings 本身都不在 CameraProxy 上——CameraProxy 只管相机几何(矩阵/viewport/
        // render target), 后处理是独立的关注点, 见 PostProcessState 类注释。render graph 只能拿到
        // const CameraProxy&(见 SceneViewGroup::getSource), 这里返回非 const 引用是唯一需要放宽
        // const 的地方, 换掉了原来散落在一堆后处理字段上的 mutable。
        PostProcessState& getPostProcessState() const noexcept
        {
            return mPostProcess;
        }

        // render graph 只能拿到 const CameraProxy&(见 getPostProcessState() 上面那条同类型
        // 注释)——GDF 的 Layer 状态(needsComposite 等)本质上也是渲染图构建期的可变状态,
        // 同样的理由放宽 const
        GlobalDistanceField& getGlobalDistanceField() const noexcept
        {
            return mGlobalDistanceField;
        }

        // 供逻辑线程侧(比如编辑器相机)在不创建 CameraProxy 的情况下复用同一份投影数学,
        // 避免两处各写一遍容易漂移。
        static float4x4 makePerspectiveMatrix(ProjectionAxis fovAxis, float fov, float aspectRatio, float nearPlane, float farPlane);

        static float4x4 makeOrthographicMatrix(ProjectionAxis orthoAxis, float orthoSize, float aspectRatio, float nearPlane, float farPlane);

    private:
        // 宽高比(getAspectRatio())同时依赖 mRenderTargetInfo.extent 和 mOutputRect, 这两者
        // 中任意一个变化都要重算投影矩阵——setOutputRect()/setRenderTargetExtent()/
        // setPerspectiveParameters()/setOrthographicParameters() 统一调这一个函数, 不在
        // 各自内部各写一份, 避免漏掉某一条路径。
        void recomputeProjectionMatrix() noexcept;

        // 归一化的 mOutputRect 换算成实际像素矩形(x, y, width, height), 结果保证落在整数像素上。
        // getSceneViewGroup() 里给渲染用的 viewport 和 getAspectRatio() 都走这一个函数, 保证
        // "投影矩阵按什么宽高比算"和"实际光栅化到多大区域"永远一致。取整的必要性见实现里的注释。
        float4 computeViewportPixels() const noexcept;

    private:
        uint32_t mViewId;
        uint32_t mCullingMask = ~0u;
        uint32_t mRenderMask = ~0u;

        double4x4 mViewMatrix;
        mutable double4x4 mPrevViewMatrix;
        bool mViewMatrixInitialized = false;
        double3 mWorldPosition{ 0.0 };
        ProjectionType mProjectionType;
        ProjectionAxis mProjectionAxis;
        float mFov;
        float mOrthoSize;
        float mNearPlane;
        float mFarPlane;
        float4x4 mProjectionMatrix;
        mutable float4x4 mPrevProjectionMatrix;
        bool mProjectionMatrixInitialized = false;
        mutable bool mCameraCutPending = false;

        float4 mOutputRect;
        struct RenderTargetInfo
        {
            TextureProxy* texture;
            HString name;
            uint2 extent;
            OutputRouting outputRouting;
        };
        RenderTargetInfo mRenderTargetInfo;

        // 后处理设置和围绕它的 GPU 资源全部在这个对象里, CameraProxy 自己不碰——见
        // PostProcessState 类注释和 getPostProcessState()。render graph 只能拿到
        // const CameraProxy&, 这里的 mutable 换掉了原来散落在十几个后处理字段上的 mutable。
        mutable PostProcessState mPostProcess;

        // 见 getGlobalDistanceField() 的注释——render graph 构建期需要在 const CameraProxy&
        // 上修改 Layer 状态
        mutable GlobalDistanceField mGlobalDistanceField;

        // 见 getLastSceneView() 的注释。
        mutable std::optional<SceneView> mLastSceneView;
    };

    template <>
    struct TProxyTraits<ProxyType::Camera>
    {
        using Proxy = CameraProxy;
    };
}
