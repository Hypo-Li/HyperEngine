#pragma once
#include "QuadRenderer.h"
#include "../../Font/Font.h"

#include <string>

namespace hyp
{
    enum class TextHorizontalAlignment : uint8_t
    {
        Left,
        Center,
        Right
    };

    class TextRenderer : public QuadRenderer, public IFontListener
    {
        REFLECT_CLASS(TextRenderer)
    public:
        TextRenderer() = default;

        ~TextRenderer() noexcept override;

        void onRegister() override;

        void onUnregister() override;

        void onFontChanged() override;

        void setFont(Font* font);

        Font* getFont() const noexcept
        {
            return mFont;
        }

        void setText(std::string text);

        const std::string& getText() const noexcept
        {
            return mText;
        }

        // 一个 em 对应的世界长度
        void setFontSize(float fontSize);

        float getFontSize() const noexcept
        {
            return mFontSize;
        }

        void setLineSpacing(float lineSpacing);

        float getLineSpacing() const noexcept
        {
            return mLineSpacing;
        }

        // 单位 em
        void setLetterSpacing(float letterSpacing);

        float getLetterSpacing() const noexcept
        {
            return mLetterSpacing;
        }

        void setHorizontalAlignment(TextHorizontalAlignment horizontalAlignment);

        TextHorizontalAlignment getHorizontalAlignment() const noexcept
        {
            return mHorizontalAlignment;
        }

        // 世界长度, 0 表示不自动换行
        void setMaxWidth(float maxWidth);

        float getMaxWidth() const noexcept
        {
            return mMaxWidth;
        }

    protected:
        void buildLayout(std::vector<QuadElement>& elements, float2& blockSize) const override;

    private:
        void attachFontListener();

        void detachFontListener();

        TRef<Font> mFont;
        bool mListeningToFont = false;
        std::string mText;
        float mFontSize = 1.0f;
        float mLineSpacing = 1.0f;
        float mLetterSpacing = 0.0f;
        TextHorizontalAlignment mHorizontalAlignment = TextHorizontalAlignment::Left;
        float mMaxWidth = 0.0f;
    };

    template <> Type* Reflection::createType<TextHorizontalAlignment>();

    template <> Type* Reflection::createType<TextRenderer>();
}
