#pragma once
#include <optional>
#include <variant>

#include <Core/Math/Vector.h>
#include <Core/Math/Matrix.h>
#include <Core/Math/Frustum.h>
#include <RenderBase/RenderBackend.h>
#include <RHI/Buffer.h>
#include <RHI/CommandList.h>

namespace hyp
{
    extern const HString gDefaultRTName;

    struct ViewData
    {
        float3x4 worldToViewRot;
        WorldCoord cameraOrigin;
        // 当时域抗锯齿开启时带 jitter
        float4x4 viewToClip;
        float4x4 clipToView;
        // 任何时候都不带 jitter
        float4x4 viewToClipUnjittered;
        float4x4 clipToViewUnjittered;
        float4 viewport;
        float4 resolution;
        float4 jitterOffset;
        float3x4 prevWorldToViewRot;
        WorldCoord prevCameraOrigin;
        float4x4 prevViewToClipUnjittered;
        float4x4 prevClipToViewUnjittered;
        float preExposure;
        float oneOverPreExposure;
        float prevPreExposure;
        float padding0;
    };

    enum class OutputRouting
    {
        Direct_Write,
        Copy_Resolve,
    };

    class ViewDataManager;
    class SceneView
    {
    public:
        SceneView(
            uint32_t viewId,
            uint32_t cullingMask,
            uint32_t renderMask,
            const double4x4& viewMatrix,
            const float4x4& projectionMatrix,
            float4 viewport,
            HString finalRTName,
            OutputRouting outputRouting,
            float2 jitterOffset = float2(0.0f, 0.0f),
            const std::optional<double4x4>& prevViewMatrix = std::nullopt,
            const std::optional<float4x4>& prevProjectionMatrix = std::nullopt,
            float preExposure = 1.0f,
            float prevPreExposure = 1.0f
        );

        SceneView(
            uint32_t viewId,
            uint32_t cullingMask,
            uint32_t renderMask,
            const double4x4& viewMatrix,
            const float4x4& projectionMatrix,
            const Frustumd& cullingFrustum,
            float4 viewport,
            HString finalRTName,
            OutputRouting outputRouting
        );

        uint32_t getViewId() const noexcept
        {
            return mViewId;
        }

        uint32_t getCullingMask() const noexcept
        {
            return mCullingMask;
        }

        uint32_t getRenderMask() const noexcept
        {
            return mRenderMask;
        }

        const double3& getCameraOrigin() const noexcept
        {
            return mCameraOrigin;
        }

        const Frustumd& getFrustum() const noexcept
        {
            return mFrustum;
        }

        float4 getViewport() const noexcept
        {
            return mViewport;
        }

        HString getFinalRTName() const noexcept
        {
            return mFinalRTName;
        }

        OutputRouting getOutputRouting() const noexcept
        {
            return mOutputRouting;
        }

        ViewData makeViewData() const noexcept;

        float getPreExposure() const noexcept
        {
            return mPreExposure;
        }

    private:
        uint32_t mViewId;
        uint32_t mCullingMask;
        uint32_t mRenderMask;

        double3 mCameraOrigin;
        float3x3 mWorldToViewRot;
        float4x4 mProjectionMatrix;
        Frustumd mFrustum;

        float4 mViewport;
        HString mFinalRTName;
        OutputRouting mOutputRouting;
        float2 mJitterOffset;

        double3 mPrevCameraOrigin;
        float3x3 mPrevWorldToViewRot;
        float4x4 mPrevProjectionMatrix;
        float mPreExposure;
        float mPrevPreExposure;
        float4x4 mViewToClip;
    };

    class CameraProxy;
    class DirectionalLightProxy;

    using CameraViewSource = const CameraProxy*;

    struct CascadeShadowViewSource
    {
        const DirectionalLightProxy* directionalLight;
        const CameraProxy* camera;
    };

    class SceneViewGroup
    {
    public:
        enum class Type
        {
            Camera,
            Cascade_Shadow,
        };

        using ViewSource = std::variant<
            CameraViewSource,
            CascadeShadowViewSource
        >;

        SceneViewGroup(Type type, std::vector<SceneView>&& sceneViews, ViewSource viewSource);

        SceneViewGroup(const SceneViewGroup&) = delete;
        SceneViewGroup& operator=(const SceneViewGroup&) = delete;

        SceneViewGroup(SceneViewGroup&& other) noexcept;

        SceneViewGroup& operator=(SceneViewGroup&& other) noexcept;

        Type getType() const noexcept
        {
            return mType;
        }

        std::span<const SceneView> getSceneViews() const noexcept
        {
            return mSceneViews;
        }

        template <typename T>
        const T getViewSource() const
        {
            return std::get<T>(mViewSource);
        }

    private:
        Type mType;
        std::vector<SceneView> mSceneViews;
        ViewSource mViewSource;
    };

    class ViewDataManager
    {
    public:
        ViewDataManager(RenderBackend& renderBackend);

        ~ViewDataManager() = default;

        static TRef<rhi::BindGroupLayout> createBindGroupLayout(BindGroupLayoutCache& cache);

        const rhi::BindGroupLayout& getBindGroupLayout() const noexcept
        {
            return *mBindGroupLayout;
        }

        const rhi::BindGroup& getBindGroup() const noexcept
        {
            return *mBindGroup;
        }

        uint32_t getViewDataAlignedSize() const noexcept
        {
            return mViewDataAlignedSize;
        }

        rhi::Buffer& getViewDataBuffer() const noexcept
        {
            return *mViewDataBuffer;
        }

        uint32_t getViewDataDynamicOffset(uint32_t viewId) const noexcept
        {
            return viewId * mViewDataAlignedSize;
        }

        uint32_t acquireViewId();

        void releaseViewId(uint32_t viewId);

        void updateViewData(
            rhi::CommandList& cmdList,
            std::span<const uint32_t> viewIds,
            std::span<const ViewData> viewData
        );

    private:
        RenderBackend& mRenderBackend;
        uint32_t mViewDataAlignedSize = 0;
        TRef<rhi::Buffer> mViewDataBuffer;
        TRef<rhi::BindGroupLayout> mBindGroupLayout;
        TRef<rhi::BindGroup> mBindGroup;
        uint32_t mNextViewId = 0;
        std::vector<uint32_t> mFreeViewIds;
        static constexpr uint32_t kMaxViews = 64;
        static constexpr uint32_t kDynamicBufferOffsetAlignment = 256;
    };
}
