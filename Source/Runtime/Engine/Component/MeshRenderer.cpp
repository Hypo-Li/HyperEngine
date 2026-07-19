#include "MeshRenderer.h"
#include "../Framework/Entity.h"
#include "../Framework/Scene.h"
#include "../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/MeshRenderProxy.h>
#include <Renderer/SceneProxy.h>

namespace hyp
{
    REGISTER_CLASS(MeshRenderer)

    void MeshRenderer::onRegister()
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Mesh_Render>();

        Boxd boundingBox;
        if (mMesh)
        {
            boundingBox = mMesh->getOrUpdateBoundingBox();
        }
        auto& transform = getEntity()->getTransform();

        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle,
            boundingBox = boundingBox.transformBy(transform.getWorldMatrix()),
            worldMatrix = transform.getWorldMatrix(),
            sceneProxyHandle](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                MeshRenderProxy* meshRenderProxy = proxyManager.createProxy(proxyHandle);
                meshRenderProxy->setBoundingBox(boundingBox);

                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                sceneProxy->addMeshRenderProxy(meshRenderProxy);

                uint32_t instanceIndex = meshRenderProxy->getInstanceIndex();
                auto& instanceData = sceneProxy->getInstanceData(instanceIndex);
                instanceData.localToWorld.fromDouble4x4(worldMatrix);
                instanceData.localToWorldRot = float4x4(worldMatrix);
                sceneProxy->notifyInstanceDataUpdated(instanceIndex);
            }
        );

        updateProxyDrawItems();

        scene->registerTransformListener(&transform, this);
    }

    void MeshRenderer::onUnregister()
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
                MeshRenderProxy* meshRenderProxy = proxyManager.getProxy(proxyHandle);
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                sceneProxy->removeMeshRenderProxy(meshRenderProxy);
                proxyManager.destroyProxy(proxyHandle);
            }
        );

        renderFrontend.freeProxyHandle(mProxyHandle);
    }

    void MeshRenderer::onVisible()
    {
        setProxyVisible(true);
    }

    void MeshRenderer::onInvisible()
    {
        setProxyVisible(false);
    }

    void MeshRenderer::onTick(float deltaTime)
    {

    }

    void MeshRenderer::onTransformUpdate(const Transform& transform)
    {
        Boxd boundingBox;
        if (mMesh)
        {
            boundingBox = mMesh->getOrUpdateBoundingBox();
        }
        SceneProxyHandle sceneProxyHandle = getScene()->getOrCreateSceneProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            boundingBox = boundingBox.transformBy(transform.getWorldMatrix()),
            sceneProxyHandle,
            worldMatrix = transform.getWorldMatrix()]
            (FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                MeshRenderProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->setBoundingBox(boundingBox);
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);

                uint32_t instanceIndex = proxy->getInstanceIndex();
                auto& instanceData = sceneProxy->getInstanceData(instanceIndex);
                instanceData.localToWorld.fromDouble4x4(worldMatrix);
                instanceData.localToWorldRot = float4x4(worldMatrix);
                sceneProxy->notifyInstanceDataUpdated(instanceIndex);
            }
        );
    }

    void MeshRenderer::setMesh(Mesh* newMesh)
    {
        if (mMesh == newMesh)
            return;

        if (newMesh)
        {
            mOverrideMaterials.resize(newMesh->getMaterialCount());
            for (uint32_t i = 0; i < newMesh->getMaterialCount(); ++i)
            {
                mOverrideMaterials[i] = newMesh->getMaterial(i);
            }
        }
        else
        {
            mOverrideMaterials.clear();
        }
        mMesh = newMesh;
        updateProxyDrawItems();
    }

    void MeshRenderer::setOverrideMaterial(uint32_t index, Material* material)
    {
        assert(index < mOverrideMaterials.size());
        mOverrideMaterials[index] = material;

        updateProxyDrawItems();
    }

    void MeshRenderer::updateProxyDrawItems()
    {
        if (!mMesh || !mProxyHandle.isValid())
            return;

        std::vector<std::tuple<MaterialProxyHandle, GeometryProxyHandle, uint32_t>> drawItemHandles;
        std::vector<std::tuple<uint32_t, uint32_t, float>> lodInfos;
        uint32_t totalSubmeshCount = 0;
        lodInfos.resize(mMesh->getLODCount());
        for (uint32_t i = 0; i < mMesh->getLODCount(); ++i)
        {
            uint32_t submeshCount = mMesh->getSubmeshCount(i);
            float minScreenPercentage = mMesh->getLODMinScreenPercentage(i);
            lodInfos[i] = { totalSubmeshCount, submeshCount, minScreenPercentage };
            totalSubmeshCount += mMesh->getSubmeshCount(i);
        }
        drawItemHandles.reserve(totalSubmeshCount);
        for (uint32_t i = 0; i < mMesh->getLODCount(); ++i)
        {
            for (uint32_t j = 0; j < mMesh->getSubmeshCount(i); ++j)
            {
                MaterialProxyHandle materialProxyHandle = mOverrideMaterials[mMesh->getSubmeshMaterialIndex(i, j)]->getOrCreateMaterialProxy();
                GeometryProxyHandle geometryProxyHandle = mMesh->getOrCreateGeometryProxy(i);
                drawItemHandles.emplace_back(materialProxyHandle, geometryProxyHandle, j);
            }
        }

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            drawItemHandles = std::move(drawItemHandles),
            lodInfos = std::move(lodInfos)](FrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                MeshRenderProxy* proxy = proxyManager.getProxy(proxyHandle);
                std::vector<DrawItem> drawItems;
                drawItems.reserve(drawItemHandles.size());
                for (auto& drawItemHandle : drawItemHandles)
                {
                    MaterialProxy* materialProxy = proxyManager.getProxy(std::get<0>(drawItemHandle));
                    GeometryProxy* geometryProxy = proxyManager.getProxy(std::get<1>(drawItemHandle));
                    uint32_t submeshIndex = std::get<2>(drawItemHandle);
                    uint32_t instanceIndex = proxy->getInstanceIndex();
                    drawItems.emplace_back(materialProxy, geometryProxy, submeshIndex, instanceIndex);
                }
                std::vector<MeshRenderProxy::LOD> lods;
                lods.reserve(lodInfos.size());
                for (auto& lodInfo : lodInfos)
                {
                    uint32_t startItem = std::get<0>(lodInfo);
                    uint32_t itemCount = std::get<1>(lodInfo);
                    float minScreenPercentage = std::get<2>(lodInfo);
                    lods.emplace_back(startItem, itemCount, minScreenPercentage);
                }
                proxy->setDrawItems(std::move(drawItems), std::move(lods));
            }
        );
    }

    void MeshRenderer::setProxyVisible(bool visible)
    {
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, visible](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                MeshRenderProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->setVisible(visible);
            }
        );
    }

    template <> Type* Reflection::createType<MeshRenderer>()
    {
        auto c = new TClass<MeshRenderer, Component>("MeshRenderer");
        return c;
    }
}
