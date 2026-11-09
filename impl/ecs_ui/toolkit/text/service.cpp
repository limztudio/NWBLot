// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "service.h"
#include "font.h"
#include <impl/ecs_ui/toolkit/text/shaper_types.adhd.h>
#include <core/common/log.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <global/scope_exit.h>
#include FT_MODULE_H
#include <hb.h>
#include <hb-ot.h>
#include "atlas.h"
#include "glyph_page.h"
#include "glyph_visibility.h"
#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>
#include <core/alloc/scratch.h>
#include <global/atomic_identity.h>
#include <global/simd/result.h>
#include <global/simd/vector_arithmetic.h>
#include <global/simd/vector_rounding.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FontGlyphRasterizer final{
public:
    FontGlyphRasterizer(FT_Face face, b8 allowOutlineBounds, b8 coverageInkReliable)noexcept : m_face(face), m_allowOutlineBounds(allowOutlineBounds), m_coverageInkReliable(coverageInkReliable){}

    [[nodiscard]] GlyphCoverageBounds coverageBounds(const u32 glyphId, const f32 fontSize)const{
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
        return ComputeOutlineCoverageBounds(box.xMin, box.yMin, box.xMax, box.yMax, fontSize, m_face->units_per_EM);
    }

    [[nodiscard]] Expected<Rect> rasterBounds(const u32 glyphId, const u32 pixelSize){
        if(pixelSize == 0u || pixelSize > s_TextMaxRasterPpem)
            return MakeUnexpected(Failure{});
        if(FT_Set_Pixel_Sizes(m_face, 0u, pixelSize) != 0 || FT_Load_Glyph(m_face, glyphId, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0){
            NWB_LOGGER_ERROR(NWB_TEXT("UI glyph raster measurement failed"));
            return MakeUnexpected(Failure{});
        }
        // FT_Load_Glyph presets this box through the same ft_glyphslot_preset_bitmap path as the normal renderer, without allocating coverage pixels.
        const FT_GlyphSlot slot = m_face->glyph;
        const FT_Bitmap& bitmap = slot->bitmap;
        if(slot->format != FT_GLYPH_FORMAT_OUTLINE || (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.width != 0u && bitmap.rows != 0u)){
            NWB_LOGGER_ERROR(NWB_TEXT("UI glyph measurement requires a grayscale outline"));
            return MakeUnexpected(Failure{});
        }
        return MakeRasterGlyphBoundsRect(slot->bitmap_left, slot->bitmap_top, bitmap.width, bitmap.rows);
    }

    [[nodiscard]] b8 rasterize(const u32 glyphId, const u32 pixelSize, GlyphBitmap& output){
        if(pixelSize == 0u || pixelSize > s_TextMaxRasterPpem)
            return false;
        if(FT_Set_Pixel_Sizes(m_face, 0u, pixelSize) != 0 || FT_Load_Glyph(m_face, glyphId, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0 || FT_Render_Glyph(m_face->glyph, FT_RENDER_MODE_NORMAL) != 0){
            NWB_LOGGER_ERROR(NWB_TEXT("UI glyph rasterization failed"));
            return false;
        }
        const FT_GlyphSlot slot = m_face->glyph;
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

private:
    FT_Face m_face;
    b8 m_allowOutlineBounds;
    b8 m_coverageInkReliable;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_font{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 HasTable(hb_face_t& face, hb_tag_t tag){
    hb_blob_t* blob = hb_face_reference_table(&face, tag);
    const b8 present = hb_blob_get_length(blob) != 0u;
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
        auto& arena = *MakeNotNull(static_cast<Core::Alloc::GlobalArena*>(memory->user));
        return arena.allocate(alignof(MaxAlign), static_cast<usize>(size));
    }

    static void Release(FT_Memory memory, void* block)noexcept{
        if(block){
            auto& arena = *MakeNotNull(static_cast<Core::Alloc::GlobalArena*>(memory->user));
            arena.deallocate(block, alignof(MaxAlign), 0u);
        }
    }

    [[nodiscard]] static void* Reallocate(FT_Memory memory, [[maybe_unused]] FT_Long oldSize, FT_Long newSize, void* block)noexcept{
        if(newSize <= 0){
            Release(memory, block);
            return nullptr;
        }
        auto& arena = *MakeNotNull(static_cast<Core::Alloc::GlobalArena*>(memory->user));
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
                CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = s_TextFontLifecycleCategory, .message = "UI FreeType face destruction failed", .file = __FILE__, .line = __LINE__, });
        }
        if(m_library){
            const FT_Error error = FT_Done_Library(m_library);
            m_library = nullptr;
            if(error != 0)
                CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = s_TextFontLifecycleCategory, .message = "UI FreeType library destruction failed", .file = __FILE__, .line = __LINE__, });
        }
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
    b8 m_ready = false;
    b8 m_allowOutlineBounds = false;
    b8 m_coverageInkReliable = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FontFace::FontFace(Core::Alloc::ScratchArena& scratchArena, Core::Alloc::GlobalArena& arena, const FontSource& source)
    : m_state(Core::MakeGlobalUnique<FontFaceState>(arena, scratchArena, arena, source))
{}

FontFace::~FontFace()noexcept = default;

b8 FontFace::valid()const noexcept{ return m_state->m_ready; }

const Core::Assets::AssetRef<Font>& FontFace::identity()const noexcept{ return m_state->m_identity; }

u64 FontFace::generation()const noexcept{ return m_state->m_generation; }

u32 FontFace::unitsPerEm()const noexcept{ return m_state->m_unitsPerEm; }

b8 FontFace::coverageInkReliable()const noexcept{ return m_state->m_coverageInkReliable; }

const SharedBakedFontAtlas& FontFace::bakedAtlas()const noexcept{ return m_state->m_bakedAtlas; }

Expected<FontMetrics> FontFace::metrics(f32 fontSize)const{
    if(!valid() || !IsValidTextFontSize(fontSize))
        return MakeUnexpected(Failure{});
    const i32 scale = ComputeTextFixedPointScale(fontSize);
    hb_font_set_scale(m_state->m_scaledFont, scale, scale);
    hb_font_extents_t extents{};
    if(!hb_font_get_h_extents(m_state->m_scaledFont, &extents))
        return MakeUnexpected(Failure{});
    return TryResolveFontExtentsMetrics(extents.ascender, extents.descender, extents.line_gap);
}

b8 FontFace::shape(const ShapeRequest& request, u32 byteBegin, u32 byteEnd, PaintVector<RawShapedGlyph>& output)const{
    if(!valid() || byteBegin > byteEnd || byteEnd > request.text.size() || request.text.size() > s_TextMaxBytes || !IsValidTextFontSize(request.fontSize))
        return false;
    const i32 scale = ComputeTextFixedPointScale(request.fontSize);
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
        const u32 previousCluster = index != 0u ? infos[index - 1u].cluster : 0u;
        if(!IsValidHbShapedGlyphClusterStep(infos[index].cluster, previousCluster, byteBegin, byteEnd, positions[index].x_advance, index, request.direction))
            return false;
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
            if(hb_font_get_glyph_extents(m_state->m_scaledFont, infos[index].codepoint, &extents))
                ink = ResolveHbGlyphExtentsInk(extents.x_bearing, extents.y_bearing, extents.width, extents.height);
            Point offset;
            Point advance;
            ResolveHbGlyphPlacement(positions[index].x_offset, positions[index].y_offset, positions[index].x_advance, positions[index].y_advance, offset, advance);
            output.push_back({ infos[index].codepoint, clusterBegin, clusterEnd, offset, advance, ink, FontGlyphRasterizer(m_state->m_face, m_state->m_allowOutlineBounds, m_state->m_coverageInkReliable).coverageBounds(infos[index].codepoint, request.fontSize) });
        }
        previousBegin = clusterBegin;
        first = end;
    }
    return true;
}

Expected<Rect> FontFace::rasterBounds(const u32 glyphId, const u32 pixelSize){
    if(!valid())
        return MakeUnexpected(Failure{});
    return FontGlyphRasterizer(m_state->m_face, m_state->m_allowOutlineBounds, m_state->m_coverageInkReliable).rasterBounds(glyphId, pixelSize);
}

b8 FontFace::rasterize(const u32 glyphId, const u32 pixelSize, GlyphBitmap& output){
    if(!valid())
        return false;
    return FontGlyphRasterizer(m_state->m_face, m_state->m_allowOutlineBounds, m_state->m_coverageInkReliable).rasterize(glyphId, pixelSize, output);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SharedFontFace> MakeFontFace(Core::Alloc::ScratchArena& scratchArena, Core::Alloc::GlobalArena& arena, const FontSource& source){
    auto face = SharedFontFace(NewArenaObject<RefCounter<FontFace>>(arena, scratchArena, arena, source), ArenaRefDeleter<RefCounter<FontFace>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
    if(!face)
        return MakeUnexpected(Failure{});
    return face;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_service{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextServiceIdentity{ 1u };

[[nodiscard]] static Expected<SIMDVectorDoubleResult> LoadSdfSupport(const PlacedGlyph& placed, const BakedFontAtlas& atlas, const f32 fontSize, const DisplayMetrics& display)noexcept{
    const auto plane = TextGlyphVisibility::LoadAtlasRectangle(placed, atlas, fontSize, {});
    if(!plane)
        return MakeUnexpected(Failure{});
    if(VectorDoubleGetZ(plane->value) <= 0.0 || VectorDoubleGetW(plane->value) <= 0.0)
        return plane;
    const FontAtlasGlyph& glyph = *atlas.glyph(placed.glyphId);
    const SdfAtlasPage& page = *atlas.page(glyph.group);
    const SdfAtlasPageBinding& binding = page.binding();
    const auto& pixels = page.pixels();
    return TryLoadSdfGlyphSupport(plane->value, glyph, binding.width, binding.height, binding.channelCount, binding.spreadPixels, Span<const u8>(pixels.data(), pixels.size()), display);
}

template<typename TPage, typename TValue>
[[nodiscard]] static u32 InternPreparedPage(PaintVector<TPage>& pages, TValue&& page){
    const auto existing = FindIf(pages.begin(), pages.end(), [&page](const TPage& item)noexcept{
        return item.get() == page.get();
    });
    const u32 pageIndex = static_cast<u32>(existing - pages.begin());
    if(existing == pages.end())
        pages.push_back(Forward<TValue>(page));
    return pageIndex;
}

struct PreparedGlyph{
    Rect rectangle;
    Rect uv;
    const SharedSdfAtlasPage* sdfPage = nullptr;
    u32 pageIndex = 0u;
    u32 channel = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextService::TextService(Core::Alloc::GlobalArena& arena)
    : m_shaper(arena)
    , m_layoutBuilder(arena, m_shaper)
    , m_atlas(Core::MakeGlobalUnique<GlyphAtlas>(arena, arena))
    , m_pages(arena)
    , m_sdfPages(arena)
    , m_identity(NextNonWrappingIdentity(__hidden_ui_text_service::s_NextServiceIdentity))
{
    m_pages.reserve(s_GlyphAtlasMaxPages);
    m_sdfPages.reserve(s_PaintMaxImages);
}

TextService::~TextService()noexcept = default;

b8 TextService::setFonts(Core::Alloc::ScratchArena& scratchArena, const Span<const FontSource> sources){
    if(m_generation == Limit<u64>::s_Max || !m_shaper.setFonts(scratchArena, sources))
        return false;
    m_pages.clear();
    m_sdfPages.clear();
    m_atlas->reset();
    ++m_generation;
    return true;
}

Expected<Rect> TextService::measure(const TextLayout& layout, const DisplayMetrics& display){
    using namespace __hidden_ui_text_service;
    if(layout.lines().empty())
        return MakeUnexpected(Failure{});
    const auto rasterMetrics = TryResolveTextRasterMetrics(layout.fontSize(), display);
    if(!rasterMetrics)
        return MakeUnexpected(Failure{});
    const f32 physicalSize = rasterMetrics->physicalSize;
    const f32 rasterScale = rasterMetrics->rasterScale;
    const u32 pixelSize = rasterMetrics->pixelSize;
    SIMDVectorDouble bounds = ComputeInitialMeasuredTextBounds(layout.measure(), layout.inkBounds());
    for(const PlacedGlyph& glyph : layout.glyphs()){
        if(const BakedFontAtlas* atlas = TextGlyphVisibility::SelectAtlas(glyph, physicalSize)){
            const auto support = LoadSdfSupport(glyph, *atlas, layout.fontSize(), display);
            if(!support)
                return MakeUnexpected(Failure{});
            ExpandMeasuredTextBoundsWithSdfSupport(support->value, bounds);
        }
        else{
            if(!glyph.face || !glyph.face->valid())
                return MakeUnexpected(Failure{});
            const AtlasGlyph* cached = m_atlas->find(glyph.face, glyph.glyphId, pixelSize);
            const Expected<Rect> raster = cached
                ? Expected<Rect>(Rect{ static_cast<f32>(cached->bearingX), -static_cast<f32>(cached->bearingY), cached->pixels.width, cached->pixels.height })
                : glyph.face->rasterBounds(glyph.glyphId, pixelSize);
            if(!raster)
                return MakeUnexpected(Failure{});
            ExpandMeasuredTextBoundsWithRaster(glyph.position, *raster, rasterScale, display, bounds);
        }
    }
    return TryMakeMeasuredTextRect(VectorDoubleGetX(bounds), VectorDoubleGetY(bounds), VectorDoubleGetZ(bounds), VectorDoubleGetW(bounds));
}

b8 TextService::paint(PaintBuilder& paint, const TextLayout& layout, Point topLeft, const Color& color){
    using namespace __hidden_ui_text_service;
    const DisplayMetrics& metrics = paint.displayMetrics();
    const auto rasterMetrics = TryResolveTextRasterMetrics(layout.fontSize(), metrics);
    if(layout.lines().empty() || !rasterMetrics || !IsFinite(topLeft.x) || !IsFinite(topLeft.y) || !IsValidPaintColor(color))
        return false;
    const Rect clip = paint.currentClip();
    const TextGlyphIntersection::Enum clipStatus = TryIntersectTextRects(clip, clip);
    if(clipStatus == TextGlyphIntersection::Invalid)
        return false;
    if(clipStatus == TextGlyphIntersection::Invisible || color.a == 0.0f)
        return true;
    const f32 physicalSize = rasterMetrics->physicalSize;
    const f32 rasterPpem = rasterMetrics->rasterPpem;
    const u32 pixelSize = rasterMetrics->pixelSize;
    const f32 rasterScale = rasterMetrics->rasterScale;
    const Point deviceScale{ metrics.pixelScaleX, metrics.pixelScaleY };
    Core::Alloc::ScratchArena scratchArena(s_TextPaintCandidatesArena);
    Vector<usize, Core::Alloc::ScratchArena> candidates(scratchArena);
    candidates.reserve(layout.glyphs().size());
    // Validate every conservative candidate before raster preparation mutates the coverage cache.
    for(usize index = 0u; index < layout.glyphs().size(); ++index){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        const BakedFontAtlas* atlas = TextGlyphVisibility::SelectAtlas(glyph, physicalSize);
        const TextGlyphIntersection::Enum status = TextGlyphVisibility::Candidate(glyph, atlas, layout.fontSize(), rasterPpem, topLeft, clip, deviceScale);
        if(status == TextGlyphIntersection::Invalid)
            return false;
        if(status == TextGlyphIntersection::Visible)
            candidates.push_back(index);
    }
    if(candidates.empty())
        return true;
    for(const usize index : candidates){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        if(!TextGlyphVisibility::SelectAtlas(glyph, physicalSize) && !m_atlas->prepare(glyph.face, glyph.glyphId, pixelSize))
            return false;
    }
    // Exact rectangles remove conservative overinclusion before image admission; painter order remains unchanged.
    Vector<PreparedGlyph, Core::Alloc::ScratchArena> prepared(scratchArena);
    prepared.reserve(candidates.size());
    for(const usize index : candidates){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        PreparedGlyph item;
        if(const BakedFontAtlas* atlas = TextGlyphVisibility::SelectAtlas(glyph, physicalSize)){
            const auto rectangle = TextGlyphVisibility::AtlasRectangle(glyph, *atlas, layout.fontSize(), topLeft);
            if(!rectangle)
                return false;
            item.rectangle = *rectangle;
            if(item.rectangle.width > 0.0f && item.rectangle.height > 0.0f){
                const FontAtlasGlyph& record = *atlas->glyph(glyph.glyphId);
                item.sdfPage = &atlas->page(record.group);
                item.channel = record.channel;
                const SdfAtlasPageBinding& binding = (*item.sdfPage)->binding();
                item.uv = ResolveSdfGlyphUv(record, binding.width, binding.height);
            }
        }
        else{
            const AtlasGlyph* record = m_atlas->find(glyph.face, glyph.glyphId, pixelSize);
            if(!record){
                NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Prepared UI glyph must be present"));
                return false;
            }
            const auto rectangle = TextGlyphVisibility::CoverageRectangle(glyph, *record, rasterScale, topLeft, deviceScale);
            if(!rectangle)
                return false;
            item.rectangle = *rectangle;
            item.pageIndex = record->pageIndex;
            item.uv = ResolveCoverageGlyphUv(record->pixels);
        }
        const TextGlyphIntersection::Enum status = TryIntersectTextRects(item.rectangle, clip);
        if(status == TextGlyphIntersection::Invalid)
            return false;
        if(status == TextGlyphIntersection::Visible)
            prepared.push_back(item);
    }
    if(prepared.empty())
        return true;
    m_pages.clear();
    m_sdfPages.clear();
    for(auto& glyph : prepared){
        if(glyph.sdfPage)
            glyph.pageIndex = InternPreparedPage(m_sdfPages, *glyph.sdfPage);
        else{
            auto pageResult = m_atlas->page(glyph.pageIndex);
            if(!pageResult)
                return false;
            glyph.pageIndex = InternPreparedPage(m_pages, Move(*pageResult));
        }
    }
    if(!paint.prepareImages(m_pages, m_sdfPages))
        return false;
    for(const auto& glyph : prepared){
        const b8 drawn = glyph.sdfPage ? paint.drawSdfGlyph(m_sdfPages[glyph.pageIndex], glyph.channel, glyph.rectangle, glyph.uv, color) : paint.drawGlyph(m_pages[glyph.pageIndex], glyph.rectangle, glyph.uv, color);
        if(!drawn){
            NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Admitted UI glyph draw must succeed"));
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
