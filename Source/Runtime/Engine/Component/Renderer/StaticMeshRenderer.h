#pragma once
#include "MeshRenderer.h"
#include "../../Mesh/StaticMesh.h"
#include "../../Framework/Scene.h"

namespace hyp
{
    class StaticMeshRenderer : public MeshRenderer, public ILumenStateListener
    {
        REFLECT_CLASS(StaticMeshRenderer)
    public:
        StaticMeshRenderer() = default;

        ~StaticMeshRenderer() = default;

        void onRegister() override;

        void onUnregister() override;

        void setMesh(StaticMesh* newMesh);

        StaticMesh* getMesh() const noexcept override
        {
            return mMesh.get();
        }

    private:
        // Lumen 开启时给这个实例的 Mesh 分配/挂上 SDF/Card 形状代理——只有 StaticMesh 需要,
        // MeshRenderer::onMeshChanged() 不管这部分, onRegister()/setMesh() 各自额外调一次
        void updateMeshShapeProxy();

        // ILumenStateListener——Scene::setLumenEnabled() 广播给所有已注册监听方, 用来补上
        // "onRegister() 时 Lumen 还没开, 之后才开"这种 updateMeshShapeProxy() 没机会跑的情况
        void onLumenEnabledChanged(bool enabled) override;

    private:
        TRef<StaticMesh> mMesh;
    };

    template <> Type* Reflection::createType<StaticMeshRenderer>();
}
