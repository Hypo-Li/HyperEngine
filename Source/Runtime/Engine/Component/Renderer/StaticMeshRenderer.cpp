#include "StaticMeshRenderer.h"
#include "../../Framework/Entity.h"
#include "../../Framework/Scene.h"
#include "../../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <RenderBase/MeshShapeProxy.h>
#include <Renderer/MeshRenderProxy.h>

namespace hyp
{
    REGISTER_CLASS(StaticMeshRenderer)

    void StaticMeshRenderer::onRegister()
    {
        MeshRenderer::onRegister();
        updateMeshShapeProxy();
        getScene()->registerLumenStateListener(this);
    }

    void StaticMeshRenderer::onUnregister()
    {
        getScene()->unregisterLumenStateListener(this);
        MeshRenderer::onUnregister();
    }

    void StaticMeshRenderer::setMesh(StaticMesh* newMesh)
    {
        if (mMesh == newMesh)
            return;

        TRef<StaticMesh> oldMesh = mMesh;
        mMesh = newMesh;
        onMeshChanged(oldMesh.get());
        updateMeshShapeProxy();
    }

    void StaticMeshRenderer::updateMeshShapeProxy()
    {
        if (!getProxyHandle().isValid())
            return;

        // meshShapeProxyHandle 只给 Card/SDF(Lumen)用, 普通渲染完全不碰它——场景没开 Lumen
        // 时干脆不创建, 既不用现烘不会被用到的 MDF, 也避免"先在非 Lumen 场景建出一个没有
        // SDF 数据的 shape proxy, 之后进 Lumen 场景也捡不回来"这个陈旧代理问题(shape proxy
        // 是资产级、只建一次、没有失效重建机制)
        MeshShapeProxyHandle meshShapeProxyHandle;
        Scene* scene = getEntity()->getScene();
        if (mMesh && scene && scene->isLumenEnabled())
        {
            meshShapeProxyHandle = mMesh->getOrCreateMeshShapeProxy();
        }

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = getProxyHandle(), meshShapeProxyHandle](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                MeshShapeProxy* meshShapeProxy = meshShapeProxyHandle.isValid() ? proxyManager.getProxy(meshShapeProxyHandle) : nullptr;
                MeshRenderProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->setMeshShapeProxy(meshShapeProxy);
            }
        );
    }

    void StaticMeshRenderer::onLumenEnabledChanged(bool enabled)
    {
        updateMeshShapeProxy();
    }

    template <> Type* Reflection::createType<StaticMeshRenderer>()
    {
        auto c = new TClass<StaticMeshRenderer, MeshRenderer>("StaticMeshRenderer");
        c->setDisplayName("Static Mesh Renderer");
        c->addProperty("Mesh", &StaticMeshRenderer::mMesh);
        return c;
    }
}
