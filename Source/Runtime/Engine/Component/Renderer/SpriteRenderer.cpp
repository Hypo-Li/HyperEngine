#include "SpriteRenderer.h"

#include <Renderer/QuadManager.h>

#include <algorithm>

namespace hyp
{
    REGISTER_CLASS(SpriteRenderer)

    void SpriteRenderer::setSize(float2 size)
    {
        if (mSize == size)
            return;
        mSize = size;
        markLayoutDirty();
    }

    void SpriteRenderer::setUvMin(float2 uvMin)
    {
        if (mUvMin == uvMin)
            return;
        mUvMin = uvMin;
        markLayoutDirty();
    }

    void SpriteRenderer::setUvMax(float2 uvMax)
    {
        if (mUvMax == uvMax)
            return;
        mUvMax = uvMax;
        markLayoutDirty();
    }

    void SpriteRenderer::setFlipX(bool flipX)
    {
        if (mFlipX == flipX)
            return;
        mFlipX = flipX;
        markLayoutDirty();
    }

    void SpriteRenderer::setFlipY(bool flipY)
    {
        if (mFlipY == flipY)
            return;
        mFlipY = flipY;
        markLayoutDirty();
    }

    void SpriteRenderer::buildLayout(std::vector<QuadElement>& elements, float2& blockSize) const
    {
        float2 uvMin = mUvMin;
        float2 uvMax = mUvMax;
        if (mFlipX)
            std::swap(uvMin.x, uvMax.x);
        if (mFlipY)
            std::swap(uvMin.y, uvMax.y);

        QuadElement element = {};
        element.position = float2(0.0f);
        element.size = mSize;
        element.uvMin = packUnorm16x2(uvMin);
        element.uvMax = packUnorm16x2(uvMax);
        element.pageAndFlags = 0;
        element.color = 0xFFFFFFFFu;

        elements.assign(1, element);
        blockSize = mSize;
    }

    template <> Type* Reflection::createType<SpriteRenderer>()
    {
        auto c = new TClass<SpriteRenderer, QuadRenderer>("SpriteRenderer");
        c->setDisplayName("Sprite Renderer");
        c->addProperty("Size", &SpriteRenderer::mSize);
        c->addProperty("UV Min", &SpriteRenderer::mUvMin);
        c->addProperty("UV Max", &SpriteRenderer::mUvMax);
        c->addProperty("Flip X", &SpriteRenderer::mFlipX);
        c->addProperty("Flip Y", &SpriteRenderer::mFlipY);
        return c;
    }
}
