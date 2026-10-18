#include "DrawableProxy.h"

namespace hyp
{
    DrawableProxy::DrawableProxy(SceneProxy& sceneProxy, const Desc& desc) :
        InstanceProxy(sceneProxy, desc.instance),
        mRenderMask(desc.renderMask),
        mSortOrder(desc.sortOrder)
    {
        DrawableProxy::setBoundingBox(desc.boundingBox);
    }

    void DrawableProxy::setBoundingBox(Boxd boundingBox) noexcept
    {
        mBoundingBox = boundingBox;
        double3 boxSize = mBoundingBox.getSize();
        double radius = Math::length(boxSize) * 0.5;
        mBoundingSphere = Sphered(mBoundingBox.getCenter(), radius);
    }

    void DrawableProxy::setMobility(Mobility mobility) noexcept
    {
        InstanceProxy::setMobility(mobility);
    }
}
