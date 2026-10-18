#pragma once
#include <Engine/Texture/RenderTarget.h>
#include <Renderer/RendererDefines.h>
#include <RenderBase/ProxyHandle.h>

#include <Core/Math/Vector.h>
#include <Core/Math/Matrix.h>
#include <Core/Object/ObjectDefines.h>

namespace hyp
{
    class Scene;
}

namespace hyp::ed
{
    // 编辑器自己的相机: 不挂在任何 Entity 上, 不经过 Scene 的 ECS/Component 生命周期,
    // 直接在渲染线程创建/销毁一个 CameraProxy 并注册进当前 Scene 的 SceneProxy——这样
    // 它能和场景内容一起被剔除、参与渲染, 但不受 Scene::PlayState 影响, 也不会出现在
    // Hierarchy 面板或存盘数据里(它本来就不是场景的一部分)。
    //
    // 只持有数据(位姿/投影参数/渲染目标), 不解释输入——输入由外部的 ICameraController
    // 驱动, 见 CameraController.h。
    class EditorCamera
    {
    public:
        EditorCamera();

        ~EditorCamera();

        // scene 为 nullptr 等价于 detach。切换场景(比如打开新工程)时调用。
        void attachToScene(Scene* scene);

        // capacityExtent 是 renderTarget 实际分配的纹理尺寸, 只在 renderTarget 本身被
        // (重新)创建时调用——和 setViewportExtent() 的可见尺寸是两个独立概念, 见 mExtent/
        // mCapacityExtent 的注释。
        void setRenderTarget(RenderTarget* renderTarget, uint2 capacityExtent);

        // 相机在 renderTarget 里实际使用/显示的尺寸, 可以小于等于 renderTarget 的容量——
        // 拖拽 SceneView 窗口时高频调用, 只更新 outputRect(渲染进容量纹理左上角对应的那一块
        // 子矩形)和投影矩阵的宽高比, 不会碰 renderTarget/TextureProxy, 避免每次拖拽都
        // 重建 GPU 资源。
        void setViewportExtent(uint2 extent) noexcept;

        void setPosition(double3 position) noexcept;

        double3 getPosition() const noexcept
        {
            return mPosition;
        }

        // forward/up 需要保持归一化 —— 由调用方(ICameraController)负责
        void setOrientation(float3 forward, float3 up) noexcept;

        float3 getForward() const noexcept
        {
            return mForward;
        }

        float3 getUp() const noexcept
        {
            return mUp;
        }

        void setPerspective(float fov, float nearPlane, float farPlane) noexcept;

        float getFov() const noexcept
        {
            return mFov;
        }

        float getNearPlane() const noexcept
        {
            return mNearPlane;
        }

        float getFarPlane() const noexcept
        {
            return mFarPlane;
        }

        // double 精度, 支持大世界坐标——和 CameraProxy::getViewMatrix() 用的是同一份换算
        // (glm::lookAt(eye, eye+forward, up), eye 为 double3), 不是先转 float 再拼一份近似值。
        const double4x4& getViewMatrix() const noexcept
        {
            return mViewMatrix;
        }

        const float4x4& getProjectionMatrix() const noexcept
        {
            return mProjectionMatrix;
        }

        CameraProxyHandle getProxyHandle() const noexcept
        {
            return mProxyHandle;
        }

        SceneProxyHandle getAttachedSceneProxyHandle() const noexcept
        {
            return mAttachedSceneProxyHandle;
        }

    private:
        void recomputeViewMatrix() noexcept;

        // 宽高比只由 mExtent(可见尺寸)决定, 和 mCapacityExtent 无关——否则窗口显示的
        // 画面比例会随着容量纹理的增长策略漂移, 而不是跟随实际可见区域。
        void recomputeProjectionMatrix() noexcept;

        void pushTransformToProxy() noexcept;

        void pushProjectionToProxy() noexcept;

        // mExtent 在 mCapacityExtent 这块容量纹理里占左上角的那一部分, 归一化成
        // CameraProxy::setOutputRect() 需要的 [0,1] 矩形。
        float4 computeOutputRect() const noexcept;

    private:
        Scene* mScene = nullptr;

        CameraProxyHandle mProxyHandle;
        bool mAttached = false;

        SceneProxyHandle mAttachedSceneProxyHandle;

        TRef<RenderTarget> mRenderTarget;

        double3 mPosition = double3(0.0, 0.0, 0.0);
        float3 mForward = float3(0.0f, 1.0f, 0.0f);
        float3 mUp = float3(0.0f, 0.0f, 1.0f);

        float mFov = 90.0f;
        float mNearPlane = 0.01f;
        float mFarPlane = 0.0f;

        // mExtent: 相机实际可见/渲染使用的尺寸(决定投影宽高比和 outputRect)。
        // mCapacityExtent: mRenderTarget 真正分配的纹理尺寸, 恒 >= mExtent——两者不相等时,
        // 相机只渲染进 mRenderTarget 左上角 mExtent 大小的一块子矩形, 由 SceneViewport
        // 按增长策略决定何时真正扩容(见 setRenderTarget()/setViewportExtent() 的分工)。
        uint2 mExtent = uint2(1, 1);
        uint2 mCapacityExtent = uint2(1, 1);

        double4x4 mViewMatrix;
        float4x4 mProjectionMatrix;
    };
}
