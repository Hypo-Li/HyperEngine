#include "Camera.h"
#include "../Framework/Scene.h"
#include "../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/RendererContext.h>
#include <Renderer/SceneView.h>
#include <Renderer/CameraProxy.h>
#include <Renderer/SceneProxy.h>

namespace hyp
{
    REGISTER_CLASS(Camera)

    void Camera::onRegister()
    {
        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        // Create Camera Proxy
        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Camera>();
        TextureProxyHandle renderTargetProxyHandle;
        HString renderTargetName = gDefaultRTName;
        uint2 renderTargetExtent = uint2(0, 0);
        if (mRenderTarget)
        {
            renderTargetProxyHandle = mRenderTarget->getOrCreateTextureProxy();
            renderTargetName = mRenderTarget->getName();
            auto& description = mRenderTarget->getDescription();
            renderTargetExtent = uint2(description.width, description.height);
        }

        Transform& transform = getEntity()->getTransform();
        double3 eye = transform.getWorldPosition();
        float3 forward = transform.getWorldForwardVector();
        float3 up = transform.getWorldUpVector();

        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle,
            sceneProxyHandle,
            outputRect = mOutputRect,
            renderTargetProxyHandle,
            renderTargetName,
            renderTargetExtent,
            eye, forward, up,
            projectionType = mProjectionType,
            projectionAxis = mProjectionAxis,
            fov = mFov, orthoSize = mOrthoSize,
            nearPlane = mNearPlane, farPlane = mFarPlane](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                TextureProxy* renderTargetProxy = nullptr;
                OutputRouting outputRouting = OutputRouting::Direct_Write;
                if (renderTargetProxyHandle.isValid())
                {
                    renderTargetProxy = proxyManager.getProxy(renderTargetProxyHandle);
                    outputRouting = OutputRouting::Copy_Resolve;
                }
                
                auto& rendererContext = Context::get().getRendererContext();
                uint32_t viewId = rendererContext.getViewDataManager().acquireViewId();
                CameraProxy* cameraProxy = proxyManager.createProxy(
                    proxyHandle,
                    viewId,
                    outputRect,
                    renderTargetProxy,
                    renderTargetName,
                    renderTargetExtent,
                    outputRouting
                );

                cameraProxy->setViewMatrix(eye, forward, up);
                if (projectionType == ProjectionType::Perspective)
                {
                    cameraProxy->setPerspectiveParameters(projectionAxis, fov, nearPlane, farPlane);
                }
                else
                {
                    cameraProxy->setOrthographicParameters(projectionAxis, orthoSize, nearPlane, farPlane);
                }

                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                sceneProxy->addCameraProxy(cameraProxy);
            }
        );

        scene->registerTransformListener(&getEntity()->getTransform(), this);
    }

    void Camera::onUnregister()
    {
        Scene* scene = getScene();
        scene->unregisterTransformListener(&getEntity()->getTransform(), this);

        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle,
            sceneProxyHandle](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                CameraProxy* cameraProxy = proxyManager.getProxy(proxyHandle);
                auto& rendererContext = Context::get().getRendererContext();
                rendererContext.getViewDataManager().releaseViewId(cameraProxy->getViewId());

                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                sceneProxy->removeCameraProxy(cameraProxy);
                proxyManager.destroyProxy(proxyHandle);
            }
        );

        renderFrontend.freeProxyHandle(mProxyHandle);
    }

    void Camera::onTick(float deltaTime)
    {
    }

    void Camera::onTransformUpdate(const Transform& transform)
    {
        double3 eye = transform.getWorldPosition();
        float3 forward = transform.getWorldForwardVector();
        float3 up = transform.getWorldUpVector();

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            eye, forward, up](FrameContext& context)
            {
                CameraProxy* cameraProxy = context.getProxyManager().getProxy(proxyHandle);
                cameraProxy->setViewMatrix(eye, forward, up);
            }
        );
    }

    void Camera::setProjectionType(ProjectionType newType) noexcept
    {
        mProjectionType = newType;
        updateProxyProjectionMatrix();
    }

    void Camera::setProjectionAxis(ProjectionAxis newAxis) noexcept
    {
        if (mProjectionAxis == newAxis)
            return;

        mProjectionAxis = newAxis;
        updateProxyProjectionMatrix();
    }

    void Camera::setFov(float newFov) noexcept
    {
        mFov = newFov;
        if (mProjectionType == ProjectionType::Perspective)
        {
            updateProxyProjectionMatrix();
        }
    }

    void Camera::setOrthoSize(float newSize) noexcept
    {
        mOrthoSize = newSize;
        if (mProjectionType == ProjectionType::Orthographic)
        {
            updateProxyProjectionMatrix();
        }
    }

    void Camera::setNearPlane(float newNearPlane) noexcept
    {
        mNearPlane = newNearPlane;
        updateProxyProjectionMatrix();
    }

    void Camera::setFarPlane(float newFarPlane) noexcept
    {
        mFarPlane = newFarPlane;
        updateProxyProjectionMatrix();
    }

    void Camera::setOutputRect(float4 newRect) noexcept
    {
        mOutputRect = newRect;
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle,
            rect = mOutputRect](FrameContext& context)
            {
                CameraProxy* proxy = context.getProxyManager().getProxy(proxyHandle);
                proxy->setOutputRect(rect);
            }
        );
    }

    void Camera::setCullingMask(uint32_t newMask) noexcept
    {
        mCullingMask = newMask;
    }

    void Camera::setPriority(int newPriority) noexcept
    {
        mPriority = newPriority;
    }

    void Camera::updateProxyProjectionMatrix()
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle,
            projectionType = mProjectionType,
            projectionAxis = mProjectionAxis,
            fov = mFov,
            orthoSize = mOrthoSize,
            nearPlane = mNearPlane,
            farPlane = mFarPlane](FrameContext& context)
            {
                CameraProxy* proxy = context.getProxyManager().getProxy(proxyHandle);
                if (projectionType == ProjectionType::Perspective)
                {
                    proxy->setPerspectiveParameters(projectionAxis, fov, nearPlane, farPlane);
                }
                else
                {
                    proxy->setOrthographicParameters(projectionAxis, orthoSize, nearPlane, farPlane);
                }
            }
        );
    }

    template <> Type* Reflection::createType<Camera>()
    {
        auto c = new TClass<Camera, Component>("Camera");
        /*c->addProperty("Projection Type", &Camera::mProjectionType);
        c->addProperty("Projection Axis", &Camera::mProjectionAxis);
        c->addProperty("Fov", &Camera::mFov);
        c->addProperty("Ortho Size", &Camera::mOrthoSize);
        c->addProperty("Near Plane", &Camera::mNearPlane);
        c->addProperty("Far Plane", &Camera::mFarPlane);*/
        return c;
    }
}
