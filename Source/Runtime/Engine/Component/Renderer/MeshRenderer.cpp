#include "MeshRenderer.h"
#include "../../Framework/Entity.h"
#include "../../Framework/Scene.h"
#include "../../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/MeshRenderProxy.h>
#include <Renderer/SceneProxy.h>

namespace hyp
{
    REGISTER_CLASS(MeshRenderer)

    MeshRenderer::~MeshRenderer()
    {
        assert(!mListeningToMesh);
    }

    void MeshRenderer::onRegister()
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Mesh_Render>();

        Mesh* mesh = getMesh();
        mOverrideMaterials.resize(mesh ? mesh->getMaterialCount() : 0);

        auto& transform = getEntity()->getTransform();
        Mobility mobility = getEntity()->getMobility();

        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle,
            boundingBox = computeWorldBoundingBox(),
            worldMatrix = transform.getWorldMatrix(),
            mobility,
            visible = getEntity()->isVisible(),
            renderMask = mCastShadow ? ~0u : ~Context::get().getRenderTagRegistry().getBuiltinShadowDepthMask(),
            sortOrder = mSortOrder,
            sceneProxyHandle](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                proxyManager.createProxy(proxyHandle, *sceneProxy, MeshRenderProxy::Desc{
                    .instance = { .visible = visible, .mobility = mobility, .worldMatrix = worldMatrix },
                    .boundingBox = boundingBox,
                    .renderMask = renderMask,
                    .sortOrder = sortOrder
                });
            }
        );

        updateProxyDrawItems();
        attachMeshListener();

        scene->registerTransformListener(&transform, this);
    }

    void MeshRenderer::onUnregister()
    {
        detachMeshListener(getMesh());

        Scene* scene = getScene();
        scene->unregisterTransformListener(&getEntity()->getTransform(), this);

        auto& renderFrontend = Context::get().getRenderFrontend();
        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle](FrameContext& context)
            {
                context.getProxyManager().destroyProxy(proxyHandle);
            }
        );

        renderFrontend.freeProxyHandle(mProxyHandle);
    }

    void MeshRenderer::onVisibilityChanged(bool visible)
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, visible](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setVisible(visible);
            }
        );
    }

    void MeshRenderer::onTransformUpdate(const Transform& transform)
    {
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            boundingBox = computeWorldBoundingBox(),
            worldMatrix = transform.getWorldMatrix()]
            (FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                MeshRenderProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->setBoundingBox(boundingBox);
                proxy->updateInstanceTransform(worldMatrix);
            }
        );
    }

    void MeshRenderer::onMobilityChanged(Mobility mobility)
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, mobility](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                MeshRenderProxy* proxy = proxyManager.getProxy(proxyHandle);
                proxy->setMobility(mobility);
            }
        );
    }

    void MeshRenderer::onMeshChanged(Mesh* oldMesh)
    {
        detachMeshListener(oldMesh);

        Mesh* mesh = getMesh();
        mOverrideMaterials.clear();
        mOverrideMaterials.resize(mesh ? mesh->getMaterialCount() : 0);
        updateProxyDrawItems();

        // 换 mesh 不会触发 onTransformUpdate(), 之前这里漏了包围盒更新——渲染用的几何体已经
        // 换成新 mesh 了, 但 MeshRenderProxy 上参与剔除测试的包围盒还停在旧 mesh(或空)的值,
        // 导致新几何体明明在屏幕上却被按旧包围盒误剔除
        if (mProxyHandle.isValid())
        {
            Context::get().getRenderFrontend().enqueueLambda(
                [proxyHandle = mProxyHandle, boundingBox = computeWorldBoundingBox()](FrameContext& context)
                {
                    auto& proxyManager = context.getProxyManager();
                    MeshRenderProxy* proxy = proxyManager.getProxy(proxyHandle);
                    proxy->setBoundingBox(boundingBox);
                }
            );
            attachMeshListener();
        }
    }

    void MeshRenderer::onMeshMaterialsChanged()
    {
        Mesh* mesh = getMesh();
        mOverrideMaterials.resize(mesh ? mesh->getMaterialCount() : 0);
        updateProxyDrawItems();
    }

    void MeshRenderer::attachMeshListener()
    {
        Mesh* mesh = getMesh();
        if (mListeningToMesh || !mesh)
            return;

        mesh->addMaterialListener(this);
        mListeningToMesh = true;
    }

    void MeshRenderer::detachMeshListener(Mesh* mesh)
    {
        if (!mListeningToMesh)
            return;

        assert(mesh);
        mesh->removeMaterialListener(this);
        mListeningToMesh = false;
    }

    Material* MeshRenderer::getEffectiveMaterial(uint32_t index) const noexcept
    {
        assert(index < mOverrideMaterials.size());
        Material* overrideMaterial = mOverrideMaterials[index].get();
        return overrideMaterial ? overrideMaterial : getMesh()->getMaterial(index);
    }

    void MeshRenderer::setSortOrder(int8_t sortOrder)
    {
        if (mSortOrder == sortOrder)
            return;
        mSortOrder = sortOrder;

        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, sortOrder](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setSortOrder(sortOrder);
            }
        );
    }

    void MeshRenderer::setCastShadow(bool castShadow)
    {
        if (mCastShadow == castShadow)
            return;
        mCastShadow = castShadow;

        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            renderMask = castShadow ? ~0u : ~Context::get().getRenderTagRegistry().getBuiltinShadowDepthMask()](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setRenderMask(renderMask);
            }
        );
    }

    Boxd MeshRenderer::computeWorldBoundingBox()
    {
        Boxd boundingBox;
        Mesh* mesh = getMesh();
        if (mesh)
        {
            boundingBox = mesh->getOrUpdateBoundingBox();
        }
        return boundingBox.transformBy(getEntity()->getTransform().getWorldMatrix());
    }

    uint32_t MeshRenderer::getSubmeshCount() const noexcept
    {
        Mesh* mesh = getMesh();
        return mesh ? mesh->getSubmeshCount(0) : 0;
    }

    void MeshRenderer::setOverrideMaterial(uint32_t index, Material* material)
    {
        assert(index < mOverrideMaterials.size());
        mOverrideMaterials[index] = material;

        updateProxyDrawItems();
    }

    void MeshRenderer::updateProxyDrawItems()
    {
        if (!mProxyHandle.isValid())
            return;

        Mesh* mesh = getMesh();

        struct DrawItemHandle
        {
            MaterialProxyHandle material;
            GeometryProxyHandle geometry;
            uint32_t submeshIndex;
        };
        std::vector<DrawItemHandle> drawItemHandles;
        struct LODRange
        {
            uint32_t startItem;
            uint32_t itemCount;
            float minScreenPercentage;
        };
        std::vector<MeshRenderProxy::LODRange> lodRanges;

        if (mesh)
        {
            uint32_t totalSubmeshCount = 0;
            lodRanges.resize(mesh->getLODCount());
            for (uint32_t i = 0; i < mesh->getLODCount(); ++i)
            {
                uint32_t submeshCount = mesh->getSubmeshCount(i);
                float minScreenPercentage = mesh->getLODMinScreenPercentage(i);
                lodRanges[i] = { totalSubmeshCount, submeshCount, minScreenPercentage };
                totalSubmeshCount += mesh->getSubmeshCount(i);
            }
            drawItemHandles.reserve(totalSubmeshCount);
            for (uint32_t i = 0; i < mesh->getLODCount(); ++i)
            {
                for (uint32_t j = 0; j < mesh->getSubmeshCount(i); ++j)
                {
                    MaterialProxyHandle materialProxyHandle = getEffectiveMaterial(mesh->getSubmeshMaterialIndex(i, j))->getOrCreateMaterialProxy();
                    GeometryProxyHandle geometryProxyHandle = mesh->getOrCreateGeometryProxy(i);
                    drawItemHandles.emplace_back(materialProxyHandle, geometryProxyHandle, j);
                }
            }
        }

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            drawItemHandles = std::move(drawItemHandles),
            lodRanges = std::move(lodRanges)](FrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                MeshRenderProxy* proxy = proxyManager.getProxy(proxyHandle);

                std::vector<DrawItem> drawItems;
                drawItems.reserve(drawItemHandles.size());
                for (auto& drawItemHandle : drawItemHandles)
                {
                    MaterialProxy* materialProxy = proxyManager.getProxy(drawItemHandle.material);
                    GeometryProxy* geometryProxy = proxyManager.getProxy(drawItemHandle.geometry);
                    uint32_t submeshIndex = drawItemHandle.submeshIndex;
                    drawItems.emplace_back(materialProxy, geometryProxy, submeshIndex, proxy->getMeshInstanceIndex());
                }
                proxy->setDrawItems(std::move(drawItems), std::move(lodRanges));
            }
        );
    }

    template <> Type* Reflection::createType<MeshRenderer>()
    {
        auto c = new TClass<MeshRenderer, Component>("MeshRenderer");
        c->setDisplayName("Mesh Renderer");
        c->addProperty("Override Materials", &MeshRenderer::mOverrideMaterials);
        c->addProperty("Sort Order", &MeshRenderer::mSortOrder);
        c->addProperty("Cast Shadow", &MeshRenderer::mCastShadow);
        return c;
    }
}
