#pragma once
#include "../../Framework/Component.h"
#include "../../Framework/Transform.h"
#include "../../Material/Material.h"

#include <Core/Math/Box.h>
#include <Core/Math/Vector.h>
#include <RenderBase/ProxyHandle.h>

#include <vector>

namespace hyp
{
    struct QuadElement;
    struct QuadPlacement;
    enum class QuadBillboardMode : uint8_t;

    uint32_t packUnorm16x2(float2 value);

    // TextRenderer 和 SpriteRenderer 共用的部分: 一组带 UV 的四边形加整体的放置方式。
    // 派生类只负责把自己的内容排成四边形, 渲染代理、可见性、Mobility、包围盒这些跟具体内容无关。
    class QuadRenderer : public Component, public ITransformListener
    {
        REFLECT_CLASS(QuadRenderer)
    public:
        QuadRenderer() = default;

        virtual ~QuadRenderer() = 0;

        void onRegister() override;

        void onUnregister() override;

        void onVisibilityChanged(bool visible) override;

        void onTransformUpdate(const Transform& transform) override;

        void onMobilityChanged(Mobility mobility) override;

        void setMaterial(Material* material);

        Material* getMaterial() const noexcept
        {
            return mMaterial;
        }

        // 块内归一化坐标, (0,0) 是左下角, 整个块以它为原点放置
        void setAnchor(float2 anchor);

        float2 getAnchor() const noexcept
        {
            return mAnchor;
        }

        // 实体局部空间里相对 Transform 原点的偏移
        void setOffset(float3 offset);

        float3 getOffset() const noexcept
        {
            return mOffset;
        }

        void setTint(float4 tint);

        float4 getTint() const noexcept
        {
            return mTint;
        }

        void setBillboardMode(QuadBillboardMode billboardMode);

        QuadBillboardMode getBillboardMode() const noexcept
        {
            return mBillboardMode;
        }

        // 0 保持世界尺寸, 1 保持屏幕尺寸, 中间值线性混合; 在 referenceDistance(相机深度)处缩放为 1,
        // 实际缩放夹在 [minScale, maxScale]
        void setAutoScaleStrength(float autoScaleStrength);

        float getAutoScaleStrength() const noexcept
        {
            return mAutoScaleStrength;
        }

        void setReferenceDistance(float referenceDistance);

        float getReferenceDistance() const noexcept
        {
            return mReferenceDistance;
        }

        void setMinScale(float minScale);

        float getMinScale() const noexcept
        {
            return mMinScale;
        }

        void setMaxScale(float maxScale);

        float getMaxScale() const noexcept
        {
            return mMaxScale;
        }

        // 沿视线朝相机方向偏移的世界长度
        void setDepthBias(float depthBias);

        float getDepthBias() const noexcept
        {
            return mDepthBias;
        }

        void setSortOrder(int8_t sortOrder);

        int8_t getSortOrder() const noexcept
        {
            return mSortOrder;
        }

        void setCastShadow(bool castShadow);

        bool isCastShadow() const noexcept
        {
            return mCastShadow;
        }

        QuadRenderProxyHandle getProxyHandle() const noexcept
        {
            return mProxyHandle;
        }

        Boxd computeWorldBoundingBox() const;

    protected:
        // 位置相对于块的左下角, 单位是世界长度; 块尺寸用来定位锚点和算包围盒
        virtual void buildLayout(std::vector<QuadElement>& elements, float2& blockSize) const = 0;

        // 派生类的内容变化之后调用: 重新排版并把元素、放置参数、包围盒推给渲染代理
        void markLayoutDirty();

    private:
        QuadPlacement makePlacement() const;

        double4x4 computeWorldMatrix() const;

        // 放置参数(锚点、偏移、色调)变化之后调用: 只推放置参数、包围盒和世界矩阵, 不重传元素
        void markPlacementDirty();

    private:
        TRef<Material> mMaterial;
        float2 mAnchor = float2(0.5f, 0.5f);
        float3 mOffset = float3(0.0f);
        float4 mTint = float4(1.0f);
        QuadBillboardMode mBillboardMode{};
        float mAutoScaleStrength = 0.0f;
        float mReferenceDistance = 10.0f;
        float mMinScale = 0.1f;
        float mMaxScale = 10.0f;
        float mDepthBias = 0.0f;
        int8_t mSortOrder = 0;
        bool mCastShadow = true;

        float2 mBlockSize = float2(0.0f);

        QuadRenderProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<QuadBillboardMode>();

    template <> Type* Reflection::createType<QuadRenderer>();
}
