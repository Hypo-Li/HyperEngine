#include "EditorCamera.h"

#include <Engine/Framework/Scene.h>
#include <Engine/Context.h>

#include <RenderBase/RenderFrontend.h>
#include <Renderer/RendererContext.h>
#include <Renderer/SceneView.h>
#include <Renderer/CameraProxy.h>
#include <Renderer/SceneProxy.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cassert>

namespace hyp::ed
{
    EditorCamera::EditorCamera()
    {
        recomputeViewMatrix();
        recomputeProjectionMatrix();
    }

    EditorCamera::~EditorCamera()
    {
        attachToScene(nullptr);
    }

    void EditorCamera::attachToScene(Scene* scene)
    {
        if (scene == mScene)
            return;

        if (mAttached)
        {
            auto& renderFrontend = Context::get().getRenderFrontend();
            renderFrontend.enqueueLambda(
                [proxyHandle = mProxyHandle](FrameContext& context)
                {
                    auto& proxyManager = context.getProxyManager();
                    CameraProxy* cameraProxy = proxyManager.getProxy(proxyHandle);
                    auto& rendererContext = Context::get().getRendererContext();
                    rendererContext.getViewDataManager().releaseViewId(cameraProxy->getViewId());
                    proxyManager.destroyProxy(proxyHandle);
                }
            );
            renderFrontend.freeProxyHandle(mProxyHandle);
            mAttached = false;
        }

        mScene = scene;
        if (!mScene)
            return;

        // EditorCamera 直接呈现到自己的 RenderTarget, 必须先 setRenderTarget()
        // 再 attachToScene() —— 不然渲染图里 view.getFinalRTName() 对应的 blackboard
        // 条目根本没被导入(见 Scene.cpp 只在 cameraProxy->getRenderTarget() 非空时才
        // rg.importTexture()), 输出 pass 取不到目标纹理。
        assert(mRenderTarget && "EditorCamera::setRenderTarget() must be called before attachToScene()");

        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Camera>();

        SceneProxyHandle sceneProxyHandle = mScene->getOrCreateSceneProxy();
        mAttachedSceneProxyHandle = sceneProxyHandle;

        TextureProxyHandle renderTargetProxyHandle = mRenderTarget->getOrCreateTextureProxy();
        HString renderTargetName = mRenderTarget->getName();
        uint2 renderTargetExtent = mCapacityExtent;

        float4 outputRect = computeOutputRect();
        double3 eye = mPosition;
        float3 forward = mForward;
        float3 up = mUp;
        float fov = mFov, nearPlane = mNearPlane, farPlane = mFarPlane;

        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle, sceneProxyHandle, outputRect,
            renderTargetProxyHandle, renderTargetName, renderTargetExtent,
            eye, forward, up, fov, nearPlane, farPlane](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                TextureProxy* renderTargetProxy = proxyManager.getProxy(renderTargetProxyHandle);

                auto& rendererContext = Context::get().getRendererContext();
                uint32_t viewId = rendererContext.getViewDataManager().acquireViewId();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                proxyManager.createProxy(proxyHandle, *sceneProxy, CameraProxy::Desc{
                    .viewId = viewId,
                    .outputRect = outputRect,
                    .renderTarget = renderTargetProxy,
                    .renderTargetName = renderTargetName,
                    .renderTargetExtent = renderTargetExtent,
                    .outputRouting = OutputRouting::Direct_Write,
                    .postProcessSettings = PostProcessSettings{},
                    .eye = eye,
                    .forward = forward,
                    .up = up,
                    .projectionType = ProjectionType::Perspective,
                    .projectionAxis = ProjectionAxis::Horizontal,
                    .fov = fov,
                    .orthoSize = 0.0f,
                    .nearPlane = nearPlane,
                    .farPlane = farPlane
                });
            }
        );

        mAttached = true;
    }

    void EditorCamera::setRenderTarget(RenderTarget* renderTarget, uint2 capacityExtent)
    {
        mRenderTarget = renderTarget;
        mCapacityExtent = capacityExtent;
        // 宽高比只由 mExtent 决定, 容量变化不影响投影矩阵——这里不调用
        // recomputeProjectionMatrix()。

        if (!mAttached)
            return;

        TextureProxyHandle renderTargetProxyHandle;
        HString renderTargetName;
        if (mRenderTarget)
        {
            renderTargetProxyHandle = mRenderTarget->getOrCreateTextureProxy();
            renderTargetName = mRenderTarget->getName();
        }

        float4 outputRect = computeOutputRect();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, renderTargetProxyHandle, renderTargetName, capacityExtent, outputRect](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                TextureProxy* renderTargetProxy = renderTargetProxyHandle.isValid() ? proxyManager.getProxy(renderTargetProxyHandle) : nullptr;
                CameraProxy* cameraProxy = proxyManager.getProxy(proxyHandle);
                cameraProxy->setRenderTarget(renderTargetProxy, renderTargetName, capacityExtent, OutputRouting::Direct_Write);
                cameraProxy->setOutputRect(outputRect);
            }
        );
    }

    void EditorCamera::setViewportExtent(uint2 extent) noexcept
    {
        if (mExtent == extent)
            return;

        mExtent = extent;
        recomputeProjectionMatrix();

        if (!mAttached)
            return;

        float4 outputRect = computeOutputRect();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, outputRect](FrameContext& context)
            {
                CameraProxy* cameraProxy = context.getProxyManager().getProxy(proxyHandle);
                cameraProxy->setOutputRect(outputRect);
            }
        );
    }

    float4 EditorCamera::computeOutputRect() const noexcept
    {
        return float4(
            0.0f, 0.0f,
            static_cast<float>(mExtent.x) / static_cast<float>(mCapacityExtent.x),
            static_cast<float>(mExtent.y) / static_cast<float>(mCapacityExtent.y)
        );
    }

    void EditorCamera::setPosition(double3 position) noexcept
    {
        mPosition = position;
        recomputeViewMatrix();
        pushTransformToProxy();
    }

    void EditorCamera::setOrientation(float3 forward, float3 up) noexcept
    {
        mForward = forward;
        mUp = up;
        recomputeViewMatrix();
        pushTransformToProxy();
    }

    void EditorCamera::setPerspective(float fov, float nearPlane, float farPlane) noexcept
    {
        mFov = fov;
        mNearPlane = nearPlane;
        mFarPlane = farPlane;
        recomputeProjectionMatrix();
        pushProjectionToProxy();
    }

    void EditorCamera::recomputeViewMatrix() noexcept
    {
        mViewMatrix = glm::lookAt(mPosition, mPosition + double3(mForward), double3(mUp));
    }

    void EditorCamera::recomputeProjectionMatrix() noexcept
    {
        float aspectRatio = static_cast<float>(mExtent.x) / static_cast<float>(mExtent.y);
        mProjectionMatrix = CameraProxy::makePerspectiveMatrix(ProjectionAxis::Horizontal, mFov, aspectRatio, mNearPlane, mFarPlane);
    }

    void EditorCamera::pushTransformToProxy() noexcept
    {
        if (!mAttached)
            return;

        double3 eye = mPosition;
        float3 forward = mForward;
        float3 up = mUp;
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, eye, forward, up](FrameContext& context)
            {
                CameraProxy* cameraProxy = context.getProxyManager().getProxy(proxyHandle);
                cameraProxy->setViewMatrix(eye, forward, up);
            }
        );
    }

    void EditorCamera::pushProjectionToProxy() noexcept
    {
        if (!mAttached)
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, fov = mFov, nearPlane = mNearPlane, farPlane = mFarPlane](FrameContext& context)
            {
                CameraProxy* cameraProxy = context.getProxyManager().getProxy(proxyHandle);
                cameraProxy->setPerspectiveParameters(ProjectionAxis::Horizontal, fov, nearPlane, farPlane);
            }
        );
    }
}
