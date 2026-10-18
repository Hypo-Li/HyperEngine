#include "QuadRenderer.h"
#include "../../Framework/Entity.h"
#include "../../Framework/Scene.h"
#include "../../Context.h"

#include <RenderBase/RenderFrontend.h>
#include <Renderer/QuadRenderProxy.h>
#include <Renderer/SceneProxy.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace hyp
{
    REGISTER_CLASS(QuadRenderer)

    namespace
    {
        uint32_t packRGBA8(const float4& color)
        {
            auto quantize = [](float value)
            {
                return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            return quantize(color.x) | (quantize(color.y) << 8) | (quantize(color.z) << 16) | (quantize(color.w) << 24);
        }
    }

    uint32_t packUnorm16x2(float2 value)
    {
        auto quantize = [](float v)
        {
            return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 65535.0f + 0.5f);
        };
        return quantize(value.x) | (quantize(value.y) << 16);
    }

    QuadRenderer::~QuadRenderer()
    {
    }

    void QuadRenderer::onRegister()
    {
        std::vector<QuadElement> elements;
        buildLayout(elements, mBlockSize);

        MaterialProxyHandle materialHandle;
        if (mMaterial)
            materialHandle = mMaterial->getOrCreateMaterialProxy();

        auto& renderFrontend = Context::get().getRenderFrontend();
        mProxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Quad_Render>();

        auto& transform = getEntity()->getTransform();
        Scene* scene = getScene();
        SceneProxyHandle sceneProxyHandle = scene->getOrCreateSceneProxy();

        renderFrontend.enqueueLambda(
            [proxyHandle = mProxyHandle,
            materialHandle,
            elements = std::move(elements),
            placement = makePlacement(),
            boundingBox = computeWorldBoundingBox(),
            worldMatrix = computeWorldMatrix(),
            mobility = getEntity()->getMobility(),
            visible = getEntity()->isVisible(),
            sortOrder = mSortOrder,
            renderMask = mCastShadow ? ~0u : ~Context::get().getRenderTagRegistry().getBuiltinShadowDepthMask(),
            sceneProxyHandle](FrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                proxyManager.createProxy(proxyHandle, *sceneProxy, QuadRenderProxy::Desc{
                    .drawable = {
                        .instance = { .visible = visible, .mobility = mobility, .worldMatrix = worldMatrix },
                        .boundingBox = boundingBox,
                        .renderMask = renderMask,
                        .sortOrder = sortOrder
                    },
                    .material = materialHandle.isValid() ? proxyManager.getProxy(materialHandle) : nullptr,
                    .elements = std::move(elements),
                    .placement = placement
                });
            }
        );

        scene->registerTransformListener(&transform, this);
    }

    void QuadRenderer::onUnregister()
    {
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
        mProxyHandle.reset();
    }

    void QuadRenderer::onVisibilityChanged(bool visible)
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

    void QuadRenderer::onTransformUpdate(const Transform& transform)
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            boundingBox = computeWorldBoundingBox(),
            worldMatrix = computeWorldMatrix()](FrameContext& context)
            {
                QuadRenderProxy* proxy = context.getProxyManager().getProxy(proxyHandle);
                proxy->setBoundingBox(boundingBox);
                proxy->updateInstanceTransform(worldMatrix);
            }
        );
    }

    void QuadRenderer::onMobilityChanged(Mobility mobility)
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, mobility](FrameContext& context)
            {
                context.getProxyManager().getProxy(proxyHandle)->setMobility(mobility);
            }
        );
    }

    void QuadRenderer::setMaterial(Material* material)
    {
        if (mMaterial == material)
            return;
        mMaterial = material;

        if (!mProxyHandle.isValid())
            return;

        MaterialProxyHandle materialHandle;
        if (material)
            materialHandle = material->getOrCreateMaterialProxy();

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle, materialHandle](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                proxyManager.getProxy(proxyHandle)->setMaterial(
                    materialHandle.isValid() ? proxyManager.getProxy(materialHandle) : nullptr
                );
            }
        );
    }

    void QuadRenderer::setAnchor(float2 anchor)
    {
        if (mAnchor == anchor)
            return;
        mAnchor = anchor;
        markPlacementDirty();
    }

    void QuadRenderer::setOffset(float3 offset)
    {
        if (mOffset == offset)
            return;
        mOffset = offset;
        markPlacementDirty();
    }

    void QuadRenderer::setTint(float4 tint)
    {
        if (mTint == tint)
            return;
        mTint = tint;
        markPlacementDirty();
    }

    void QuadRenderer::setBillboardMode(QuadBillboardMode billboardMode)
    {
        if (mBillboardMode == billboardMode)
            return;
        mBillboardMode = billboardMode;
        markPlacementDirty();
    }

    void QuadRenderer::setAutoScaleStrength(float autoScaleStrength)
    {
        if (mAutoScaleStrength == autoScaleStrength)
            return;
        mAutoScaleStrength = autoScaleStrength;
        markPlacementDirty();
    }

    void QuadRenderer::setReferenceDistance(float referenceDistance)
    {
        if (mReferenceDistance == referenceDistance)
            return;
        mReferenceDistance = referenceDistance;
        markPlacementDirty();
    }

    void QuadRenderer::setMinScale(float minScale)
    {
        if (mMinScale == minScale)
            return;
        mMinScale = minScale;
        markPlacementDirty();
    }

    void QuadRenderer::setMaxScale(float maxScale)
    {
        if (mMaxScale == maxScale)
            return;
        mMaxScale = maxScale;
        markPlacementDirty();
    }

    void QuadRenderer::setDepthBias(float depthBias)
    {
        if (mDepthBias == depthBias)
            return;
        mDepthBias = depthBias;
        markPlacementDirty();
    }

    void QuadRenderer::setSortOrder(int8_t sortOrder)
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

    void QuadRenderer::setCastShadow(bool castShadow)
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

    Boxd QuadRenderer::computeWorldBoundingBox() const
    {
        const double extentScale = mAutoScaleStrength > 0.0f ? std::max(1.0, double(mMaxScale)) : 1.0;
        const double3 localMin = double3(-double(mAnchor.x) * mBlockSize.x, 0.0, -double(mAnchor.y) * mBlockSize.y) * extentScale;
        const double3 localMax = double3((1.0 - mAnchor.x) * mBlockSize.x, 0.0, (1.0 - mAnchor.y) * mBlockSize.y) * extentScale;
        const double4x4 worldMatrix = computeWorldMatrix();

        Boxd box;
        if (mBillboardMode == QuadBillboardMode::None)
        {
            box = Boxd(localMin, localMax).transformBy(worldMatrix);
        }
        else
        {
            const double3 farthestCorner = glm::max(glm::abs(localMin), glm::abs(localMax));
            const double axisScale = std::max(glm::length(double3(worldMatrix[0])), glm::length(double3(worldMatrix[2])));
            const double radius = glm::length(farthestCorner) * axisScale;
            const double3 center(worldMatrix[3]);
            box = Boxd(center - double3(radius), center + double3(radius));
        }

        const double3 depthBiasPadding(std::abs(double(mDepthBias)));
        return Boxd(box.getMin() - depthBiasPadding, box.getMax() + depthBiasPadding);
    }

    void QuadRenderer::markLayoutDirty()
    {
        std::vector<QuadElement> elements;
        buildLayout(elements, mBlockSize);

        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            elements = std::move(elements),
            placement = makePlacement(),
            boundingBox = computeWorldBoundingBox()](FrameContext& context) mutable
            {
                QuadRenderProxy* proxy = context.getProxyManager().getProxy(proxyHandle);
                proxy->setElements(std::move(elements));
                proxy->setPlacement(placement);
                proxy->setBoundingBox(boundingBox);
            }
        );
    }

    void QuadRenderer::markPlacementDirty()
    {
        if (!mProxyHandle.isValid())
            return;

        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle = mProxyHandle,
            placement = makePlacement(),
            boundingBox = computeWorldBoundingBox(),
            worldMatrix = computeWorldMatrix()](FrameContext& context)
            {
                QuadRenderProxy* proxy = context.getProxyManager().getProxy(proxyHandle);
                proxy->setPlacement(placement);
                proxy->setBoundingBox(boundingBox);
                proxy->updateInstanceTransform(worldMatrix);
            }
        );
    }

    QuadPlacement QuadRenderer::makePlacement() const
    {
        QuadPlacement placement;
        placement.billboardMode = mBillboardMode;
        placement.autoScaleStrength = mAutoScaleStrength;
        placement.referenceDistance = mReferenceDistance;
        placement.minScale = mMinScale;
        placement.maxScale = mMaxScale;
        placement.depthBias = mDepthBias;
        placement.blockSize = mBlockSize;
        placement.anchor = mAnchor;
        placement.tint = packRGBA8(mTint);
        return placement;
    }

    double4x4 QuadRenderer::computeWorldMatrix() const
    {
        return getEntity()->getTransform().getWorldMatrix() * glm::translate(double4x4(1.0), double3(mOffset));
    }

    template <> Type* Reflection::createType<QuadBillboardMode>()
    {
        auto e = new TEnumeration<QuadBillboardMode>("QuadBillboardMode");
        e->addValue("None", QuadBillboardMode::None);
        e->addValue("Screen", QuadBillboardMode::Screen);
        e->addValue("Axis", QuadBillboardMode::Axis);
        return e;
    }

    template <> Type* Reflection::createType<QuadRenderer>()
    {
        auto c = new TClass<QuadRenderer, Component>("QuadRenderer");
        c->setDisplayName("Quad Renderer");
        c->addProperty("Material", &QuadRenderer::mMaterial);
        c->addProperty("Anchor", &QuadRenderer::mAnchor);
        c->addProperty("Offset", &QuadRenderer::mOffset);
        c->addProperty("Tint", &QuadRenderer::mTint);
        c->addProperty("Billboard Mode", &QuadRenderer::mBillboardMode);
        c->addProperty("Auto Scale Strength", &QuadRenderer::mAutoScaleStrength);
        c->addProperty("Reference Distance", &QuadRenderer::mReferenceDistance);
        c->addProperty("Min Scale", &QuadRenderer::mMinScale);
        c->addProperty("Max Scale", &QuadRenderer::mMaxScale);
        c->addProperty("Depth Bias", &QuadRenderer::mDepthBias);
        c->addProperty("Sort Order", &QuadRenderer::mSortOrder);
        c->addProperty("Cast Shadow", &QuadRenderer::mCastShadow);
        return c;
    }
}
