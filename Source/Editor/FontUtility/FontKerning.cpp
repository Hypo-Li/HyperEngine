#include "FontKerning.h"

#include <hb.h>
#include <hb-ot.h>

#include <memory>

namespace hyp
{
    namespace
    {
        struct HbDeleter
        {
            void operator()(hb_face_t* face) const noexcept { hb_face_destroy(face); }
            void operator()(hb_font_t* font) const noexcept { hb_font_destroy(font); }
            void operator()(hb_buffer_t* buffer) const noexcept { hb_buffer_destroy(buffer); }
            void operator()(hb_set_t* set) const noexcept { hb_set_destroy(set); }
        };

        template <typename T>
        using HbPtr = std::unique_ptr<T, HbDeleter>;

        struct Participant
        {
            uint32_t codepoint;
            hb_codepoint_t glyph;
            hb_position_t advance;
        };
    }

    std::optional<std::vector<FontKerningPair>> bakeKerningPairs(
        std::span<const uint8_t> faceData,
        uint32_t faceIndex,
        std::span<const uint32_t> codepoints,
        const std::function<bool(float)>& progress)
    {
        hb_blob_t* blob = hb_blob_create(
            reinterpret_cast<const char*>(faceData.data()),
            static_cast<unsigned int>(faceData.size()),
            HB_MEMORY_MODE_READONLY,
            nullptr,
            nullptr);
        HbPtr<hb_face_t> face(hb_face_create(blob, faceIndex));
        hb_blob_destroy(blob);

        HbPtr<hb_font_t> font(hb_font_create(face.get()));
        const unsigned int unitsPerEm = hb_face_get_upem(face.get());
        hb_font_set_scale(font.get(), static_cast<int>(unitsPerEm), static_cast<int>(unitsPerEm));
        hb_ot_font_set_funcs(font.get());

        HbPtr<hb_set_t> lookups(hb_set_create());
        const hb_tag_t kernFeature[] = { HB_TAG('k', 'e', 'r', 'n'), HB_TAG_NONE };
        hb_ot_layout_collect_lookups(face.get(), HB_OT_TAG_GPOS, nullptr, nullptr, kernFeature, lookups.get());

        HbPtr<hb_set_t> involvedGlyphs(hb_set_create());
        for (hb_codepoint_t lookup = HB_SET_VALUE_INVALID; hb_set_next(lookups.get(), &lookup);)
        {
            hb_ot_layout_lookup_collect_glyphs(
                face.get(), HB_OT_TAG_GPOS, lookup,
                involvedGlyphs.get(), involvedGlyphs.get(), involvedGlyphs.get(), involvedGlyphs.get());
        }

        std::vector<Participant> participants;
        for (uint32_t codepoint : codepoints)
        {
            hb_codepoint_t glyph = 0;
            if (hb_font_get_nominal_glyph(font.get(), codepoint, &glyph) && hb_set_has(involvedGlyphs.get(), glyph))
                participants.push_back({ codepoint, glyph, hb_font_get_glyph_h_advance(font.get(), glyph) });
        }

        hb_feature_t noLigatures[2];
        hb_feature_from_string("-liga", -1, &noLigatures[0]);
        hb_feature_from_string("-clig", -1, &noLigatures[1]);

        HbPtr<hb_buffer_t> buffer(hb_buffer_create());
        std::vector<FontKerningPair> pairs;

        for (size_t i = 0; i < participants.size(); ++i)
        {
            if (progress && !progress(static_cast<float>(i) / static_cast<float>(participants.size())))
                return std::nullopt;

            for (const Participant& right : participants)
            {
                const Participant& left = participants[i];

                hb_buffer_reset(buffer.get());
                const uint32_t pairText[2] = { left.codepoint, right.codepoint };
                hb_buffer_add_utf32(buffer.get(), pairText, 2, 0, 2);
                hb_buffer_set_direction(buffer.get(), HB_DIRECTION_LTR);
                hb_buffer_guess_segment_properties(buffer.get());
                hb_shape(font.get(), buffer.get(), noLigatures, 2);

                unsigned int count = 0;
                const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer.get(), &count);
                const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer.get(), &count);
                if (count != 2 || infos[0].codepoint != left.glyph || infos[1].codepoint != right.glyph)
                    continue;

                const hb_position_t delta = positions[0].x_advance - left.advance;
                if (delta == 0)
                    continue;

                pairs.push_back({ left.codepoint, right.codepoint, static_cast<float>(delta) / static_cast<float>(unitsPerEm) });
            }
        }

        return pairs;
    }
}
