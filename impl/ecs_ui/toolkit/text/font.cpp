// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "font.h"

#include <logger/client/module.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_OUTLINE_H
#include <hb.h>
#include <hb-ot.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_font{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool HasTable(hb_face_t& face, hb_tag_t tag){
    hb_blob_t* blob = hb_face_reference_table(&face, tag);
    const bool present = hb_blob_get_length(blob) != 0u;
    hb_blob_destroy(blob);
    return present;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ScaledFont final : NoCopy{
public:
    ScaledFont(hb_face_t& face, f32 size)
        : m_font(hb_font_create(&face))
    {
        hb_ot_font_set_funcs(m_font);
        const i32 scale = static_cast<i32>(size * 64.0f + 0.5f);
        hb_font_set_scale(m_font, scale, scale);
    }
    ~ScaledFont(){ hb_font_destroy(m_font); }


public:
    [[nodiscard]] bool valid()const{ return m_font != hb_font_get_empty(); }
    [[nodiscard]] hb_font_t& get()const{ return *m_font; }


private:
    hb_font_t* m_font;
};

class ShapeBuffer final : NoCopy{
public:
    ShapeBuffer()
        : m_buffer(hb_buffer_create())
    {}
    ~ShapeBuffer(){ hb_buffer_destroy(m_buffer); }


public:
    [[nodiscard]] hb_buffer_t& get()const{ return *m_buffer; }


private:
    hb_buffer_t* m_buffer;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FontFaceState final : NoCopy{
    friend class FontFace;


private:
    [[nodiscard]] static void* Allocate(FT_Memory memory, FT_Long size){
        if(size <= 0)
            return nullptr;
        auto& arena = *static_cast<Core::Alloc::GlobalArena*>(memory->user);
        return arena.allocate(alignof(MaxAlign), static_cast<usize>(size));
    }

    static void Release(FT_Memory memory, void* block){
        if(block){
            auto& arena = *static_cast<Core::Alloc::GlobalArena*>(memory->user);
            arena.deallocate(block, alignof(MaxAlign), 0u);
        }
    }

    [[nodiscard]] static void* Reallocate(FT_Memory memory, FT_Long oldSize, FT_Long newSize, void* block){
        static_cast<void>(oldSize);
        if(newSize <= 0){
            Release(memory, block);
            return nullptr;
        }
        auto& arena = *static_cast<Core::Alloc::GlobalArena*>(memory->user);
        return arena.reallocate(block, alignof(MaxAlign), static_cast<usize>(newSize));
    }


public:
    FontFaceState(Core::Alloc::GlobalArena& arena, const FontSource& source)
        : m_identity(source.identity)
        , m_generation(source.generation)
        , m_bytes(arena)
    {
        const Core::Assets::AssetBytes& sourceBytes = source.font.fontBytes();
        m_bytes.assign(sourceBytes.begin(), sourceBytes.end());
        m_memory.user = &arena;
        m_memory.alloc = &Allocate;
        m_memory.free = &Release;
        m_memory.realloc = &Reallocate;
        if(FT_New_Library(&m_memory, &m_library) != 0){
            NWB_LOGGER_ERROR(GLB_TEXT("UI FreeType initialization failed"));
            return;
        }
        FT_Add_Default_Modules(m_library);
        if(FT_New_Memory_Face(m_library, m_bytes.data(), static_cast<FT_Long>(m_bytes.size()), source.font.faceIndex(), &m_face) != 0){
            NWB_LOGGER_ERROR(GLB_TEXT("UI font face initialization failed"));
            return;
        }
        if(!FT_IS_SCALABLE(m_face) || m_face->units_per_EM == 0u){
            NWB_LOGGER_ERROR(GLB_TEXT("UI font must have scalable horizontal metrics"));
            return;
        }
        m_blob = hb_blob_create(
            reinterpret_cast<const char*>(m_bytes.data()),
            static_cast<u32>(m_bytes.size()),
            HB_MEMORY_MODE_READONLY,
            nullptr,
            nullptr
        );
        m_hbFace = hb_face_create(m_blob, source.font.faceIndex());
        m_unitsPerEm = hb_face_get_upem(m_hbFace);
        m_ready = hb_face_get_glyph_count(m_hbFace) != 0u && m_unitsPerEm != 0u;
        if(!m_ready){
            NWB_LOGGER_ERROR(GLB_TEXT("UI HarfBuzz font face initialization failed"));
            return;
        }
        m_allowOutlineBounds = !FT_IS_TRICKY(m_face);
        m_coverageInkReliable =
            m_allowOutlineBounds && __hidden_ui_text_font::HasTable(*m_hbFace, HB_TAG('g', 'l', 'y', 'f'))
            && !__hidden_ui_text_font::HasTable(*m_hbFace, HB_TAG('C', 'F', 'F', ' '))
            && !__hidden_ui_text_font::HasTable(*m_hbFace, HB_TAG('s', 'b', 'i', 'x'))
            && !__hidden_ui_text_font::HasTable(*m_hbFace, HB_TAG('C', 'B', 'D', 'T'))
            && !__hidden_ui_text_font::HasTable(*m_hbFace, HB_TAG('C', 'O', 'L', 'R'));
        if(source.atlas){
            const FontAtlasPayload& payload = source.atlas->payload();
            if(
                source.atlas->validatePayload() && payload.font == m_identity
                && ValidateFontAtlasSourceMatch(payload, source.font)
                && payload.faceIndex == source.font.faceIndex() && payload.unitsPerEm == hb_face_get_upem(m_hbFace)
                && payload.sourceGlyphCount == hb_face_get_glyph_count(m_hbFace)
            ){
                m_bakedAtlas = CreateBakedFontAtlas(arena, *source.atlas, m_generation);
                if(!m_bakedAtlas)
                    NWB_LOGGER_WARNING(GLB_TEXT("UI baked font atlas could not be installed; using native coverage"));
            }
            else
                NWB_LOGGER_WARNING(GLB_TEXT("UI baked font atlas does not match the shaping font; using native coverage"));
        }
    }
    ~FontFaceState(){
        if(m_hbFace)
            hb_face_destroy(m_hbFace);
        if(m_blob)
            hb_blob_destroy(m_blob);
        if(m_face && FT_Done_Face(m_face) != 0)
            NWB_LOGGER_ERROR(GLB_TEXT("UI FreeType face destruction failed"));
        if(m_library && FT_Done_Library(m_library) != 0)
            NWB_LOGGER_ERROR(GLB_TEXT("UI FreeType library destruction failed"));
    }


private:
    [[nodiscard]] GlyphCoverageBounds coverageBounds(u32 glyphId, f32 fontSize)const{
        if(!m_allowOutlineBounds || m_coverageInkReliable)
            return {};
        if(FT_Load_Glyph(m_face, glyphId, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_NO_AUTOHINT) != 0){
            NWB_LOGGER_WARNING(GLB_TEXT("UI glyph outline bounds unavailable; retaining conservative coverage candidate"));
            return {};
        }
        const FT_GlyphSlot slot = m_face->glyph;
        if(slot->format != FT_GLYPH_FORMAT_OUTLINE)
            return {};
        if(slot->outline.n_points == 0)
            return { {}, true };
        FT_BBox box{};
        FT_Outline_Get_CBox(&slot->outline, &box);
        // Native outline bounds include CFF font/subfont transforms; leave HarfBuzz ink and placement unchanged.
        const f64 scale = static_cast<f64>(fontSize) / m_face->units_per_EM;
        const f64 left = (static_cast<f64>(box.xMin) - 1.0) * scale;
        const f64 top = (-static_cast<f64>(box.yMax) - 1.0) * scale;
        const f64 width = (static_cast<f64>(box.xMax) - box.xMin + 2.0) * scale;
        const f64 height = (static_cast<f64>(box.yMax) - box.yMin + 2.0) * scale;
        const f64 limit = Limit<f32>::s_Max;
        if(
            !IsFinite(left) || !IsFinite(top) || !IsFinite(width) || !IsFinite(height)
            || Abs(left) > limit || Abs(top) > limit || width < 0.0 || height < 0.0 || width > limit || height > limit
            || Abs(left + width) > limit || Abs(top + height) > limit
        )
            return {};
        const Rect ink{ static_cast<f32>(left), static_cast<f32>(top), static_cast<f32>(width), static_cast<f32>(height) };
        if(!IsFinite(ink.x + ink.width) || !IsFinite(ink.y + ink.height))
            return {};
        return { ink, true };
    }


private:
    Core::Assets::AssetRef<Font> m_identity;
    u64 m_generation;
    u32 m_unitsPerEm = 0u;
    PaintVector<u8> m_bytes;
    SharedBakedFontAtlas m_bakedAtlas;
    FT_MemoryRec_ m_memory{};
    FT_Library m_library = nullptr;
    FT_Face m_face = nullptr;
    hb_blob_t* m_blob = nullptr;
    hb_face_t* m_hbFace = nullptr;
    bool m_ready = false;
    bool m_allowOutlineBounds = false;
    bool m_coverageInkReliable = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FontFace::FontFace(Core::Alloc::GlobalArena& arena, const FontSource& source)
    : m_state(Core::MakeGlobalUnique<FontFaceState>(arena, arena, source))
{}

FontFace::~FontFace() = default;

bool FontFace::valid()const{ return m_state->m_ready; }

const Core::Assets::AssetRef<Font>& FontFace::identity()const{ return m_state->m_identity; }

u64 FontFace::generation()const{ return m_state->m_generation; }

u32 FontFace::unitsPerEm()const{ return m_state->m_unitsPerEm; }

bool FontFace::coverageInkReliable()const{ return m_state->m_coverageInkReliable; }

const SharedBakedFontAtlas& FontFace::bakedAtlas()const{ return m_state->m_bakedAtlas; }

bool FontFace::metrics(f32 fontSize, FontMetrics& output)const{
    if(!valid() || !IsFinite(fontSize) || fontSize < 1.0f / 64.0f || fontSize > 2048.0f)
        return false;
    __hidden_ui_text_font::ScaledFont scaled(*m_state->m_hbFace, fontSize);
    hb_font_extents_t extents{};
    if(!scaled.valid() || !hb_font_get_h_extents(&scaled.get(), &extents))
        return false;
    output.ascender = Max(0.0f, static_cast<f32>(extents.ascender) / 64.0f);
    output.descender = Max(0.0f, -static_cast<f32>(extents.descender) / 64.0f);
    output.lineGap = Max(0.0f, static_cast<f32>(extents.line_gap) / 64.0f);
    return output.ascender + output.descender > 0.0f;
}

bool FontFace::shape(
    const ShapeRequest& request,
    u32 byteBegin,
    u32 byteEnd,
    PaintVector<RawShapedGlyph>& output)const{
    if(
        !valid() || byteBegin > byteEnd || byteEnd > request.text.size() || request.text.size() > s_TextMaxBytes
        || !IsFinite(request.fontSize) || request.fontSize < 1.0f / 64.0f || request.fontSize > 2048.0f
    )
        return false;
    __hidden_ui_text_font::ScaledFont scaled(*m_state->m_hbFace, request.fontSize);
    const hb_script_t script = hb_script_from_iso15924_tag(request.scriptTag);
    if(!scaled.valid() || script == HB_SCRIPT_INVALID)
        return false;
    if(byteBegin == byteEnd){
        output.clear();
        return true;
    }
    __hidden_ui_text_font::ShapeBuffer buffer;
    hb_buffer_t& nativeBuffer = buffer.get();
    hb_buffer_set_direction(&nativeBuffer, request.direction == TextDirection::LeftToRight ? HB_DIRECTION_LTR : HB_DIRECTION_RTL);
    hb_buffer_set_script(&nativeBuffer, script);
    hb_buffer_set_language(&nativeBuffer, hb_language_from_string(request.language.data(), static_cast<i32>(request.language.size())));
    hb_buffer_set_cluster_level(&nativeBuffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    hb_buffer_add_utf8(
        &nativeBuffer,
        request.text.data(),
        static_cast<i32>(request.text.size()),
        byteBegin,
        static_cast<i32>(byteEnd - byteBegin)
    );
    hb_shape(&scaled.get(), &nativeBuffer, nullptr, 0u);
    if(!hb_buffer_allocation_successful(&nativeBuffer)){
        NWB_LOGGER_ERROR(GLB_TEXT("UI HarfBuzz run allocation failed"));
        return false;
    }
    u32 count = 0u;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(&nativeBuffer, &count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(&nativeBuffer, nullptr);
    for(u32 index = 0u; index < count; ++index){
        if(infos[index].cluster < byteBegin || infos[index].cluster >= byteEnd || positions[index].x_advance < 0)
            return false;
        if(index != 0u){
            const bool increasing = infos[index].cluster >= infos[index - 1u].cluster;
            const bool decreasing = infos[index].cluster <= infos[index - 1u].cluster;
            if(request.direction == TextDirection::LeftToRight ? !increasing : !decreasing)
                return false;
        }
    }
    output.clear();
    output.reserve(count);
    u32 first = 0u;
    u32 previousBegin = byteEnd;
    while(first < count){
        u32 end = first + 1u;
        while(end < count && infos[end].cluster == infos[first].cluster)
            ++end;
        const u32 clusterBegin = infos[first].cluster;
        const u32 clusterEnd = request.direction == TextDirection::LeftToRight
            ? (end < count ? infos[end].cluster : byteEnd)
            : previousBegin;
        for(u32 index = first; index < end; ++index){
            hb_glyph_extents_t extents{};
            Rect ink;
            if(hb_font_get_glyph_extents(&scaled.get(), infos[index].codepoint, &extents)){
                const f32 x0 = static_cast<f32>(extents.x_bearing) / 64.0f;
                const f32 y0 = -static_cast<f32>(extents.y_bearing) / 64.0f;
                const f32 x1 = x0 + static_cast<f32>(extents.width) / 64.0f;
                const f32 y1 = y0 - static_cast<f32>(extents.height) / 64.0f;
                ink = { Min(x0, x1), Min(y0, y1), Abs(x1 - x0), Abs(y1 - y0) };
            }
            output.push_back({ infos[index].codepoint, clusterBegin, clusterEnd,
                { static_cast<f32>(positions[index].x_offset) / 64.0f, -static_cast<f32>(positions[index].y_offset) / 64.0f },
                { static_cast<f32>(positions[index].x_advance) / 64.0f, -static_cast<f32>(positions[index].y_advance) / 64.0f },
                ink, m_state->coverageBounds(infos[index].codepoint, request.fontSize) });
        }
        previousBegin = clusterBegin;
        first = end;
    }
    return true;
}

bool FontFace::rasterize(u32 glyphId, u32 pixelSize, GlyphBitmap& output){
    if(!valid() || pixelSize == 0u || pixelSize > 4096u)
        return false;
    if(
        FT_Set_Pixel_Sizes(m_state->m_face, 0u, pixelSize) != 0
        || FT_Load_Glyph(m_state->m_face, glyphId, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0
        || FT_Render_Glyph(m_state->m_face->glyph, FT_RENDER_MODE_NORMAL) != 0
    ){
        NWB_LOGGER_ERROR(GLB_TEXT("UI glyph rasterization failed"));
        return false;
    }
    const FT_GlyphSlot slot = m_state->m_face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    if(bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.width != 0u && bitmap.rows != 0u){
        NWB_LOGGER_ERROR(GLB_TEXT("UI glyph rasterizer requires grayscale coverage"));
        return false;
    }
    output.width = bitmap.width;
    output.height = bitmap.rows;
    output.bearingX = slot->bitmap_left;
    output.bearingY = slot->bitmap_top;
    if(bitmap.width > s_GlyphAtlasPageExtent - 2u || bitmap.rows > s_GlyphAtlasPageExtent - 2u)
        return false;
    output.pixels.resize(static_cast<usize>(bitmap.width) * bitmap.rows);
    const usize pitch = static_cast<usize>(Abs(bitmap.pitch));
    for(u32 row = 0u; row < bitmap.rows; ++row){
        const u32 sourceRow = bitmap.pitch >= 0 ? row : bitmap.rows - row - 1u;
        if(bitmap.width != 0u)
            GLB_MEMCPY(
                output.pixels.data() + static_cast<usize>(row) * bitmap.width,
                bitmap.width,
                bitmap.buffer + static_cast<usize>(sourceRow) * pitch,
                bitmap.width
            );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SharedFontFace MakeFontFace(Core::Alloc::GlobalArena& arena, const FontSource& source){
    return SharedFontFace(
        NewArenaObject<RefCounter<FontFace>>(arena, arena, source),
        ArenaRefDeleter<RefCounter<FontFace>, Core::Alloc::GlobalArena>(&arena),
        s_AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

