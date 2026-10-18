#pragma once
#include "../Texture/Texture2DArray.h"

#include <Core/Common/BulkData.h>
#include <Core/Object/Object.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hyp
{
    // cook 产物按数值序列化该枚举
    enum class FontSdfType : uint8_t
    {
        Sdf,
        Msdf,
        // RGB 是 MSDF, A 是单通道真实距离场
        Mtsdf,
    };

    template <> Type* Reflection::createType<FontSdfType>();

    PixelFormat getFontPagePixelFormat(FontSdfType sdfType) noexcept;

    struct FontGlyph
    {
        uint32_t codepoint = 0;
        uint16_t page = 0;
        // 图集页内的像素矩形, 包含 spread 留出的边
        uint16_t x = 0;
        uint16_t y = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        // 单位 em, 图像左上角相对笔位置和基线的偏移, bearingY 向上为正
        float bearingX = 0.0f;
        float bearingY = 0.0f;
        float advance = 0.0f;
    };

    template <> Type* Reflection::createType<FontGlyph>();

    // adjust 单位 em, 加到 left 的 advance 上
    struct FontKerningPair
    {
        uint32_t left = 0;
        uint32_t right = 0;
        float adjust = 0.0f;
    };

    template <> Type* Reflection::createType<FontKerningPair>();

    struct FontCharsetRange
    {
        uint32_t first = 0;
        uint32_t last = 0;
    };

    template <> Type* Reflection::createType<FontCharsetRange>();

    struct FontBakeSettings
    {
        FontSdfType sdfType = FontSdfType::Sdf;
        uint32_t faceIndex = 0;
        uint32_t bakePpem = 48;
        // 距离场向内和向外各覆盖这么多 texel
        float spreadTexels = 8.0f;
        uint32_t pageSize = 1024;
        // 字形矩形之间的额外间隔, 不含 spread
        uint32_t padding = 1;
        bool bakeKerning = true;

        bool includeAscii = true;
        // UTF-8
        std::string characters;
        std::vector<FontCharsetRange> unicodeRanges;
        // 非空时递归扫描该目录下的文本文件, 文件里出现的字符都会被烘焙
        std::string charsDirectory;
        // 分号分隔, 比如 ".txt;.json"
        std::string charsExtensions = ".txt";
    };

    template <> Type* Reflection::createType<FontBakeSettings>();

    struct FontBakeResult;

    uint32_t clampFontBakePpem(uint32_t ppem) noexcept;

    // 非法字节序列直接跳过, 不报错
    std::vector<uint32_t> decodeUtf8(std::string_view text);

    class IFontListener
    {
    public:
        virtual void onFontChanged() = 0;

    protected:
        ~IFontListener() = default;
    };

    class Font : public Object
    {
        REFLECT_CLASS(Font)
    public:
        Font();

        ~Font() noexcept;

        const FontGlyph* findGlyph(uint32_t codepoint) const noexcept;

        const FontGlyph& getGlyphOrNotdef(uint32_t codepoint) const noexcept;

        const FontGlyph& getNotdefGlyph() const noexcept
        {
            return mNotdefGlyph;
        }

        // 单位 em
        float getKerning(uint32_t left, uint32_t right) const noexcept;

        const std::vector<FontGlyph>& getGlyphs() const noexcept
        {
            return mGlyphs;
        }

        const std::vector<FontKerningPair>& getKerningPairs() const noexcept
        {
            return mKerning;
        }

        uint32_t getPageCount() const noexcept
        {
            return mPages ? mPages->getDescription().arrayLayers : 0;
        }

        // 数组的第 i 层就是 FontGlyph::page == i 的那一页
        Texture2DArray* getPages() const noexcept
        {
            return mPages.get();
        }

        float getAscent() const noexcept
        {
            return mAscent;
        }

        // 基线以下为负
        float getDescent() const noexcept
        {
            return mDescent;
        }

        float getLineGap() const noexcept
        {
            return mLineGap;
        }

        float getUnderlinePosition() const noexcept
        {
            return mUnderlinePosition;
        }

        float getUnderlineThickness() const noexcept
        {
            return mUnderlineThickness;
        }

        FontSdfType getSdfType() const noexcept
        {
            return mSdfType;
        }

        uint32_t getBakePpem() const noexcept
        {
            return mBakePpem;
        }

        float getSpreadTexels() const noexcept
        {
            return mSpreadTexels;
        }

        bool hasSourceFace() const noexcept
        {
            return mSourceFace.getSize() != 0;
        }

        const FontBakeSettings& getBakeSettings() const noexcept
        {
            return mBakeSettings;
        }

        void setBakeSettings(const FontBakeSettings& settings) noexcept
        {
            mBakeSettings = settings;
        }

        void setSourceFace(std::span<const uint8_t> faceData);

        std::vector<uint8_t> copySourceFace();

        // 失败或被取消时不改动现有数据
        bool bake(const std::function<bool(float)>& progress = {});

        // 会创建和更新页数组纹理, 只能在主线程调用
        void applyBakeResult(FontBakeResult&& result, const FontBakeSettings& settings);

        void addListener(IFontListener* listener);

        void removeListener(IFontListener* listener);

    private:
        void notifyChanged();

        TRef<Texture2DArray> mPages;
        std::vector<FontGlyph> mGlyphs;
        FontGlyph mNotdefGlyph;
        std::vector<FontKerningPair> mKerning;
        float mAscent = 0.0f;
        float mDescent = 0.0f;
        float mLineGap = 0.0f;
        float mUnderlinePosition = 0.0f;
        float mUnderlineThickness = 0.0f;
        FontSdfType mSdfType = FontSdfType::Sdf;
        uint32_t mBakePpem = 0;
        float mSpreadTexels = 0.0f;

        BulkData mSourceFace;
        FontBakeSettings mBakeSettings;
        std::vector<IFontListener*> mListeners;
    };

    template <> Type* Reflection::createType<Font>();
}
