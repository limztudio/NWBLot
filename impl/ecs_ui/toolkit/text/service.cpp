// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "service.h"
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


namespace __hidden_ui_text_service{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextServiceIdentity{ 1u };

[[nodiscard]] static Expected<SIMDVectorDoubleResult> LoadSdfSupport(const PlacedGlyph& placed, const BakedFontAtlas& atlas, const f32 fontSize, const DisplayMetrics& display)noexcept{
    const auto plane = TextGlyphVisibility::LoadAtlasRectangle(placed, atlas, fontSize, {});
    if(!plane)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble planeSize = VectorDoubleSwizzle<2, 3, 2, 3>(plane->value);
    if((VectorDoubleMoveMask(VectorDoubleLessOrEqual(planeSize, VectorDoubleZero())) & VectorDoubleComponentMask::s_DoubleXY) != 0u)
        return plane;
    const FontAtlasGlyph& glyph = *atlas.glyph(placed.glyphId);
    const SdfAtlasPage& page = *atlas.page(glyph.group);
    const SdfAtlasPageBinding& binding = page.binding();
    const auto& pixels = page.pixels();
    Double4 planeVec;
    StoreDouble(plane->value, planeVec);
    Double4 support;
    if(!TryLoadSdfGlyphSupport(planeVec, glyph, binding.width, binding.height, binding.channelCount, binding.spreadPixels, Span<const u8>(pixels.data(), pixels.size()), display, support))
        return MakeUnexpected(Failure{});
    return SIMDVectorDoubleResult{ LoadDouble(support) };
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
    TextRasterMetrics rasterMetrics;
    if(!TryResolveTextRasterMetrics(layout.fontSize(), display, rasterMetrics))
        return MakeUnexpected(Failure{});
    const f32 physicalSize = rasterMetrics.physicalSize;
    const f32 rasterScale = rasterMetrics.rasterScale;
    const u32 pixelSize = rasterMetrics.pixelSize;
    const Point logical = layout.measure();
    const Rect ink = layout.inkBounds();
    const SIMDVectorDouble inkOrigin = VectorDoubleSet(ink.x, ink.y, 0.0, 0.0);
    const SIMDVectorDouble inkEnd = VectorDoubleAdd(inkOrigin, VectorDoubleSet(ink.width, ink.height, 0.0, 0.0));
    const SIMDVectorDouble logicalSize = VectorDoubleSet(logical.x, logical.y, 0.0, 0.0);
    SIMDVectorDouble minimum = VectorDoubleSelect(inkOrigin, VectorDoubleZero(), VectorDoubleLess(VectorDoubleZero(), inkOrigin));
    SIMDVectorDouble maximum = VectorDoubleSelect(inkEnd, logicalSize, VectorDoubleGreater(logicalSize, inkEnd));
    for(const PlacedGlyph& glyph : layout.glyphs()){
        SIMDVectorDouble first;
        SIMDVectorDouble last;
        if(const BakedFontAtlas* atlas = TextGlyphVisibility::SelectAtlas(glyph, physicalSize)){
            const auto support = LoadSdfSupport(glyph, *atlas, layout.fontSize(), display);
            if(!support)
                return MakeUnexpected(Failure{});
            if(VectorDoubleGetZ(support->value) <= 0.0 || VectorDoubleGetW(support->value) <= 0.0)
                continue;
            first = VectorDoubleSelect(VectorDoubleZero(), support->value, s_DoubleSIMDMaskXY);
            last = VectorDoubleAdd(first, VectorDoubleSwizzle<2, 3, 2, 3>(support->value));
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
            if(raster->width <= 0.0f || raster->height <= 0.0f)
                continue;
            const SIMDVectorDouble scale = VectorDoubleReplicate(static_cast<f64>(rasterScale));
            const SIMDVectorDouble origin = VectorDoubleSet(glyph.position.x, glyph.position.y, 0.0, 0.0);
            const SIMDVectorDouble pixelOrigin = VectorDoubleSet(raster->x, raster->y, 0.0, 0.0);
            const SIMDVectorDouble pixelEnd = VectorDoubleAdd(pixelOrigin, VectorDoubleSet(raster->width, raster->height, 0.0, 0.0));
            // CoverageRectangle snaps the bitmap origin to the nearest physical pixel; every translation phase differs by at most half a pixel on each axis.
            const SIMDVectorDouble snap = VectorDoubleDivide(VectorDoubleSet(s_TextHalfPixel, s_TextHalfPixel, 0.0, 0.0), VectorDoubleSet(display.pixelScaleX, display.pixelScaleY, 1.0, 1.0));
            first = VectorDoubleSubtract(VectorDoubleAdd(origin, VectorDoubleDivide(pixelOrigin, scale)), snap);
            last = VectorDoubleAdd(VectorDoubleAdd(origin, VectorDoubleDivide(pixelEnd, scale)), snap);
        }
        minimum = VectorDoubleSelect(minimum, first, VectorDoubleLess(first, minimum));
        maximum = VectorDoubleSelect(maximum, last, VectorDoubleGreater(last, maximum));
    }
    Rect measured;
    if(!TryMakeMeasuredTextRect(VectorDoubleGetX(minimum), VectorDoubleGetY(minimum), VectorDoubleGetX(maximum), VectorDoubleGetY(maximum), measured))
        return MakeUnexpected(Failure{});
    return measured;
}

b8 TextService::paint(PaintBuilder& paint, const TextLayout& layout, Point topLeft, const Color& color){
    using namespace __hidden_ui_text_service;
    const DisplayMetrics& metrics = paint.displayMetrics();
    TextRasterMetrics rasterMetrics;
    if(layout.lines().empty() || !TryResolveTextRasterMetrics(layout.fontSize(), metrics, rasterMetrics) || !IsFinite(topLeft.x) || !IsFinite(topLeft.y) || !IsValidPaintColor(color))
        return false;
    const Rect clip = paint.currentClip();
    const TextGlyphIntersection::Enum clipStatus = TryIntersectTextRects(clip, clip);
    if(clipStatus == TextGlyphIntersection::Invalid)
        return false;
    if(clipStatus == TextGlyphIntersection::Invisible || color.a == 0.0f)
        return true;
    const f32 physicalSize = rasterMetrics.physicalSize;
    const f32 rasterPpem = rasterMetrics.rasterPpem;
    const u32 pixelSize = rasterMetrics.pixelSize;
    const f32 rasterScale = rasterMetrics.rasterScale;
    const Point deviceScale{ metrics.pixelScaleX, metrics.pixelScaleY };
    Core::Alloc::ScratchArena scratchArena(Name("impl/ecs_ui/toolkit/text/paint_candidates"));
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

