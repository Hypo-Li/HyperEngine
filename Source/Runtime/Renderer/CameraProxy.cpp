#include "CameraProxy.h"

#include <glm/gtc/matrix_transform.hpp>

namespace hyp
{
    CameraProxy::CameraProxy(
        uint32_t viewId,
        float4 outputRect,
        TextureProxy* renderTarget,
        HString renderTargetName,
        uint2 renderTargetExtent,
        OutputRouting outputRouting
    ) : mViewId(viewId),
        mOutputRect(outputRect),
        mRenderTargetInfo(renderTarget, renderTargetName, renderTargetExtent, outputRouting)
    {

    }

    CameraProxy::~CameraProxy() noexcept
    {
    }

    void CameraProxy::setViewMatrix(const double3& eye, float3 forward, float3 up)
    {
        mViewMatrix = glm::lookAt(eye, eye + double3(forward), double3(up));
    }

    void CameraProxy::setPerspectiveParameters(ProjectionAxis axis, float fov, float nearPlane, float farPlane)
    {
        mProjectionType = ProjectionType::Perspective;
        mProjectionAxis = axis;
        mFov = fov;
        mNearPlane = nearPlane;
        mFarPlane = farPlane;
        mProjectionMatrix = makePerspectiveMatrix(mProjectionAxis, mFov, getAspectRatio(), mNearPlane, mFarPlane);
    }

    void CameraProxy::setOrthographicParameters(ProjectionAxis axis, float orthoSize, float nearPlane, float farPlane)
    {
        mProjectionType = ProjectionType::Orthographic;
        mProjectionAxis = axis;
        mOrthoSize = orthoSize;
        mNearPlane = nearPlane;
        mFarPlane = farPlane;
        mProjectionMatrix = makeOrthographicMatrix(mProjectionAxis, mOrthoSize, getAspectRatio(), mNearPlane, mFarPlane);
    }

    void CameraProxy::setOutputRect(const float4& rect) noexcept
    {
        mOutputRect = rect;
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
        if (mProjectionType == ProjectionType::Perspective)
        {
            mProjectionMatrix = makePerspectiveMatrix(mProjectionAxis, mFov, getAspectRatio(), mNearPlane, mFarPlane);
        }
        else
        {
            mProjectionMatrix = makeOrthographicMatrix(mProjectionAxis, mOrthoSize, getAspectRatio(), mNearPlane, mFarPlane);
        }
    }

    float CameraProxy::getAspectRatio() const noexcept
    {
        float width = mRenderTargetInfo.extent.x * mOutputRect.z;
        float height = mRenderTargetInfo.extent.y * mOutputRect.w;
        return width / height;
    }

    SceneViewGroup CameraProxy::getSceneViewGroup() const noexcept
    {
        float4 viewport = float4(
            mRenderTargetInfo.extent.x * mOutputRect.x,
            mRenderTargetInfo.extent.y * mOutputRect.y,
            mRenderTargetInfo.extent.x * mOutputRect.z,
            mRenderTargetInfo.extent.y * mOutputRect.w
        );
        return SceneViewGroup(
            SceneViewGroup::Type::Camera,
            {
                SceneView(
                    mViewId,
                    mCullingMask,
                    mRenderMask,
                    mViewMatrix,
                    mProjectionMatrix,
                    viewport,
                    mRenderTargetInfo.name,
                    mRenderTargetInfo.outputRouting
                )
            },
            this
        );
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
