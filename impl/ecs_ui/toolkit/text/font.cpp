// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "font.h"

#include <core/common/log.h>

#include <global/simd/vector_arithmetic.h>
#include <global/scope_exit.h>

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


inline constexpr StringView s_LifecycleCategory = "ui_font_lifecycle";
inline constexpr f32 s_FixedPointRoundBias = 0.5f;
// Native outline bounds grow by one font unit on each side.
inline constexpr f64 s_OutlineBoundsMargin = 1.0;
inline constexpr f64 s_OutlineBoundsMarginBothSides = 2.0;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool HasTable(hb_face_t& face, hb_tag_t tag){
    hb_blob_t* blob = hb_face_reference_table(&face, tag);
    const bool present = hb_blob_get_length(blob) != 0u;
    hb_blob_destroy(blob);
    return present;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FontFaceState final : NoCopy{
    friend class FontFace;


private:
    [[nodiscard]] static void* Allocate(FT_Memory memory, FT_Long size)noexcept{
        if(size <= 0)
            return nullptr;
        auto& arena = *static_cast<Core::Alloc::GlobalArena*>(memory->user);
        return arena.allocate(alignof(MaxAlign), static_cast<usize>(size));
    }

    static void Release(FT_Memory memory, void* block)noexcept{
        if(block){
            auto& arena = *static_cast<Core::Alloc::GlobalArena*>(memory->user);
            arena.deallocate(block, alignof(MaxAlign), 0u);
        }
    }

    [[nodiscard]] static void* Reallocate(FT_Memory memory, FT_Long oldSize, FT_Long newSize, void* block)noexcept{
        static_cast<void>(oldSize);
        if(newSize <= 0){
            Release(memory, block);
            return nullptr;
        }
        auto& arena = *static_cast<Core::Alloc::GlobalArena*>(memory->user);
        return arena.reallocate(block, alignof(MaxAlign), static_cast<usize>(newSize));
    }


public:
    FontFaceState(Core::Alloc::ScratchArena& scratchArena, Core::Alloc::GlobalArena& arena, const FontSource& source)
        : m_identity(source.identity)
        , m_generation(source.generation)
        , m_bytes(arena)
    {
        ScopeExit cleanup([this]()noexcept{ releaseNative(); });
        initialize(scratchArena, arena, source);
        cleanup.release();
    }
    ~FontFaceState()noexcept{ releaseNative(); }


private:
    void initialize(Core::Alloc::ScratchArena& scratchArena, Core::Alloc::GlobalArena& arena, const FontSource& source){
        const Core::Assets::AssetBytes& sourceBytes = source.font.fontBytes();
        m_bytes.assign(sourceBytes.begin(), sourceBytes.end());
        m_memory.user = &arena;
        m_memory.alloc = &Allocate;
        m_memory.free = &Release;
        m_memory.realloc = &Reallocate;
        if(FT_New_Library(&m_memory, &m_library) != 0){
            NWB_LOGGER_ERROR(NWB_TEXT("UI FreeType initialization failed"));
            return;
        }
        FT_Add_Default_Modules(m_library);
        if(FT_New_Memory_Face(m_library, m_bytes.data(), static_cast<FT_Long>(m_bytes.size()), source.font.faceIndex(), &m_face) != 0){
            NWB_LOGGER_ERROR(NWB_TEXT("UI font face initialization failed"));
            return;
        }
        if(!FT_IS_SCALABLE(m_face) || m_face->units_per_EM == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("UI font must have scalable horizontal metrics"));
            return;
        }
        m_blob = hb_blob_create(reinterpret_cast<const char*>(m_bytes.data()), static_cast<u32>(m_bytes.size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        m_hbFace = hb_face_create(m_blob, source.font.faceIndex());
        m_unitsPerEm = hb_face_get_upem(m_hbFace);
        m_scaledFont = hb_font_create(m_hbFace);
        m_shapeBuffer = hb_buffer_create();
        m_ready = hb_face_get_glyph_count(m_hbFace) != 0u && m_unitsPerEm != 0u && m_scaledFont != hb_font_get_empty() && hb_buffer_allocation_successful(m_shapeBuffer);
        if(!m_ready){
            NWB_LOGGER_ERROR(NWB_TEXT("UI HarfBuzz font face initialization failed"));
            return;
        }
        hb_ot_font_set_funcs(m_scaledFont);
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
                source.atlas->validatePayload(scratchArena) && payload.font == m_identity
                && ValidateFontAtlasSourceMatch(payload, source.font)
                && payload.faceIndex == source.font.faceIndex() && payload.unitsPerEm == hb_face_get_upem(m_hbFace)
                && payload.sourceGlyphCount == hb_face_get_glyph_count(m_hbFace)
            ){
                auto atlasResult = CreateBakedFontAtlas(arena, *source.atlas, m_generation);
                if(atlasResult)
                    m_bakedAtlas = Move(*atlasResult);
                else
                    NWB_LOGGER_WARNING(NWB_TEXT("UI baked font atlas could not be installed; using native coverage"));
            }
            else
                NWB_LOGGER_WARNING(NWB_TEXT("UI baked font atlas does not match the shaping font; using native coverage"));
        }
    }

    void releaseNative()noexcept{
        if(m_shapeBuffer){
            hb_buffer_destroy(m_shapeBuffer);
            m_shapeBuffer = nullptr;
        }
        if(m_scaledFont){
            hb_font_destroy(m_scaledFont);
            m_scaledFont = nullptr;
        }
        if(m_hbFace){
            hb_face_destroy(m_hbFace);
            m_hbFace = nullptr;
        }
        if(m_blob){
            hb_blob_destroy(m_blob);
            m_blob = nullptr;
        }
        if(m_face){
            const FT_Error error = FT_Done_Face(m_face);
            m_face = nullptr;
            if(error != 0)
                CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = __hidden_ui_text_font::s_LifecycleCategory, .message = "UI FreeType face destruction failed", .file = __FILE__, .line = __LINE__, });
        }
        if(m_library){
            const FT_Error error = FT_Done_Library(m_library);
            m_library = nullptr;
            if(error != 0)
                CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = __hidden_ui_text_font::s_LifecycleCategory, .message = "UI FreeType library destruction failed", .file = __FILE__, .line = __LINE__, });
        }
    }


private:
    [[nodiscard]] GlyphCoverageBounds coverageBounds(u32 glyphId, f32 fontSize)const{
        if(!m_allowOutlineBounds || m_coverageInkReliable)
            return {};
        if(FT_Load_Glyph(m_face, glyphId, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_NO_AUTOHINT) != 0){
            NWB_LOGGER_WARNING(NWB_TEXT("UI glyph outline bounds unavailable; retaining conservative coverage candidate"));
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
        const SIMDVectorDouble scale = VectorDoubleDivide(VectorDoubleSet(static_cast<f64>(fontSize), static_cast<f64>(fontSize), 0.0, 0.0), VectorDoubleSet(static_cast<f64>(m_face->units_per_EM), static_cast<f64>(m_face->units_per_EM), 1.0, 1.0));
        const SIMDVectorDouble leftTopUnits = VectorDoubleSet(static_cast<f64>(box.xMin), static_cast<f64>(box.yMax), 0.0, 0.0);
        const SIMDVectorDouble leftTopBounds = VectorDoubleSubtract(VectorDoubleSelect(leftTopUnits, VectorDoubleNegate(leftTopUnits), s_DoubleSIMDMaskY), VectorDoubleSet(__hidden_ui_text_font::s_OutlineBoundsMargin, __hidden_ui_text_font::s_OutlineBoundsMargin, 0.0, 0.0));
        const SIMDVectorDouble leftTopValue = VectorDoubleMultiply(leftTopBounds, scale);
        const f64 left = VectorDoubleGetX(leftTopValue);
        const f64 top = VectorDoubleGetY(leftTopValue);
        const SIMDVectorDouble widthHeightValue = VectorDoubleMultiply(
            VectorDoubleAdd(VectorDoubleSubtract(VectorDoubleSet(static_cast<f64>(box.xMax), static_cast<f64>(box.yMax), 0.0, 0.0), VectorDoubleSet(static_cast<f64>(box.xMin), static_cast<f64>(box.yMin), 0.0, 0.0)), VectorDoubleSet(__hidden_ui_text_font::s_OutlineBoundsMarginBothSides, __hidden_ui_text_font::s_OutlineBoundsMarginBothSides, 0.0, 0.0)),
            scale
        );
        const f64 width = VectorDoubleGetX(widthHeightValue);
        const f64 height = VectorDoubleGetY(widthHeightValue);
        const f64 limit = Limit<f32>::s_Max;
        const SIMDVectorDouble limitValue = VectorDoubleReplicate(limit);
        // The end sums run last, on finite and bounded inputs, like the old chain.
        if(
            !IsFinite(left) || !IsFinite(top) || !IsFinite(width) || !IsFinite(height)
            || VectorDoubleMoveMask(VectorDoubleGreater(VectorDoubleAbs(leftTopValue), limitValue)) != 0u
            || width < 0.0 || height < 0.0 || width > limit || height > limit
            || VectorDoubleMoveMask(VectorDoubleGreater(VectorDoubleAbs(VectorDoubleAdd(leftTopValue, widthHeightValue)), limitValue)) != 0u
        )
            return {};
        const Rect ink{ static_cast<f32>(left), static_cast<f32>(top), static_cast<f32>(width), static_cast<f32>(height) };
        if(!VectorIsFinite(VectorAdd(VectorSet(ink.x, ink.y, 0.0f, 0.0f), VectorSet(ink.width, ink.height, 0.0f, 0.0f)), VectorComponentMask::s_XY))
            return {};
        return { ink, true };
    }


private:
    Core::Assets::AssetRef<Font> m_identity;
    u64 m_generation;
    PaintVector<u8> m_bytes;
    SharedBakedFontAtlas m_bakedAtlas;
    FT_MemoryRec_ m_memory{};
    FT_Library m_library = nullptr;
    FT_Face m_face = nullptr;
    hb_blob_t* m_blob = nullptr;
    hb_face_t* m_hbFace = nullptr;
    hb_font_t* m_scaledFont = nullptr;
    hb_buffer_t* m_shapeBuffer = nullptr;
    u32 m_unitsPerEm = 0u;
    bool m_ready = false;
    bool m_allowOutlineBounds = false;
    bool m_coverageInkReliable = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FontFace::FontFace(Core::Alloc::ScratchArena& scratchArena, Core::Alloc::GlobalArena& arena, const FontSource& source)
    : m_state(Core::MakeGlobalUnique<FontFaceState>(arena, scratchArena, arena, source))
{}

FontFace::~FontFace()noexcept = default;

bool FontFace::valid()const noexcept{ return m_state->m_ready; }

const Core::Assets::AssetRef<Font>& FontFace::identity()const noexcept{ return m_state->m_identity; }

u64 FontFace::generation()const noexcept{ return m_state->m_generation; }

u32 FontFace::unitsPerEm()const noexcept{ return m_state->m_unitsPerEm; }

bool FontFace::coverageInkReliable()const noexcept{ return m_state->m_coverageInkReliable; }

const SharedBakedFontAtlas& FontFace::bakedAtlas()const noexcept{ return m_state->m_bakedAtlas; }

Expected<FontMetrics> FontFace::metrics(f32 fontSize)const{
    FontMetrics metrics{};
    if(!valid() || !IsFinite(fontSize) || fontSize < 1.0f / s_TextFixedPointScale || fontSize > s_TextMaxFontSize)
        return MakeUnexpected(Failure{});
    const i32 scale = static_cast<i32>(VectorGetX(VectorMultiplyAddExpression(VectorReplicate(fontSize), VectorReplicate(s_TextFixedPointScale), VectorReplicate(__hidden_ui_text_font::s_FixedPointRoundBias))));
    hb_font_set_scale(m_state->m_scaledFont, scale, scale);
    hb_font_extents_t extents{};
    if(!hb_font_get_h_extents(m_state->m_scaledFont, &extents))
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle scaledExtents = VectorScale(VectorXorInt(VectorSet(static_cast<f32>(extents.ascender), static_cast<f32>(extents.descender), static_cast<f32>(extents.line_gap), 0.0f), s_SIMDFlipY), 1.0f / s_TextFixedPointScale);
    const SIMDVectorSingle positiveExtents = VectorSelect(scaledExtents, VectorZero(), VectorGreater(VectorZero(), scaledExtents));
    metrics.ascender = VectorGetX(positiveExtents);
    metrics.descender = VectorGetY(positiveExtents);
    metrics.lineGap = VectorGetZ(positiveExtents);
    if(!(VectorGetX(VectorAdd(VectorSplatX(positiveExtents), VectorSplatY(positiveExtents))) > 0.0f))
        return MakeUnexpected(Failure{});
    return metrics;
}

bool FontFace::shape(const ShapeRequest& request, u32 byteBegin, u32 byteEnd, PaintVector<RawShapedGlyph>& output)const{
    if(!valid() || byteBegin > byteEnd || byteEnd > request.text.size() || request.text.size() > s_TextMaxBytes || !IsFinite(request.fontSize) || request.fontSize < 1.0f / s_TextFixedPointScale || request.fontSize > s_TextMaxFontSize)
        return false;
    const i32 scale = static_cast<i32>(VectorGetX(VectorMultiplyAddExpression(VectorReplicate(request.fontSize), VectorReplicate(s_TextFixedPointScale), VectorReplicate(__hidden_ui_text_font::s_FixedPointRoundBias))));
    hb_font_set_scale(m_state->m_scaledFont, scale, scale);
    const hb_script_t script = hb_script_from_iso15924_tag(request.scriptTag);
    if(script == HB_SCRIPT_INVALID)
        return false;
    if(byteBegin == byteEnd){
        output.clear();
        return true;
    }
    hb_buffer_t& nativeBuffer = *m_state->m_shapeBuffer;
    hb_buffer_clear_contents(&nativeBuffer);
    hb_buffer_set_direction(&nativeBuffer, request.direction == TextDirection::LeftToRight ? HB_DIRECTION_LTR : HB_DIRECTION_RTL);
    hb_buffer_set_script(&nativeBuffer, script);
    hb_buffer_set_language(&nativeBuffer, hb_language_from_string(request.language.data(), static_cast<i32>(request.language.size())));
    hb_buffer_set_cluster_level(&nativeBuffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    hb_buffer_add_utf8(&nativeBuffer, request.text.data(), static_cast<i32>(request.text.size()), byteBegin, static_cast<i32>(byteEnd - byteBegin));
    hb_shape(m_state->m_scaledFont, &nativeBuffer, nullptr, 0u);
    if(!hb_buffer_allocation_successful(&nativeBuffer)){
        NWB_LOGGER_ERROR(NWB_TEXT("UI HarfBuzz run allocation failed"));
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
        const u32 clusterEnd = request.direction == TextDirection::LeftToRight ? (end < count ? infos[end].cluster : byteEnd) : previousBegin;
        for(u32 index = first; index < end; ++index){
            hb_glyph_extents_t extents{};
            Rect ink;
            if(hb_font_get_glyph_extents(m_state->m_scaledFont, infos[index].codepoint, &extents)){
                const SIMDVectorSingle bearing = VectorSet(static_cast<f32>(extents.x_bearing), static_cast<f32>(extents.y_bearing), static_cast<f32>(extents.x_bearing), static_cast<f32>(extents.y_bearing));
                const SIMDVectorSingle origin = VectorScale(VectorXorInt(bearing, s_SIMDFlipYW), 1.0f / s_TextFixedPointScale);
                const SIMDVectorSingle width = VectorReplicate(static_cast<f32>(extents.width));
                const SIMDVectorSingle height = VectorReplicate(static_cast<f32>(extents.height));
                const SIMDVectorSingle scale = VectorReplicate(1.0f / s_TextFixedPointScale);
                const SIMDVectorSingle end = VectorPermute<0, 5, 2, 7>(VectorMultiplyAddExpression(width, scale, origin), VectorNegativeMultiplySubtractExpression(height, scale, origin));
                const SIMDVectorSingle minimum = VectorSelect(end, origin, VectorLess(origin, end));
                const SIMDVectorSingle delta = VectorSubtract(end, origin);
                const SIMDVectorSingle size = VectorSelect(delta, VectorNegate(delta), VectorLess(delta, VectorZero()));
                ink = { VectorGetX(minimum), VectorGetY(minimum), VectorGetX(size), VectorGetY(size) };
            }
            const SIMDVectorSingle placement = VectorSet(static_cast<f32>(positions[index].x_offset), static_cast<f32>(positions[index].y_offset), static_cast<f32>(positions[index].x_advance), static_cast<f32>(positions[index].y_advance));
            const SIMDVectorSingle position = VectorScale(VectorXorInt(placement, s_SIMDFlipYW), 1.0f / s_TextFixedPointScale);
            output.push_back({ infos[index].codepoint, clusterBegin, clusterEnd, { VectorGetX(position), VectorGetY(position) }, { VectorGetZ(position), VectorGetW(position) }, ink, m_state->coverageBounds(infos[index].codepoint, request.fontSize) });
        }
        previousBegin = clusterBegin;
        first = end;
    }
    return true;
}

Expected<Rect> FontFace::rasterBounds(const u32 glyphId, const u32 pixelSize){
    if(!valid() || pixelSize == 0u || pixelSize > s_TextMaxRasterPpem)
        return MakeUnexpected(Failure{});
    if(FT_Set_Pixel_Sizes(m_state->m_face, 0u, pixelSize) != 0 || FT_Load_Glyph(m_state->m_face, glyphId, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("UI glyph raster measurement failed"));
        return MakeUnexpected(Failure{});
    }
    // FT_Load_Glyph presets this box through the same ft_glyphslot_preset_bitmap path as the normal renderer, without allocating coverage pixels.
    const FT_GlyphSlot slot = m_state->m_face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    if(slot->format != FT_GLYPH_FORMAT_OUTLINE || (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.width != 0u && bitmap.rows != 0u)){
        NWB_LOGGER_ERROR(NWB_TEXT("UI glyph measurement requires a grayscale outline"));
        return MakeUnexpected(Failure{});
    }
    const SIMDVectorSingle bounds = VectorXorInt(VectorSet(static_cast<f32>(slot->bitmap_left), static_cast<f32>(slot->bitmap_top), static_cast<f32>(bitmap.width), static_cast<f32>(bitmap.rows)), s_SIMDFlipY);
    return Rect{ VectorGetX(bounds), VectorGetY(bounds), VectorGetZ(bounds), VectorGetW(bounds) };
}

bool FontFace::rasterize(u32 glyphId, u32 pixelSize, GlyphBitmap& output){
    if(!valid() || pixelSize == 0u || pixelSize > s_TextMaxRasterPpem)
        return false;
    if(FT_Set_Pixel_Sizes(m_state->m_face, 0u, pixelSize) != 0 || FT_Load_Glyph(m_state->m_face, glyphId, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0 || FT_Render_Glyph(m_state->m_face->glyph, FT_RENDER_MODE_NORMAL) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("UI glyph rasterization failed"));
        return false;
    }
    const FT_GlyphSlot slot = m_state->m_face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    if(bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.width != 0u && bitmap.rows != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("UI glyph rasterizer requires grayscale coverage"));
        return false;
    }
    output.width = bitmap.width;
    output.height = bitmap.rows;
    output.bearingX = slot->bitmap_left;
    output.bearingY = slot->bitmap_top;
    if(bitmap.width > s_GlyphAtlasPageExtent - s_GlyphAtlasGlyphGutter || bitmap.rows > s_GlyphAtlasPageExtent - s_GlyphAtlasGlyphGutter)
        return false;
    output.pixels.resize(static_cast<usize>(bitmap.width) * bitmap.rows);
    const usize pitch = static_cast<usize>(Abs(bitmap.pitch));
    for(u32 row = 0u; row < bitmap.rows; ++row){
        const u32 sourceRow = bitmap.pitch >= 0 ? row : bitmap.rows - row - 1u;
        if(bitmap.width != 0u)
            NWB_MEMCPY(output.pixels.data() + static_cast<usize>(row) * bitmap.width, bitmap.width, bitmap.buffer + static_cast<usize>(sourceRow) * pitch, bitmap.width);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SharedFontFace MakeFontFace(Core::Alloc::ScratchArena& scratchArena, Core::Alloc::GlobalArena& arena, const FontSource& source){
    return SharedFontFace(NewArenaObject<RefCounter<FontFace>>(arena, scratchArena, arena, source), ArenaRefDeleter<RefCounter<FontFace>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

