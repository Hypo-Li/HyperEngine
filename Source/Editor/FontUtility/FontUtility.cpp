#include "FontUtility.h"
#include "FontKerning.h"

#include <Core/Common/LogSystem.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <msdfgen.h>
#include <msdfgen-ext.h>

#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace hyp
{
    namespace
    {
        class FreeTypeFace
        {
        public:
            FreeTypeFace() = default;
            FreeTypeFace(const FreeTypeFace&) = delete;
            FreeTypeFace& operator=(const FreeTypeFace&) = delete;

            ~FreeTypeFace()
            {
                if (mFont)
                    msdfgen::destroyFont(mFont);
                if (mFace)
                    FT_Done_Face(mFace);
                if (mLibrary)
                    FT_Done_FreeType(mLibrary);
            }

            bool open(std::span<const uint8_t> faceData, uint32_t faceIndex)
            {
                if (FT_Init_FreeType(&mLibrary) != 0)
                    return false;
                if (FT_New_Memory_Face(mLibrary, faceData.data(), static_cast<FT_Long>(faceData.size()), static_cast<FT_Long>(faceIndex), &mFace) != 0)
                    return false;
                if (!FT_IS_SCALABLE(mFace))
                    return false;
                mFont = msdfgen::adoptFreetypeFont(mFace);
                return mFont != nullptr;
            }

            FT_Face face() const noexcept
            {
                return mFace;
            }

            msdfgen::FontHandle* font() const noexcept
            {
                return mFont;
            }

        private:
            FT_Library mLibrary = nullptr;
            FT_Face mFace = nullptr;
            msdfgen::FontHandle* mFont = nullptr;
        };

        uint32_t channelCountOf(FontSdfType sdfType) noexcept
        {
            switch (sdfType)
            {
            case FontSdfType::Sdf: return 1;
            case FontSdfType::Msdf: return 3;
            case FontSdfType::Mtsdf: return 4;
            }
            return 0;
        }

        std::vector<uint32_t> collectCodepoints(const FontBakeSettings& settings)
        {
            std::vector<uint32_t> codepoints;

            if (settings.includeAscii)
            {
                for (uint32_t c = 0x20; c <= 0x7E; ++c)
                    codepoints.push_back(c);
            }

            for (uint32_t c : decodeUtf8(settings.characters))
                codepoints.push_back(c);

            for (const FontCharsetRange& range : settings.unicodeRanges)
            {
                const uint32_t last = std::min(range.last, 0x10FFFFu);
                for (uint32_t c = range.first; c <= last; ++c)
                    codepoints.push_back(c);
            }

            if (!settings.charsDirectory.empty())
            {
                std::vector<std::string> extensions;
                size_t start = 0;
                while (start <= settings.charsExtensions.size())
                {
                    size_t end = settings.charsExtensions.find(';', start);
                    if (end == std::string::npos)
                        end = settings.charsExtensions.size();
                    std::string extension = settings.charsExtensions.substr(start, end - start);
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (!extension.empty())
                        extensions.push_back(std::move(extension));
                    start = end + 1;
                }

                std::error_code error;
                std::filesystem::recursive_directory_iterator it(
                    std::filesystem::path(reinterpret_cast<const char8_t*>(settings.charsDirectory.c_str())),
                    std::filesystem::directory_options::skip_permission_denied,
                    error);
                for (; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
                {
                    if (!it->is_regular_file(error))
                        continue;

                    std::string extension = it->path().extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (std::find(extensions.begin(), extensions.end(), extension) == extensions.end())
                        continue;

                    std::ifstream file(it->path(), std::ios::binary);
                    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    for (uint32_t c : decodeUtf8(content))
                        codepoints.push_back(c);
                }
            }

            std::erase_if(codepoints, [](uint32_t c) { return c < 0x20 || (c >= 0x7F && c <= 0x9F); });
            std::sort(codepoints.begin(), codepoints.end());
            codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());
            return codepoints;
        }

        struct GlyphWork
        {
            uint32_t codepoint = 0;
            msdfgen::Shape shape;
            double advance = 0.0;
            bool hasImage = false;
            double left = 0.0;
            double bottom = 0.0;
            int width = 0;
            int height = 0;
            int page = 0;
            int x = 0;
            int y = 0;
        };

        bool loadGlyphWork(GlyphWork& work, const FreeTypeFace& face, FT_UInt glyphIndex, uint32_t ppem, int pad)
        {
            if (!msdfgen::loadGlyph(work.shape, face.font(), msdfgen::GlyphIndex(glyphIndex), msdfgen::FONT_SCALING_EM_NORMALIZED, &work.advance))
                return false;

            if (work.shape.edgeCount() == 0)
                return true;

            work.shape.normalize();
            const msdfgen::Shape::Bounds bounds = work.shape.getBounds();
            work.hasImage = true;
            work.left = bounds.l;
            work.bottom = bounds.b;
            work.width = static_cast<int>(std::ceil((bounds.r - bounds.l) * ppem)) + 2 * pad;
            work.height = static_cast<int>(std::ceil((bounds.t - bounds.b) * ppem)) + 2 * pad;
            return true;
        }

        uint8_t toByte(float value) noexcept
        {
            return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        template <int N>
        void copyToPage(const std::vector<float>& image, const GlyphWork& work, uint32_t pageSize, uint32_t pageChannels, std::vector<uint8_t>& page)
        {
            for (int row = 0; row < work.height; ++row)
            {
                for (int column = 0; column < work.width; ++column)
                {
                    const float* source = &image[(static_cast<size_t>(row) * work.width + column) * N];
                    uint8_t* destination = &page[((static_cast<size_t>(work.y) + row) * pageSize + work.x + column) * pageChannels];
                    for (int channel = 0; channel < N; ++channel)
                        destination[channel] = toByte(source[channel]);
                }
            }
        }

        void generateGlyph(GlyphWork& work, const FontBakeSettings& settings, uint32_t ppem, int pad, std::vector<uint8_t>& page)
        {
            const double scale = ppem;
            const msdfgen::SDFTransformation transformation(
                msdfgen::Projection(msdfgen::Vector2(scale), msdfgen::Vector2(-work.left + pad / scale, -work.bottom + pad / scale)),
                msdfgen::DistanceMapping(msdfgen::Range(2.0 * settings.spreadTexels / scale)));

            const size_t pixelCount = static_cast<size_t>(work.width) * work.height;
            const uint32_t pageChannels = channelCountOf(settings.sdfType);

            switch (settings.sdfType)
            {
            case FontSdfType::Sdf:
            {
                std::vector<float> image(pixelCount);
                msdfgen::generateSDF(
                    msdfgen::BitmapSection<float, 1>(image.data(), work.width, work.height, msdfgen::Y_DOWNWARD),
                    work.shape, transformation);
                copyToPage<1>(image, work, settings.pageSize, pageChannels, page);
                break;
            }
            case FontSdfType::Msdf:
            {
                std::vector<float> image(pixelCount * 3);
                msdfgen::edgeColoringSimple(work.shape, 3.0);
                msdfgen::generateMSDF(
                    msdfgen::BitmapSection<float, 3>(image.data(), work.width, work.height, msdfgen::Y_DOWNWARD),
                    work.shape, transformation);
                copyToPage<3>(image, work, settings.pageSize, pageChannels, page);
                break;
            }
            case FontSdfType::Mtsdf:
            {
                std::vector<float> image(pixelCount * 4);
                msdfgen::edgeColoringSimple(work.shape, 3.0);
                msdfgen::generateMTSDF(
                    msdfgen::BitmapSection<float, 4>(image.data(), work.width, work.height, msdfgen::Y_DOWNWARD),
                    work.shape, transformation);
                copyToPage<4>(image, work, settings.pageSize, pageChannels, page);
                break;
            }
            }
        }

        FontGlyph makeGlyph(const GlyphWork& work, uint32_t ppem, int pad)
        {
            FontGlyph glyph;
            glyph.codepoint = work.codepoint;
            glyph.advance = static_cast<float>(work.advance);
            if (work.hasImage)
            {
                glyph.page = static_cast<uint16_t>(work.page);
                glyph.x = static_cast<uint16_t>(work.x);
                glyph.y = static_cast<uint16_t>(work.y);
                glyph.width = static_cast<uint16_t>(work.width);
                glyph.height = static_cast<uint16_t>(work.height);
                glyph.bearingX = static_cast<float>(work.left - static_cast<double>(pad) / ppem);
                glyph.bearingY = static_cast<float>(work.bottom + static_cast<double>(work.height - pad) / ppem);
            }
            return glyph;
        }

        bool packGlyphs(std::vector<GlyphWork*>& works, uint32_t pageSize, uint32_t padding, uint32_t& pageCount)
        {
            std::vector<stbrp_rect> pending;
            pending.reserve(works.size());
            for (size_t i = 0; i < works.size(); ++i)
            {
                stbrp_rect rect{};
                rect.id = static_cast<int>(i);
                rect.w = works[i]->width + static_cast<int>(padding);
                rect.h = works[i]->height + static_cast<int>(padding);
                if (rect.w > static_cast<int>(pageSize) || rect.h > static_cast<int>(pageSize))
                    return false;
                pending.push_back(rect);
            }

            std::vector<stbrp_node> nodes(pageSize);
            pageCount = 0;
            while (!pending.empty())
            {
                stbrp_context context;
                stbrp_init_target(&context, static_cast<int>(pageSize), static_cast<int>(pageSize), nodes.data(), static_cast<int>(nodes.size()));
                stbrp_pack_rects(&context, pending.data(), static_cast<int>(pending.size()));

                std::vector<stbrp_rect> unpacked;
                for (const stbrp_rect& rect : pending)
                {
                    if (rect.was_packed)
                    {
                        GlyphWork& work = *works[rect.id];
                        work.page = static_cast<int>(pageCount);
                        work.x = rect.x;
                        work.y = rect.y;
                    }
                    else
                    {
                        unpacked.push_back(rect);
                    }
                }

                if (unpacked.size() == pending.size())
                    return false;

                pending = std::move(unpacked);
                ++pageCount;
            }
            return true;
        }
    }

    std::optional<FontFaceInfo> FontUtility::inspectFace(std::span<const uint8_t> faceData, uint32_t faceIndex)
    {
        FreeTypeFace face;
        if (!face.open(faceData, faceIndex))
            return std::nullopt;

        FontFaceInfo info;
        info.familyName = face.face()->family_name ? face.face()->family_name : "";
        info.styleName = face.face()->style_name ? face.face()->style_name : "";
        info.faceCount = static_cast<uint32_t>(face.face()->num_faces);
        info.glyphCount = static_cast<uint32_t>(face.face()->num_glyphs);
        info.unitsPerEm = face.face()->units_per_EM;
        return info;
    }

    std::optional<FontBakeResult> FontUtility::bake(
        std::span<const uint8_t> faceData,
        const FontBakeSettings& settings,
        const std::function<bool(float)>& progress)
    {
        const uint32_t ppem = clampFontBakePpem(settings.bakePpem);
        const int pad = static_cast<int>(std::ceil(settings.spreadTexels));
        if (settings.spreadTexels <= 0.0f || settings.pageSize == 0)
            return std::nullopt;

        auto report = [&progress](float value) { return !progress || progress(value); };

        FreeTypeFace face;
        if (!face.open(faceData, settings.faceIndex))
        {
            LOG_ERROR("FontUtility: failed to open face {}", settings.faceIndex);
            return std::nullopt;
        }

        msdfgen::FontMetrics metrics;
        if (!msdfgen::getFontMetrics(metrics, face.font(), msdfgen::FONT_SCALING_EM_NORMALIZED))
            return std::nullopt;

        FontBakeResult result;
        result.ascent = static_cast<float>(metrics.ascenderY);
        result.descent = static_cast<float>(metrics.descenderY);
        result.lineGap = std::max(0.0f, static_cast<float>(metrics.lineHeight - (metrics.ascenderY - metrics.descenderY)));
        result.underlinePosition = static_cast<float>(metrics.underlineY);
        result.underlineThickness = static_cast<float>(metrics.underlineThickness);

        const std::vector<uint32_t> requested = collectCodepoints(settings);

        std::vector<GlyphWork> works;
        works.reserve(requested.size() + 1);

        GlyphWork& notdef = works.emplace_back();
        loadGlyphWork(notdef, face, 0, ppem, pad);

        for (size_t i = 0; i < requested.size(); ++i)
        {
            if (!report(0.1f * static_cast<float>(i) / static_cast<float>(requested.size())))
                return std::nullopt;

            const FT_UInt glyphIndex = FT_Get_Char_Index(face.face(), requested[i]);
            GlyphWork work;
            work.codepoint = requested[i];
            if (glyphIndex == 0 || !loadGlyphWork(work, face, glyphIndex, ppem, pad))
            {
                result.missingCodepoints.push_back(requested[i]);
                continue;
            }
            works.push_back(std::move(work));
        }

        std::vector<GlyphWork*> imageWorks;
        for (GlyphWork& work : works)
        {
            if (work.hasImage)
                imageWorks.push_back(&work);
        }

        uint32_t pageCount = 0;
        if (!packGlyphs(imageWorks, settings.pageSize, settings.padding, pageCount))
        {
            LOG_ERROR("FontUtility: a glyph does not fit in a {0}x{0} page, increase Page Size or lower Bake Ppem", settings.pageSize);
            return std::nullopt;
        }

        const size_t pageBytes = static_cast<size_t>(settings.pageSize) * settings.pageSize * channelCountOf(settings.sdfType);
        result.pages.assign(pageCount, std::vector<uint8_t>(pageBytes, 0));

        std::atomic<size_t> nextIndex{ 0 };
        std::atomic<size_t> doneCount{ 0 };
        std::atomic<bool> cancelled{ false };

        auto worker = [&]()
        {
            for (;;)
            {
                if (cancelled.load())
                    return;
                const size_t index = nextIndex.fetch_add(1);
                if (index >= imageWorks.size())
                    return;
                GlyphWork& work = *imageWorks[index];
                generateGlyph(work, settings, ppem, pad, result.pages[work.page]);
                doneCount.fetch_add(1);
                doneCount.notify_one();
            }
        };

        {
            const unsigned int threadCount = std::max(1u, std::thread::hardware_concurrency());
            std::vector<std::jthread> threads;
            threads.reserve(threadCount);
            for (unsigned int i = 0; i < threadCount; ++i)
                threads.emplace_back(worker);

            for (;;)
            {
                const size_t done = doneCount.load();
                if (done >= imageWorks.size())
                    break;
                if (!report(0.1f + 0.8f * static_cast<float>(done) / static_cast<float>(imageWorks.size())))
                {
                    cancelled.store(true);
                    return std::nullopt;
                }
                doneCount.wait(done);
            }
        }

        result.notdefGlyph = makeGlyph(works.front(), ppem, pad);
        result.glyphs.reserve(works.size() - 1);
        for (size_t i = 1; i < works.size(); ++i)
            result.glyphs.push_back(makeGlyph(works[i], ppem, pad));

        if (settings.bakeKerning)
        {
            std::vector<uint32_t> present;
            present.reserve(result.glyphs.size());
            for (const FontGlyph& glyph : result.glyphs)
                present.push_back(glyph.codepoint);

            std::optional<std::vector<FontKerningPair>> kerning = bakeKerningPairs(faceData, settings.faceIndex, present,
                [&report](float fraction) { return report(0.9f + 0.1f * fraction); });
            if (!kerning)
                return std::nullopt;
            result.kerning = std::move(*kerning);
        }

        report(1.0f);
        return result;
    }
}
