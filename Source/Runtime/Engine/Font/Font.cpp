#include "Font.h"
#include "FontUtility.h"

#include <Core/Common/Compressor.h>
#include <Core/Common/ModuleServiceRegistry.h>

#include <algorithm>

namespace hyp
{
    REGISTER_CLASS(Font)

    template <> Type* Reflection::createType<FontSdfType>()
    {
        auto e = new TEnumeration<FontSdfType>("FontSdfType");
        e->addValue("SDF", FontSdfType::Sdf);
        e->addValue("MSDF", FontSdfType::Msdf);
        e->addValue("MTSDF", FontSdfType::Mtsdf);
        return e;
    }

    template <> Type* Reflection::createType<FontGlyph>()
    {
        auto s = new TStructure<FontGlyph>("FontGlyph");
        s->addProperty("Codepoint", &FontGlyph::codepoint);
        s->addProperty("Page", &FontGlyph::page);
        s->addProperty("X", &FontGlyph::x);
        s->addProperty("Y", &FontGlyph::y);
        s->addProperty("Width", &FontGlyph::width);
        s->addProperty("Height", &FontGlyph::height);
        s->addProperty("Bearing X", &FontGlyph::bearingX);
        s->addProperty("Bearing Y", &FontGlyph::bearingY);
        s->addProperty("Advance", &FontGlyph::advance);
        return s;
    }

    template <> Type* Reflection::createType<FontKerningPair>()
    {
        auto s = new TStructure<FontKerningPair>("FontKerningPair");
        s->addProperty("Left", &FontKerningPair::left);
        s->addProperty("Right", &FontKerningPair::right);
        s->addProperty("Adjust", &FontKerningPair::adjust);
        return s;
    }

    template <> Type* Reflection::createType<FontCharsetRange>()
    {
        auto s = new TStructure<FontCharsetRange>("FontCharsetRange");
        s->addProperty("First", &FontCharsetRange::first);
        s->addProperty("Last", &FontCharsetRange::last);
        return s;
    }

    template <> Type* Reflection::createType<FontBakeSettings>()
    {
        auto s = new TStructure<FontBakeSettings>("FontBakeSettings");
        s->addProperty("SDF Type", &FontBakeSettings::sdfType);
        s->addProperty("Face Index", &FontBakeSettings::faceIndex);
        s->addProperty("Bake Ppem", &FontBakeSettings::bakePpem);
        s->addProperty("Spread Texels", &FontBakeSettings::spreadTexels);
        s->addProperty("Page Size", &FontBakeSettings::pageSize);
        s->addProperty("Padding", &FontBakeSettings::padding);
        s->addProperty("Bake Kerning", &FontBakeSettings::bakeKerning);
        s->addProperty("Include ASCII", &FontBakeSettings::includeAscii);
        s->addProperty("Characters", &FontBakeSettings::characters);
        s->addProperty("Unicode Ranges", &FontBakeSettings::unicodeRanges);
        s->addProperty("Chars Directory", &FontBakeSettings::charsDirectory);
        s->addProperty("Chars Extensions", &FontBakeSettings::charsExtensions);
        return s;
    }

    PixelFormat getFontPagePixelFormat(FontSdfType sdfType) noexcept
    {
        switch (sdfType)
        {
        case FontSdfType::Sdf: return PixelFormat::R8_UNORM;
        case FontSdfType::Msdf: return PixelFormat::RGB8_UNORM;
        case FontSdfType::Mtsdf: return PixelFormat::RGBA8_UNORM;
        }
        return PixelFormat::Undefined;
    }

    uint32_t clampFontBakePpem(uint32_t ppem) noexcept
    {
        return std::clamp((ppem + 3) / 4 * 4, 8u, 256u);
    }

    std::vector<uint32_t> decodeUtf8(std::string_view text)
    {
        std::vector<uint32_t> codepoints;
        codepoints.reserve(text.size());

        size_t i = 0;
        while (i < text.size())
        {
            const uint8_t lead = static_cast<uint8_t>(text[i]);

            uint32_t length = 0;
            uint32_t codepoint = 0;
            uint32_t minimum = 0;
            if (lead < 0x80)
            {
                length = 1;
                codepoint = lead;
            }
            else if ((lead & 0xE0) == 0xC0)
            {
                length = 2;
                codepoint = lead & 0x1F;
                minimum = 0x80;
            }
            else if ((lead & 0xF0) == 0xE0)
            {
                length = 3;
                codepoint = lead & 0x0F;
                minimum = 0x800;
            }
            else if ((lead & 0xF8) == 0xF0)
            {
                length = 4;
                codepoint = lead & 0x07;
                minimum = 0x10000;
            }
            else
            {
                ++i;
                continue;
            }

            if (i + length > text.size())
                break;

            bool valid = true;
            for (uint32_t k = 1; k < length; ++k)
            {
                const uint8_t continuation = static_cast<uint8_t>(text[i + k]);
                if ((continuation & 0xC0) != 0x80)
                {
                    valid = false;
                    break;
                }
                codepoint = (codepoint << 6) | (continuation & 0x3F);
            }

            if (!valid || codepoint < minimum || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            {
                ++i;
                continue;
            }

            codepoints.push_back(codepoint);
            i += length;
        }

        return codepoints;
    }

    Font::Font()
    {
    }

    Font::~Font() noexcept
    {
    }

    const FontGlyph* Font::findGlyph(uint32_t codepoint) const noexcept
    {
        auto it = std::lower_bound(mGlyphs.begin(), mGlyphs.end(), codepoint,
            [](const FontGlyph& glyph, uint32_t value) { return glyph.codepoint < value; });
        if (it == mGlyphs.end() || it->codepoint != codepoint)
            return nullptr;
        return &*it;
    }

    const FontGlyph& Font::getGlyphOrNotdef(uint32_t codepoint) const noexcept
    {
        const FontGlyph* glyph = findGlyph(codepoint);
        return glyph ? *glyph : mNotdefGlyph;
    }

    float Font::getKerning(uint32_t left, uint32_t right) const noexcept
    {
        auto it = std::lower_bound(mKerning.begin(), mKerning.end(), std::make_pair(left, right),
            [](const FontKerningPair& pair, const std::pair<uint32_t, uint32_t>& key)
            {
                return std::make_pair(pair.left, pair.right) < key;
            });
        if (it == mKerning.end() || it->left != left || it->right != right)
            return 0.0f;
        return it->adjust;
    }

    void Font::setSourceFace(std::span<const uint8_t> faceData)
    {
        mSourceFace.setData(Compressor::compress(faceData));
    }

    std::vector<uint8_t> Font::copySourceFace()
    {
        bool wasLoaded = mSourceFace.isLoaded();
        if (!wasLoaded)
            mSourceFace.load();

        std::vector<uint8_t> faceData = Compressor::decompress(mSourceFace.getData());

        if (!wasLoaded)
            mSourceFace.unload();

        return faceData;
    }

    bool Font::bake(const std::function<bool(float)>& progress)
    {
        IFontUtility* fontUtility = ModuleServiceRegistry::get().getService<IFontUtility>();
        if (!fontUtility || !hasSourceFace())
            return false;

        std::vector<uint8_t> faceData = copySourceFace();
        std::optional<FontBakeResult> result = fontUtility->bake(faceData, mBakeSettings, progress);
        if (!result)
            return false;

        applyBakeResult(std::move(*result), mBakeSettings);
        return true;
    }

    void Font::applyBakeResult(FontBakeResult&& result, const FontBakeSettings& settings)
    {
        mBakeSettings = settings;
        mGlyphs = std::move(result.glyphs);
        mNotdefGlyph = result.notdefGlyph;
        mKerning = std::move(result.kerning);
        mAscent = result.ascent;
        mDescent = result.descent;
        mLineGap = result.lineGap;
        mUnderlinePosition = result.underlinePosition;
        mUnderlineThickness = result.underlineThickness;
        mSdfType = settings.sdfType;
        mBakePpem = clampFontBakePpem(settings.bakePpem);
        mSpreadTexels = settings.spreadTexels;

        if (result.pages.empty())
        {
            mPages = nullptr;
            notifyChanged();
            return;
        }

        SampleState pageSampleState;
        pageSampleState.filter = Filter::Bilinear;
        pageSampleState.addressModeU = AddressMode::Clamp;
        pageSampleState.addressModeV = AddressMode::Clamp;
        pageSampleState.addressModeW = AddressMode::Clamp;

        std::vector<uint8_t> pixels;
        pixels.reserve(result.pages.size() * result.pages.front().size());
        for (const std::vector<uint8_t>& page : result.pages)
            pixels.insert(pixels.end(), page.begin(), page.end());

        if (!mPages)
            mPages = createObject<Texture2DArray>(this);

        mPages->initialize(settings.pageSize, settings.pageSize, static_cast<uint32_t>(result.pages.size()), getFontPagePixelFormat(mSdfType), 1);
        mPages->setSRGB(false);
        mPages->setCompressionPreset(CompressionPreset::None);
        mPages->setSampleState(pageSampleState);
        mPages->setSourcePixels(std::move(pixels));
        mPages->bakeMips();
        mPages->uploadToGpu();
        notifyChanged();
    }

    void Font::addListener(IFontListener* listener)
    {
        assert(std::find(mListeners.begin(), mListeners.end(), listener) == mListeners.end());
        mListeners.push_back(listener);
    }

    void Font::removeListener(IFontListener* listener)
    {
        auto it = std::find(mListeners.begin(), mListeners.end(), listener);
        assert(it != mListeners.end());
        mListeners.erase(it);
    }

    void Font::notifyChanged()
    {
        for (IFontListener* listener : mListeners)
            listener->onFontChanged();
    }

    template <> Type* Reflection::createType<Font>()
    {
        auto c = new TClass<Font, Object>("Font");
        c->setDisplayName("Font");
        c->addProperty("Pages", &Font::mPages, CopyPolicy::Deep);
        c->addProperty("Glyphs", &Font::mGlyphs);
        c->addProperty("Notdef Glyph", &Font::mNotdefGlyph);
        c->addProperty("Kerning", &Font::mKerning);
        c->addProperty("Ascent", &Font::mAscent);
        c->addProperty("Descent", &Font::mDescent);
        c->addProperty("Line Gap", &Font::mLineGap);
        c->addProperty("Underline Position", &Font::mUnderlinePosition);
        c->addProperty("Underline Thickness", &Font::mUnderlineThickness);
        c->addProperty("SDF Type", &Font::mSdfType);
        c->addProperty("Bake Ppem", &Font::mBakePpem);
        c->addProperty("Spread Texels", &Font::mSpreadTexels);
        c->addProperty("Source Face", &Font::mSourceFace, CopyPolicy::Shallow, PropertyFlags::Editor_Only);
        c->addProperty("Bake Settings", &Font::mBakeSettings, CopyPolicy::Shallow, PropertyFlags::Editor_Only);
        return c;
    }
}
