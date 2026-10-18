#include "TextRenderer.h"

#include <Renderer/QuadManager.h>

#include <algorithm>

namespace hyp
{
    REGISTER_CLASS(TextRenderer)

    namespace
    {
        struct Line
        {
            uint32_t begin;
            uint32_t end;
            float width;
        };

        bool isBreakableSpace(uint32_t codepoint)
        {
            return codepoint == ' ' || codepoint == '\t';
        }
    }

    TextRenderer::~TextRenderer() noexcept
    {
        detachFontListener();
    }

    void TextRenderer::onRegister()
    {
        QuadRenderer::onRegister();
        attachFontListener();
    }

    void TextRenderer::onUnregister()
    {
        detachFontListener();
        QuadRenderer::onUnregister();
    }

    void TextRenderer::onFontChanged()
    {
        markLayoutDirty();
    }

    void TextRenderer::attachFontListener()
    {
        if (mListeningToFont || !mFont)
            return;

        mFont->addListener(this);
        mListeningToFont = true;
    }

    void TextRenderer::detachFontListener()
    {
        if (!mListeningToFont)
            return;

        mFont->removeListener(this);
        mListeningToFont = false;
    }

    void TextRenderer::setFont(Font* font)
    {
        if (mFont == font)
            return;
        detachFontListener();
        mFont = font;
        if (getProxyHandle().isValid())
            attachFontListener();
        markLayoutDirty();
    }

    void TextRenderer::setText(std::string text)
    {
        if (mText == text)
            return;
        mText = std::move(text);
        markLayoutDirty();
    }

    void TextRenderer::setFontSize(float fontSize)
    {
        if (mFontSize == fontSize)
            return;
        mFontSize = fontSize;
        markLayoutDirty();
    }

    void TextRenderer::setLineSpacing(float lineSpacing)
    {
        if (mLineSpacing == lineSpacing)
            return;
        mLineSpacing = lineSpacing;
        markLayoutDirty();
    }

    void TextRenderer::setLetterSpacing(float letterSpacing)
    {
        if (mLetterSpacing == letterSpacing)
            return;
        mLetterSpacing = letterSpacing;
        markLayoutDirty();
    }

    void TextRenderer::setHorizontalAlignment(TextHorizontalAlignment horizontalAlignment)
    {
        if (mHorizontalAlignment == horizontalAlignment)
            return;
        mHorizontalAlignment = horizontalAlignment;
        markLayoutDirty();
    }

    void TextRenderer::setMaxWidth(float maxWidth)
    {
        if (mMaxWidth == maxWidth)
            return;
        mMaxWidth = maxWidth;
        markLayoutDirty();
    }

    void TextRenderer::buildLayout(std::vector<QuadElement>& elements, float2& blockSize) const
    {
        elements.clear();
        blockSize = float2(0.0f);

        if (!mFont || !mFont->getPages() || mFont->getBakePpem() == 0 || mText.empty())
            return;

        const Font& font = *mFont;
        const auto& pageDescription = font.getPages()->getDescription();
        const float2 pageSize(static_cast<float>(pageDescription.width), static_cast<float>(pageDescription.height));
        const float texelToWorld = mFontSize / static_cast<float>(font.getBakePpem());
        const float lineAdvance = (font.getAscent() - font.getDescent() + font.getLineGap()) * mFontSize * mLineSpacing;

        std::vector<Line> lines;
        uint32_t lineBegin = 0;
        float penX = 0.0f;
        float contentEnd = 0.0f;

        bool hasPrevious = false;
        uint32_t previousCodepoint = 0;

        bool breakPending = false;
        bool breakValid = false;
        uint32_t breakElement = 0;
        float breakResumePen = 0.0f;
        float breakWidth = 0.0f;

        auto endLine = [&]()
        {
            lines.push_back({ lineBegin, static_cast<uint32_t>(elements.size()), contentEnd });
            lineBegin = static_cast<uint32_t>(elements.size());
            penX = 0.0f;
            contentEnd = 0.0f;
            hasPrevious = false;
            breakPending = false;
            breakValid = false;
        };

        for (uint32_t codepoint : decodeUtf8(mText))
        {
            if (codepoint == '\r')
                continue;
            if (codepoint == '\n')
            {
                endLine();
                continue;
            }

            const FontGlyph* found = font.findGlyph(codepoint);
            const FontGlyph& glyph = found ? *found : font.getNotdefGlyph();

            if (hasPrevious && found)
                penX += font.getKerning(previousCodepoint, codepoint) * mFontSize;

            if (breakPending)
            {
                breakPending = false;
                breakValid = true;
                breakElement = static_cast<uint32_t>(elements.size());
                breakResumePen = penX;
            }

            const bool space = isBreakableSpace(codepoint);
            const bool lineHasContent = contentEnd > 0.0f || elements.size() > lineBegin;
            if (!space && mMaxWidth > 0.0f && lineHasContent && penX + glyph.advance * mFontSize > mMaxWidth)
            {
                if (breakValid)
                {
                    lines.push_back({ lineBegin, breakElement, breakWidth });
                    for (size_t i = breakElement; i < elements.size(); ++i)
                        elements[i].position.x -= breakResumePen;
                    penX -= breakResumePen;
                    contentEnd = std::max(contentEnd - breakResumePen, 0.0f);
                    lineBegin = breakElement;
                    breakValid = false;
                }
                else
                {
                    lines.push_back({ lineBegin, static_cast<uint32_t>(elements.size()), contentEnd });
                    lineBegin = static_cast<uint32_t>(elements.size());
                    penX = 0.0f;
                    contentEnd = 0.0f;
                }
            }

            if (glyph.width > 0 && glyph.height > 0)
            {
                QuadElement element = {};
                element.position = float2(
                    penX + glyph.bearingX * mFontSize,
                    (glyph.bearingY - static_cast<float>(glyph.height) / static_cast<float>(font.getBakePpem())) * mFontSize
                );
                element.size = float2(static_cast<float>(glyph.width), static_cast<float>(glyph.height)) * texelToWorld;
                element.uvMin = packUnorm16x2(float2(glyph.x, glyph.y) / pageSize);
                element.uvMax = packUnorm16x2(float2(glyph.x + glyph.width, glyph.y + glyph.height) / pageSize);
                element.pageAndFlags = glyph.page;
                element.color = 0xFFFFFFFFu;
                elements.push_back(element);
            }

            penX += (glyph.advance + mLetterSpacing) * mFontSize;
            if (space)
            {
                breakPending = true;
                breakWidth = contentEnd;
            }
            else
            {
                contentEnd = penX - mLetterSpacing * mFontSize;
            }

            hasPrevious = found != nullptr;
            previousCodepoint = codepoint;
        }
        endLine();

        float blockWidth = 0.0f;
        for (const Line& line : lines)
            blockWidth = std::max(blockWidth, line.width);

        const float lineCount = static_cast<float>(lines.size());
        const float blockHeight = (font.getAscent() - font.getDescent()) * mFontSize + (lineCount - 1.0f) * lineAdvance;

        for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
        {
            const Line& line = lines[lineIndex];
            const float baselineY = blockHeight - font.getAscent() * mFontSize - static_cast<float>(lineIndex) * lineAdvance;

            float offsetX = 0.0f;
            if (mHorizontalAlignment == TextHorizontalAlignment::Center)
                offsetX = (blockWidth - line.width) * 0.5f;
            else if (mHorizontalAlignment == TextHorizontalAlignment::Right)
                offsetX = blockWidth - line.width;

            for (uint32_t i = line.begin; i < line.end; ++i)
            {
                elements[i].position.x += offsetX;
                elements[i].position.y += baselineY;
            }
        }

        blockSize = float2(blockWidth, blockHeight);
    }

    template <> Type* Reflection::createType<TextHorizontalAlignment>()
    {
        auto e = new TEnumeration<TextHorizontalAlignment>("TextHorizontalAlignment");
        e->addValue("Left", TextHorizontalAlignment::Left);
        e->addValue("Center", TextHorizontalAlignment::Center);
        e->addValue("Right", TextHorizontalAlignment::Right);
        return e;
    }

    template <> Type* Reflection::createType<TextRenderer>()
    {
        auto c = new TClass<TextRenderer, QuadRenderer>("TextRenderer");
        c->setDisplayName("Text Renderer");
        c->addProperty("Font", &TextRenderer::mFont);
        c->addProperty("Text", &TextRenderer::mText);
        c->addProperty("Font Size", &TextRenderer::mFontSize);
        c->addProperty("Line Spacing", &TextRenderer::mLineSpacing);
        c->addProperty("Letter Spacing", &TextRenderer::mLetterSpacing);
        c->addProperty("Horizontal Alignment", &TextRenderer::mHorizontalAlignment);
        c->addProperty("Max Width", &TextRenderer::mMaxWidth);
        return c;
    }
}
