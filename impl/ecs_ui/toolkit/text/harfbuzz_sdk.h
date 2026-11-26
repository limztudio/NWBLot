// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/containers.h>
#include <global/expected.h>
#include <global/span.h>

#include <hb.h>
#include <hb-ot.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::UiFontSdk{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UiOpenTypeFeature{
    u32 tag = 0u;
    u32 value = 1u;
    u32 start = 0u;
    u32 end = 0xffffffffu;
};

[[nodiscard]] inline u32 HarfBuzzTag(const AStringView tag)noexcept{
    return hb_tag_from_string(tag.data(), static_cast<i32>(Min(tag.size(), static_cast<usize>(4u))));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class HarfBuzzFont final : NoCopy{
    friend class HarfBuzzBuffer;


public:
    HarfBuzzFont()noexcept = default;
    ~HarfBuzzFont()noexcept{ hb_font_destroy(m_font); }


public:
    [[nodiscard]] b8 open(const Span<const u8> bytes, const u32 faceIndex)noexcept{
        if(valid())
            return false;
        if(bytes.size() > Limit<u32>::s_Max)
            return false;
        hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(bytes.data()), static_cast<u32>(bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        hb_face_t* face = hb_face_create(blob, faceIndex);
        m_font = hb_font_create(face);
        hb_face_destroy(face);
        hb_blob_destroy(blob);
        return valid();
    }


public:
    [[nodiscard]] b8 valid()const noexcept{ return m_font != hb_font_get_empty(); }
    [[nodiscard]] u32 unitsPerEm()const noexcept{ return hb_face_get_upem(hb_font_get_face(m_font)); }
    [[nodiscard]] u32 glyphCount()const noexcept{ return hb_face_get_glyph_count(hb_font_get_face(m_font)); }

    [[nodiscard]] u32 tableLength(const u32 tag)const noexcept{
        hb_blob_t* blob = hb_face_reference_table(hb_font_get_face(m_font), tag);
        const u32 size = hb_blob_get_length(blob);
        hb_blob_destroy(blob);
        return size;
    }

    [[nodiscard]] b8 copyTable(const u32 tag, const ::Span<u8> destination)const noexcept{
        hb_blob_t* blob = hb_face_reference_table(hb_font_get_face(m_font), tag);
        u32 size = 0u;
        const char* data = hb_blob_get_data(blob, &size);
        const b8 fits = size == destination.size();
        if(fits && size != 0u)
            FOUNDATION_MEMCPY(destination.data(), destination.size(), data, size);
        hb_blob_destroy(blob);
        return fits;
    }

    void setOpenTypeFunctions()noexcept{ hb_ot_font_set_funcs(m_font); }
    void setScale(const i32 x, const i32 y)noexcept{ hb_font_set_scale(m_font, x, y); }
    [[nodiscard]] b8 horizontalExtents(hb_font_extents_t& extents)const noexcept{ return hb_font_get_h_extents(m_font, &extents); }
    [[nodiscard]] b8 glyphExtents(const u32 glyph, hb_glyph_extents_t& extents)const noexcept{ return hb_font_get_glyph_extents(m_font, glyph, &extents); }
    [[nodiscard]] b8 hasNominalGlyph(const u32 codepoint)const noexcept{
        hb_codepoint_t glyph = 0u;
        return hb_font_get_nominal_glyph(m_font, codepoint, &glyph) != 0 && glyph != 0u;
    }
    [[nodiscard]] b8 hasVariationGlyph(const u32 codepoint, const u32 variationSelector)const noexcept{
        hb_codepoint_t glyph = 0u;
        return hb_font_get_variation_glyph(m_font, codepoint, variationSelector, &glyph) != 0 && glyph != 0u;
    }


private:
    hb_font_t* m_font = hb_font_get_empty();
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class HarfBuzzBuffer final : NoCopy{
public:
    HarfBuzzBuffer()noexcept = default;
    ~HarfBuzzBuffer()noexcept{ hb_buffer_destroy(m_buffer); }


public:
    [[nodiscard]] b8 valid()const noexcept{ return hb_buffer_allocation_successful(m_buffer); }
    void clear()noexcept{ hb_buffer_clear_contents(m_buffer); }
    void setDirection(const hb_direction_t direction)noexcept{ hb_buffer_set_direction(m_buffer, direction); }
    void setScript(const hb_script_t script)noexcept{ hb_buffer_set_script(m_buffer, script); }
    void setClusterLevel(const hb_buffer_cluster_level_t level)noexcept{ hb_buffer_set_cluster_level(m_buffer, level); }

    [[nodiscard]] b8 setLanguage(const AStringView language)noexcept{
        if(language.size() > static_cast<usize>(Limit<i32>::s_Max))
            return false;
        hb_buffer_set_language(m_buffer, hb_language_from_string(language.data(), static_cast<i32>(language.size())));
        return true;
    }
    [[nodiscard]] b8 addUtf8(const AStringView text, const u32 offset, const u32 count)noexcept{
        if(text.size() > static_cast<usize>(Limit<i32>::s_Max) || offset > text.size() || count > text.size() - offset)
            return false;
        hb_buffer_add_utf8(m_buffer, text.data(), static_cast<i32>(text.size()), offset, static_cast<i32>(count));
        return true;
    }
    void shape(const HarfBuzzFont& font)noexcept{ hb_shape(font.m_font, m_buffer, nullptr, 0u); }
    void shapeWithFeatures(const HarfBuzzFont& font, const Span<const UiOpenTypeFeature> features)noexcept{
        hb_feature_t stackFeatures[16]{};
        const usize count = Min<usize>(features.size(), 16u);
        for(usize i = 0u; i < count; ++i){
            stackFeatures[i].tag = features[i].tag;
            stackFeatures[i].value = features[i].value;
            stackFeatures[i].start = features[i].start;
            stackFeatures[i].end = features[i].end;
        }
        if(count != 0u)
            hb_shape(font.m_font, m_buffer, stackFeatures, static_cast<u32>(count));
        else
            hb_shape(font.m_font, m_buffer, nullptr, 0u);
    }

    [[nodiscard]] Span<const hb_glyph_info_t> glyphInfos()const noexcept{
        u32 count = 0u;
        const auto* infos = hb_buffer_get_glyph_infos(m_buffer, &count);
        return { infos, count };
    }
    [[nodiscard]] Span<const hb_glyph_position_t> glyphPositions()const noexcept{
        u32 count = 0u;
        const auto* positions = hb_buffer_get_glyph_positions(m_buffer, &count);
        return { positions, count };
    }


private:
    hb_buffer_t* m_buffer = hb_buffer_create();
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

