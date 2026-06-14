#pragma once
#include <variant>

#include <Core/Math/Vector.h>
#include <Core/Math/Matrix.h>
#include <Core/Math/Frustum.h>
#include <RenderBase/Vulkan/Buffer.h>
#include <RenderBase/Vulkan/CommandBuffer.h>
#include <RenderBase/RenderBaseDefines.h>
#include <RenderBase/RenderBackend.h>

namespace hyp
{
    extern const HString gDefaultRTName;

    struct ViewData
    {
        float3x4 worldToViewRot;
        WorldCoord cameraOrigin;
        float4x4 viewToClip;
        float4x4 clipToView;
        float4 viewport;
        float4 resolution;
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

        ViewData makeViewData() const noexcept;

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
    };

    class CameraProxy;
    class DirectionalLightProxy;

    class SceneViewGroup
    {
    public:
        enum class Type
        {
            Camera,
            Cascade_Shadow,
        };

        using Source = std::variant<
            std::monostate,
            const CameraProxy*,
            const DirectionalLightProxy*
        >;

        SceneViewGroup(Type type, std::vector<SceneView>&& sceneViews, Source source = std::monostate{}) :
            mType(type),
            mSceneViews(std::move(sceneViews)),
            mSource(source)
        {
        }

        SceneViewGroup(const SceneViewGroup&) = delete;
        SceneViewGroup& operator=(const SceneViewGroup&) = delete;

        SceneViewGroup(SceneViewGroup&& other) noexcept :
            mType(other.mType),
            mSceneViews(std::move(other.mSceneViews)),
            mSource(other.mSource)
        {
        }

        SceneViewGroup& operator=(SceneViewGroup&& other) noexcept
        {
            std::swap(mType, other.mType);
            std::swap(mSceneViews, other.mSceneViews);
            std::swap(mSource, other.mSource);
            return *this;
        }

        Type getType() const noexcept
        {
            return mType;
        }

        std::span<const SceneView> getSceneViews() const noexcept
        {
            return mSceneViews;
        }

        template <typename T>
        const T* getSource() const
        {
            if (const auto* ptr = std::get_if<const T*>(&mSource))
                return *ptr;
            return nullptr;
        }

    private:
        Type mType;
        std::vector<SceneView> mSceneViews;
        Source mSource;
    };

    class ViewDataManager
    {
    public:
        ViewDataManager(RenderBackend& renderBackend);

        uint32_t getViewDataAlignedSize() const noexcept
        {
            return mViewDataAlignedSize;
        }

        vk::Buffer& getViewDataBuffer() const noexcept
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
            vk::CommandBuffer& cmd,
            std::span<const uint32_t> viewIds,
            std::span<const ViewData> viewData
        );

    private:
        RenderBackend& mRenderBackend;
        uint32_t mViewDataAlignedSize = 0;
        TRef<vk::Buffer> mViewDataBuffer;
        uint32_t mNextViewId = 0;
        std::vector<uint32_t> mFreeViewIds;
        static constexpr uint32_t kMaxViews = 64;
    };
}
