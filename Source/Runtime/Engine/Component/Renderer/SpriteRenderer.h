#pragma once
#include "QuadRenderer.h"

namespace hyp
{
    class SpriteRenderer : public QuadRenderer
    {
        REFLECT_CLASS(SpriteRenderer)
    public:
        SpriteRenderer() = default;

        ~SpriteRenderer() noexcept override = default;

        // 世界长度, 宽(局部 X)和高(局部 Z)
        void setSize(float2 size);

        float2 getSize() const noexcept
        {
            return mSize;
        }

        // 纹理坐标 v 向下, uvMin 是图像区域的左上角, uvMax 是右下角
        void setUvMin(float2 uvMin);

        float2 getUvMin() const noexcept
        {
            return mUvMin;
        }

        void setUvMax(float2 uvMax);

        float2 getUvMax() const noexcept
        {
            return mUvMax;
        }

        void setFlipX(bool flipX);

        bool isFlipX() const noexcept
        {
            return mFlipX;
        }

        void setFlipY(bool flipY);

        bool isFlipY() const noexcept
        {
            return mFlipY;
        }

    protected:
        void buildLayout(std::vector<QuadElement>& elements, float2& blockSize) const override;

    private:
        float2 mSize = float2(1.0f, 1.0f);
        float2 mUvMin = float2(0.0f, 0.0f);
        float2 mUvMax = float2(1.0f, 1.0f);
        bool mFlipX = false;
        bool mFlipY = false;
    };

    template <> Type* Reflection::createType<SpriteRenderer>();
}
