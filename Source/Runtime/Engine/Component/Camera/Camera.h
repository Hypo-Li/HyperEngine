#pragma once
#include "../../Framework/Component.h"
#include "../../Framework/Transform.h"
#include "../../Texture/RenderTarget.h"
#include "../../Texture/Texture3D.h"

#include <Core/Math/Vector.h>
#include <RenderBase/ProxyHandle.h>
#include <Renderer/RendererDefines.h>
#include <Renderer/PostProcessSettings.h>

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

        // mRenderTarget == nullptr 代表这个相机想画在"最终呈现的位置"上(独立运行时是
        // 硬件 swapchain, 编辑器里是当前接管的那个视口), mPriority 只决定同时有多个这样
        // 的相机时的叠加顺序, 不代表谁是"主相机"。运行期改绑用于编辑器在编辑/运行模式间
        // 切换时, 把这个相机的输出临时接到某个 SceneView 视口自己的 RenderTarget 上。
        void setRenderTarget(RenderTarget* renderTarget) noexcept;

        RenderTarget* getRenderTarget() const noexcept
        {
            return mRenderTarget.get();
        }

        PostProcessSettings& getPostProcessSettings() noexcept
        {
            return mPostProcessSettings;
        }

        const PostProcessSettings& getPostProcessSettings() const noexcept
        {
            return mPostProcessSettings;
        }

        void updatePostProcessSettings();

        // Call after teleporting the camera, switching scenes, loading a save,
        // or otherwise replacing the visible world discontinuously.
        void markCameraCut() noexcept;

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

        PostProcessSettings mPostProcessSettings;

        CameraProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<Camera>();
}
