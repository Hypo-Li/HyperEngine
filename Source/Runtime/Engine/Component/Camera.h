#pragma once
#include "../Framework/Component.h"
#include "../Framework/Transform.h"
#include "../Texture/RenderTarget.h"

#include <Core/Math/Vector.h>
#include <RenderBase/ProxyHandle.h>
#include <Renderer/RendererDefines.h>

namespace hyp
{
    class Camera : public Component, public ITransformListener
    {
        REFLECT_CLASS(Camera)
    public:
        Camera() = default;

        virtual void onRegister() override;

        virtual void onUnregister() override;

        virtual void onTick(float deltaTime) override;

        virtual void onTransformUpdate(const Transform& transform) override;

        void setProjectionType(ProjectionType newType) noexcept;

        ProjectionType getProjectionType() const noexcept
        {
            return mProjectionType;
        }

        void setProjectionAxis(ProjectionAxis newAxis) noexcept;

        ProjectionAxis getProjectionAxis() const noexcept
        {
            return mProjectionAxis;
        }

        void setFov(float newFov) noexcept;

        float getFov() const noexcept
        {
            return mFov;
        }

        void setOrthoSize(float newSize) noexcept;

        float getOrthoSize() const noexcept
        {
            return mOrthoSize;
        }

        void setNearPlane(float newNearPlane) noexcept;

        float getNearPlane() const noexcept
        {
            return mNearPlane;
        }

        void setFarPlane(float newFarPlane) noexcept;

        float getFarPlane() const noexcept
        {
            return mFarPlane;
        }

        void setOutputRect(float4 newRect) noexcept;

        float4 getOutputRect() const noexcept
        {
            return mOutputRect;
        }

        void setCullingMask(uint32_t newMask) noexcept;

        uint32_t getCullingMask() const noexcept
        {
            return mCullingMask;
        }

        void setPriority(int newPriority) noexcept;

        int getPriority() const noexcept
        {
            return mPriority;
        }

        CameraProxyHandle getProxyHandle() const noexcept
        {
            return mProxyHandle;
        }

    private:
        void updateProxyProjectionMatrix();

    private:
        ProjectionType mProjectionType = ProjectionType::Perspective;
        ProjectionAxis mProjectionAxis = ProjectionAxis::Horizontal;

        float mFov = 90.0f;
        float mOrthoSize = 1.0f;
        float mNearPlane = 0.01f;
        float mFarPlane = 0.0;
        // x, y, width, height (0.0 ~ 1.0)
        float4 mOutputRect = float4(0.0f, 0.0f, 1.0f, 1.0f);

        uint32_t mCullingMask = ~0u;
        int mPriority = 0;

        TRef<RenderTarget> mRenderTarget;

        CameraProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<Camera>();
}
