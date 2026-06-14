#pragma once
#include <RenderBase/RenderProxy.h>
#include <RenderBase/TextureProxy.h>
#include "SceneView.h"
#include "RendererDefines.h"

namespace hyp
{
    class CameraProxy : public RenderProxy
    {
    public:
        CameraProxy(
            uint32_t viewId,
            float4 outputRect,
            TextureProxy* renderTarget,
            HString renderTargetName,
            uint2 renderTargetExtent,
            OutputRouting outputRouting
        );

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

        void setPerspectiveParameters(ProjectionAxis axis, float fov, float nearPlane, float farPlane);

        void setOrthographicParameters(ProjectionAxis axis, float orthoSize, float nearPlane, float farPlane);

        const float4x4& getProjectionMatrix() const noexcept
        {
            return mProjectionMatrix;
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

        float getAspectRatio() const noexcept;

        SceneViewGroup getSceneViewGroup() const noexcept;

    private:
        float4x4 makePerspectiveMatrix(ProjectionAxis fovAxis, float fov, float aspectRatio, float nearPlane, float farPlane);

        float4x4 makeOrthographicMatrix(ProjectionAxis orthoAxis, float orthoSize, float aspectRatio, float nearPlane, float farPlane);

    private:
        uint32_t mViewId;
        uint32_t mCullingMask = ~0u;
        uint32_t mRenderMask = ~0u;

        double4x4 mViewMatrix;
        ProjectionType mProjectionType;
        ProjectionAxis mProjectionAxis;
        float mFov;
        float mOrthoSize;
        float mNearPlane;
        float mFarPlane;
        float4x4 mProjectionMatrix;

        float4 mOutputRect;
        struct RenderTargetInfo
        {
            TextureProxy* texture;
            HString name;
            uint2 extent;
            OutputRouting outputRouting;
        };
        RenderTargetInfo mRenderTargetInfo;
    };

    template <>
    struct TProxyTraits<ProxyType::Camera>
    {
        using Proxy = CameraProxy;
    };
}
