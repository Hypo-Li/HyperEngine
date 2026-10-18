#include "QuadRenderProxy.h"
#include "SceneProxy.h"

#include <RenderBase/MaterialProxy.h>

#include <Core/Common/LogSystem.h>

namespace hyp
{
    namespace
    {
        constexpr uint32_t kVerticesPerQuad = 6;
    }

    QuadRenderProxy::QuadRenderProxy(SceneProxy& sceneProxy, Desc desc) :
        DrawableProxy(sceneProxy, desc.drawable),
        mQuadManager(sceneProxy.getOrCreateQuadManager()),
        mMaterial(desc.material),
        mPlacement(desc.placement)
    {
        sceneProxy.registerElement(this);

        mQuadInstanceIndex = mQuadManager.acquireInstanceIndex();
        if (!mQuadInstanceIndex)
        {
            LOG_WARN("QuadRenderProxy: 实例旁表容量耗尽, 这个四边形渲染器不会被绘制");
            return;
        }

        uploadElements(std::move(desc.elements));
        uploadInstanceData();
        syncBillboardFlags();
        refreshDrawItems();
    }

    QuadRenderProxy::~QuadRenderProxy() noexcept
    {
        releaseElementRange();
        if (mQuadInstanceIndex)
            mQuadManager.releaseInstanceIndex(*mQuadInstanceIndex);

        getSceneProxy().unregisterElement(this);
    }

    void QuadRenderProxy::setMaterial(MaterialProxy* material) noexcept
    {
        mMaterial = material;
        refreshDrawItems();
    }

    void QuadRenderProxy::setElements(std::vector<QuadElement>&& elements)
    {
        if (!mQuadInstanceIndex)
            return;

        uploadElements(std::move(elements));
        uploadInstanceData();
        refreshDrawItems();
    }

    void QuadRenderProxy::setPlacement(const QuadPlacement& placement)
    {
        mPlacement = placement;

        if (!mQuadInstanceIndex)
            return;

        uploadInstanceData();
        syncBillboardFlags();
    }

    void QuadRenderProxy::uploadElements(std::vector<QuadElement>&& elements)
    {
        releaseElementRange();
        if (elements.empty())
            return;

        const uint32_t elementCount = static_cast<uint32_t>(elements.size());
        std::optional<uint32_t> range = mQuadManager.allocateElementRange(elementCount);
        if (!range)
        {
            LOG_WARN("QuadRenderProxy::uploadElements: 元素缓冲容量不足(需要 {} 个), 这个四边形渲染器不会被绘制", elementCount);
            return;
        }

        mElementBase = range;
        mElementRangeSize = elementCount;
        mQuadManager.queueElementUpload(*range, std::move(elements));
    }

    void QuadRenderProxy::uploadInstanceData()
    {
        QuadInstanceData data = {};
        data.instanceIndex = getInstanceIndex();
        data.elementBase = mElementBase.value_or(0);
        data.elementCount = mElementBase ? mElementRangeSize : 0;
        data.tint = mPlacement.tint;
        data.blockSize = mPlacement.blockSize;
        data.anchor = mPlacement.anchor;
        data.depthBias = mPlacement.depthBias;
        data.autoScaleStrength = mPlacement.autoScaleStrength;
        data.referenceDistance = mPlacement.referenceDistance;
        data.minScale = mPlacement.minScale;
        data.maxScale = mPlacement.maxScale;
        mQuadManager.setInstanceData(*mQuadInstanceIndex, data);
    }

    void QuadRenderProxy::syncBillboardFlags() noexcept
    {
        SceneProxy& sceneProxy = getSceneProxy();
        InstanceData& instanceData = sceneProxy.getInstanceData(getInstanceIndex());
        enumRemoveFlags(instanceData.flags, InstanceFlags::Billboard_Screen | InstanceFlags::Billboard_Axis);
        if (mPlacement.billboardMode == QuadBillboardMode::Screen)
            enumAddFlags(instanceData.flags, InstanceFlags::Billboard_Screen);
        else if (mPlacement.billboardMode == QuadBillboardMode::Axis)
            enumAddFlags(instanceData.flags, InstanceFlags::Billboard_Axis);
        sceneProxy.markInstanceDataUpdated(getInstanceIndex());
    }

    void QuadRenderProxy::releaseElementRange() noexcept
    {
        if (!mElementBase)
            return;

        mQuadManager.freeElementRange(*mElementBase, mElementRangeSize);
        mElementBase.reset();
        mElementRangeSize = 0;
    }

    void QuadRenderProxy::refreshDrawItems()
    {
        mDrawItems.clear();
        if (!mMaterial || !mQuadInstanceIndex || !mElementBase)
            return;

        DrawItem drawItem;
        drawItem.material = mMaterial;
        drawItem.instanceIndex = *mQuadInstanceIndex;
        drawItem.vertexLayout = QuadManager::getVertexLayout();
        drawItem.vertexCount = mElementRangeSize * kVerticesPerQuad;
        mDrawItems.push_back(drawItem);
    }
}
