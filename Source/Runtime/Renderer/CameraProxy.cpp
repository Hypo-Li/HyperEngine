#include "CameraProxy.h"
#include "SceneProxy.h"

#include <RenderBase/RenderBackend.h>

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace hyp
{
    namespace
    {
        // Halton(2,3) 序列, TAA jitter 最常用的低差异序列组合。8 个样本循环一轮。
        static constexpr uint32_t kJitterSampleCount = 8;

        float haltonSequence(uint32_t index, uint32_t base)
        {
            float result = 0.0f;
            float f = 1.0f / float(base);
            while (index > 0)
            {
                result += f * float(index % base);
                index /= base;
                f /= float(base);
            }
            return result;
        }

        // UE SceneVisibility.cpp 里 r.TemporalAASamples=8(默认值)时的抖动生成方式: 不是直接把
        // Halton(2,3) 当像素偏移用(那是 UE 时域升采样/TSR 路径的做法), 而是把两个 Halton 序列
        // 当均匀随机数, 通过 Box-Muller 变换重塑成正态分布, 并按 [-0.5, 0.5] 窗口做概率意义上的
        // 截断——这个高斯核跟 TAA.slang 里当前帧重建滤波用的 sigma=0.47 高斯核是配套的一套东西,
        // 保证 jitter 落点的概率密度和重建滤波假设的一致, 而不是简单均匀分布。index 从 1 开始取
        // (index=0 时 Halton 序列恒为 (0,0), 没有抖动意义)。
        float2 nextJitterOffsetPixels(uint32_t index)
        {
            float u1 = haltonSequence(index + 1, 2);
            float u2 = haltonSequence(index + 1, 3);

            // 对应 UE r.TemporalAAFilterSize 默认值 1.0, 跟着重建滤波用的核宽度保持一致
            constexpr float kFilterSize = 1.0f;
            constexpr float kSigma = 0.47f * kFilterSize;
            constexpr float kOutWindow = 0.5f;
            const float inWindow = std::exp(-0.5f * (kOutWindow / kSigma) * (kOutWindow / kSigma));

            constexpr float kPi = 3.14159265358979323846f;
            float theta = 2.0f * kPi * u2;
            float r = kSigma * std::sqrt(-2.0f * std::log((1.0f - u1) * inWindow + u1));

            return float2(r * std::cos(theta), r * std::sin(theta));
        }
    }

    CameraProxy::CameraProxy(SceneProxy& sceneProxy, const Desc& desc) :
        SceneElementProxy(sceneProxy, desc.visible),
        mViewId(desc.viewId),
        mOutputRect(desc.outputRect),
        mRenderTargetInfo(desc.renderTarget, desc.renderTargetName, desc.renderTargetExtent, desc.outputRouting),
        mPostProcess(getRenderBackend(), desc.viewId, desc.postProcessSettings),
        mGlobalDistanceField(getRenderBackend())
    {
        setViewMatrix(desc.eye, desc.forward, desc.up);
        if (desc.projectionType == ProjectionType::Perspective)
            setPerspectiveParameters(desc.projectionAxis, desc.fov, desc.nearPlane, desc.farPlane);
        else
            setOrthographicParameters(desc.projectionAxis, desc.orthoSize, desc.nearPlane, desc.farPlane);

        sceneProxy.registerElement(this);
    }

    CameraProxy::~CameraProxy() noexcept
    {
        getSceneProxy().unregisterElement(this);
    }

    void CameraProxy::setViewMatrix(const double3& eye, float3 forward, float3 up)
    {
        double4x4 newViewMatrix = glm::lookAt(eye, eye + double3(forward), double3(up));
        // 第一次初始化让 previous=current，避免出生帧速度；后续 setter 只改 current。
        // previous 按渲染帧推进，而不是按 setter 调用次数推进。
        if (!mViewMatrixInitialized)
            mPrevViewMatrix = newViewMatrix;
        mViewMatrix = newViewMatrix;
        mViewMatrixInitialized = true;
        mWorldPosition = eye;
    }

    void CameraProxy::setPerspectiveParameters(ProjectionAxis axis, float fov, float nearPlane, float farPlane)
    {
        const bool projectionCut = mProjectionMatrixInitialized && (
            mProjectionType != ProjectionType::Perspective ||
            mProjectionAxis != axis ||
            std::abs(mFov - fov) > 5.0f);
        if (projectionCut)
            markCameraCut();

        mProjectionType = ProjectionType::Perspective;
        mProjectionAxis = axis;
        mFov = fov;
        mNearPlane = nearPlane;
        mFarPlane = farPlane;
        recomputeProjectionMatrix();
    }

    void CameraProxy::setOrthographicParameters(ProjectionAxis axis, float orthoSize, float nearPlane, float farPlane)
    {
        const float oldSize = std::max(std::abs(mOrthoSize), 0.0001f);
        const bool projectionCut = mProjectionMatrixInitialized && (
            mProjectionType != ProjectionType::Orthographic ||
            mProjectionAxis != axis ||
            std::abs(orthoSize - mOrthoSize) / oldSize > 0.25f);
        if (projectionCut)
            markCameraCut();

        mProjectionType = ProjectionType::Orthographic;
        mProjectionAxis = axis;
        mOrthoSize = orthoSize;
        mNearPlane = nearPlane;
        mFarPlane = farPlane;
        recomputeProjectionMatrix();
    }

    void CameraProxy::setOutputRect(const float4& rect) noexcept
    {
        if (mOutputRect.x != rect.x || mOutputRect.y != rect.y ||
            mOutputRect.z != rect.z || mOutputRect.w != rect.w)
        {
            markCameraCut();
        }
        // mOutputRect 和 mRenderTargetInfo.extent 共同决定 getAspectRatio()——只改这个字段
        // 不重算投影矩阵的话, SceneViewport 在容量纹理内部调整可见尺寸(不触发
        // setRenderTargetExtent())时, mProjectionMatrix 会一直停留在上一次容量变化时的
        // 宽高比, 画面比例和实际可见区域对不上。
        mOutputRect = rect;
        recomputeProjectionMatrix();
    }

    void CameraProxy::setCullingMask(uint32_t mask) noexcept
    {
        mCullingMask = mask;
    }

    void CameraProxy::setRenderTargetName(HString name) noexcept
    {
        mRenderTargetInfo.name = name;
    }

    void CameraProxy::setRenderTargetExtent(uint2 extent) noexcept
    {
        if (mRenderTargetInfo.extent == extent)
            return;

        mRenderTargetInfo.extent = extent;
        recomputeProjectionMatrix();
    }

    void CameraProxy::setRenderTarget(TextureProxy* texture, HString name, uint2 extent, OutputRouting outputRouting) noexcept
    {
        mRenderTargetInfo.texture = texture;
        mRenderTargetInfo.name = name;
        mRenderTargetInfo.outputRouting = outputRouting;
        setRenderTargetExtent(extent);
    }

    float4 CameraProxy::computeViewportPixels() const noexcept
    {
        // viewport 必须落在整数像素上: 下游会拿它去建渲染目标纹理(GBufferPass 那边直接
        // uint32_t(viewport.zw)), 也会作为 ViewData.resolution 给 shader 做
        // svPosition.xy / resolution 这类归一化。mOutputRect 是归一化比例, 由调用方用
        // "像素尺寸 / 容量尺寸"算出来(见 EditorCamera::computeOutputRect), 再乘回容量尺寸
        // 时 float 往返不保证回到原来的整数——比如 803 会变成 802.99994, 建纹理时截断成 802,
        // 而光栅化 viewport 仍是 802.99994, 两套坐标系差了 802/803, 到屏幕下半部分就累积成
        // 整整一行的采样偏移(TAA 这种按整数像素索引的 pass 表现为下半屏错位发虚)。
        //
        // 先把左/上和右/下边界各自 round 成整数再相减, 而不是直接 round 宽高: 这样相邻子矩形
        // 共享的那条边界取到的是同一个整数, 将来真用起带偏移的分屏时不会出现缝隙或重叠。
        const float left = std::round(mRenderTargetInfo.extent.x * mOutputRect.x);
        const float top = std::round(mRenderTargetInfo.extent.y * mOutputRect.y);
        const float right = std::round(mRenderTargetInfo.extent.x * (mOutputRect.x + mOutputRect.z));
        const float bottom = std::round(mRenderTargetInfo.extent.y * (mOutputRect.y + mOutputRect.w));
        return float4(left, top, right - left, bottom - top);
    }

    bool CameraProxy::isViewportEmpty() const noexcept
    {
        const float4 viewport = computeViewportPixels();
        return viewport.z <= 0.0f || viewport.w <= 0.0f;
    }

    float CameraProxy::getAspectRatio() const noexcept
    {
        const float4 viewport = computeViewportPixels();
        return viewport.z / viewport.w;
    }

    void CameraProxy::recomputeProjectionMatrix() noexcept
    {
        float4x4 newProjectionMatrix = mProjectionType == ProjectionType::Perspective
            ? makePerspectiveMatrix(mProjectionAxis, mFov, getAspectRatio(), mNearPlane, mFarPlane)
            : makeOrthographicMatrix(mProjectionAxis, mOrthoSize, getAspectRatio(), mNearPlane, mFarPlane);

        // 第一次初始化让 previous=current；后续这里只更新 current。这样同一渲染帧内因为
        // viewport/extent/投影参数发生多次重算时，previous 仍然是上一实际录制帧的投影。
        if (!mProjectionMatrixInitialized)
            mPrevProjectionMatrix = newProjectionMatrix;
        mProjectionMatrix = newProjectionMatrix;
        mProjectionMatrixInitialized = true;
    }

    SceneViewGroup CameraProxy::getSceneViewGroup(uint32_t frameIndex) const noexcept
    {
        const PostProcessState::PreExposureResult preExposure = mPostProcess.updatePreExposure(frameIndex);

        float4 viewport = computeViewportPixels();

        float4x4 jitteredProjectionMatrix = mProjectionMatrix;
        float2 jitterOffsetNDC(0.0f, 0.0f);
        if (mPostProcess.isJitterEnabled() && viewport.z > 0.0f && viewport.w > 0.0f)
        {
            uint32_t jitterIndex = frameIndex % kJitterSampleCount;
            float2 jitterPixels = nextJitterOffsetPixels(jitterIndex);

            jitterOffsetNDC = float2(
                jitterPixels.x * 2.0f / viewport.z,
                jitterPixels.y * 2.0f / viewport.w
            );

            if (mProjectionType == ProjectionType::Perspective)
            {
                jitteredProjectionMatrix[2][0] -= jitterOffsetNDC.x;
                jitteredProjectionMatrix[2][1] -= jitterOffsetNDC.y;
            }
            else
            {
                jitteredProjectionMatrix[3][0] += jitterOffsetNDC.x;
                jitteredProjectionMatrix[3][1] += jitterOffsetNDC.y;
            }
        }

        SceneViewGroup result(
            SceneViewGroup::Type::Camera,
            {
                SceneView(
                    mViewId,
                    mCullingMask,
                    mRenderMask,
                    mViewMatrix,
                    jitteredProjectionMatrix,
                    viewport,
                    mRenderTargetInfo.name,
                    mRenderTargetInfo.outputRouting,
                    jitterOffsetNDC,
                    mCameraCutPending ? mViewMatrix : mPrevViewMatrix,
                    mCameraCutPending ? mProjectionMatrix : mPrevProjectionMatrix,
                    preExposure.current,
                    preExposure.previous
                )
            },
            this
        );

        mPrevViewMatrix = mViewMatrix;
        mPrevProjectionMatrix = mProjectionMatrix;
        mCameraCutPending = false;

        mLastSceneView = result.getSceneViews()[0];

        return result;
    }

    void CameraProxy::setPostProcessSettings(const PostProcessSettings& settings) noexcept
    {
        mPostProcess.setPostProcessSettings(settings);
    }

    float4x4 CameraProxy::makePerspectiveMatrix(ProjectionAxis fovAxis, float fov, float aspectRatio, float nearPlane, float farPlane)
    {
        float4x4 result(0);

        float halfRad = glm::radians(fov * 0.5f);
        float tanHalfVFov = std::tan(halfRad);
        if (fovAxis == ProjectionAxis::Horizontal)
        {
            tanHalfVFov /= aspectRatio;
        }

        const float invTanHalfVFov = 1.0f / tanHalfVFov;
        result[0][0] = invTanHalfVFov / aspectRatio;
        result[1][1] = invTanHalfVFov;
        result[2][3] = -1;
        // Reversed Z
        if (farPlane == 0)
        {
            result[2][2] = 0;
            result[3][2] = nearPlane;
        }
        else
        {
            result[2][2] = nearPlane / (farPlane - nearPlane);
            result[3][2] = (nearPlane * farPlane) / (farPlane - nearPlane);
        }

        return result;
    }

    float4x4 CameraProxy::makeOrthographicMatrix(ProjectionAxis orthoAxis, float orthoSize, float aspectRatio, float nearPlane, float farPlane)
    {
        float4x4 result(0);
        const float halfHeight = orthoAxis == ProjectionAxis::Vertical ? orthoSize : orthoSize / aspectRatio;
        result[0][0] = 1.0f / (halfHeight * aspectRatio);
        result[1][1] = 1.0f / halfHeight;
        result[2][2] = 1.0f / (farPlane - nearPlane);
        result[3][0] = 0.0f;
        result[3][1] = 0.0f;
        result[3][2] = farPlane / (farPlane - nearPlane);
        result[3][3] = 1;

        return result;
    }

}
