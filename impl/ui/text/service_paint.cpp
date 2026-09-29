// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "service.h"

#include "atlas.h"
#include "glyph_visibility.h"

#include <core/alloc/scratch.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TextService::paint(PaintBuilder& paint, const TextLayout& layout, Point topLeft, const Color& color){
    const DisplayMetrics& metrics = paint.displayMetrics();
    const f32 pixelScale = Max(metrics.pixelScaleX, metrics.pixelScaleY);
    const f32 physicalSize = layout.fontSize() * pixelScale;
    const f32 rasterPpem = Ceil(physicalSize);
    if(
        layout.lines().empty() || !IsFinite(layout.fontSize())
        || layout.fontSize() < 1.0f / 64.0f || layout.fontSize() > 2048.0f
        || !IsFinite(topLeft.x) || !IsFinite(topLeft.y) || !IsFinite(rasterPpem)
        || rasterPpem < 1.0f || rasterPpem > 4096.0f || !IsFinite(color.r) || !IsFinite(color.g) || !IsFinite(color.b)
        || !IsFinite(color.a) || color.a < 0.0f || color.a > 1.0f
    )
        return false;
    const Rect clip = paint.currentClip();
    const TextGlyphIntersection::Enum clipStatus = TextGlyphVisibility::intersect(clip, clip);
    if(clipStatus == TextGlyphIntersection::Invalid)
        return false;
    if(clipStatus == TextGlyphIntersection::Invisible || color.a == 0.0f)
        return true;
    const u32 pixelSize = static_cast<u32>(rasterPpem);
    const f32 rasterScale = rasterPpem / layout.fontSize();
    const Point deviceScale{ metrics.pixelScaleX, metrics.pixelScaleY };
    Core::Alloc::ScratchArena scratchArena(Name("impl/ui/text/paint_candidates"));
    Vector<usize, Core::Alloc::ScratchArena> candidates(scratchArena);
    candidates.reserve(layout.glyphs().size());
    // Validate every conservative candidate before raster preparation mutates the coverage cache.
    for(usize index = 0u; index < layout.glyphs().size(); ++index){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        const BakedFontAtlas* atlas = TextGlyphVisibility::selectAtlas(glyph, physicalSize);
        const TextGlyphIntersection::Enum status = TextGlyphVisibility::candidate(
            glyph, atlas, layout.fontSize(), rasterPpem, topLeft, clip, deviceScale
        );
        if(status == TextGlyphIntersection::Invalid)
            return false;
        if(status == TextGlyphIntersection::Visible)
            candidates.push_back(index);
    }
    if(candidates.empty())
        return true;
    for(const usize index : candidates){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        if(!TextGlyphVisibility::selectAtlas(glyph, physicalSize) && !m_atlas->prepare(glyph.face, glyph.glyphId, pixelSize))
            return false;
    }
    // Exact rectangles remove conservative overinclusion before image admission; painter order remains unchanged.
    usize retained = 0u;
    for(const usize index : candidates){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        Rect rectangle;
        if(const BakedFontAtlas* atlas = TextGlyphVisibility::selectAtlas(glyph, physicalSize)){
            if(!TextGlyphVisibility::atlasRectangle(glyph, *atlas, layout.fontSize(), topLeft, rectangle))
                return false;
        }
        else{
            const AtlasGlyph* record = m_atlas->find(glyph.face, glyph.glyphId, pixelSize);
            if(!record){
                NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Prepared UI glyph must be present"));
                return false;
            }
            if(!TextGlyphVisibility::coverageRectangle(glyph, *record, rasterScale, topLeft, rectangle, deviceScale))
                return false;
        }
        const TextGlyphIntersection::Enum status = TextGlyphVisibility::intersect(rectangle, clip);
        if(status == TextGlyphIntersection::Invalid)
            return false;
        if(status == TextGlyphIntersection::Visible){
            candidates[retained] = index;
            ++retained;
        }
    }
    candidates.resize(retained);
    if(candidates.empty())
        return true;
    m_pages.clear();
    m_sdfPages.clear();
    for(const usize index : candidates){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        if(const BakedFontAtlas* atlas = TextGlyphVisibility::selectAtlas(glyph, physicalSize)){
            const FontAtlasGlyph& record = *atlas->glyph(glyph.glyphId);
            const SharedSdfAtlasPage& page = atlas->page(record.group);
            const bool present = FindIf(m_sdfPages.begin(), m_sdfPages.end(), [&page](const SharedSdfAtlasPage& existing){
                return existing.get() == page.get();
            }) != m_sdfPages.end();
            if(!present)
                m_sdfPages.push_back(page);
        }
        else{
            const AtlasGlyph* record = m_atlas->find(glyph.face, glyph.glyphId, pixelSize);
            if(!record){
                NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Prepared UI glyph must remain present"));
                return false;
            }
            SharedGlyphPage page = m_atlas->page(record->pageIndex);
            if(!page)
                return false;
            const bool present = FindIf(m_pages.begin(), m_pages.end(), [&page](const SharedGlyphPage& existing){
                return existing.get() == page.get();
            }) != m_pages.end();
            if(!present)
                m_pages.push_back(Move(page));
        }
    }
    if(!paint.prepareImages(m_pages.data(), m_pages.size(), m_sdfPages.data(), m_sdfPages.size()))
        return false;
    for(const usize index : candidates){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        if(const BakedFontAtlas* atlas = TextGlyphVisibility::selectAtlas(glyph, physicalSize)){
            const FontAtlasGlyph& record = *atlas->glyph(glyph.glyphId);
            const SharedSdfAtlasPage& page = atlas->page(record.group);
            Rect rectangle;
            if(!TextGlyphVisibility::atlasRectangle(glyph, *atlas, layout.fontSize(), topLeft, rectangle))
                return false;
            const Rect uv{
                static_cast<f32>(record.x) / page->binding().width,
                static_cast<f32>(record.y) / page->binding().height,
                static_cast<f32>(record.width) / page->binding().width,
                static_cast<f32>(record.height) / page->binding().height
            };
            if(!paint.drawSdfGlyph(page, record.channel, rectangle, uv, color)){
                NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Admitted UI SDF glyph draw must succeed"));
                return false;
            }
        }
        else{
            const AtlasGlyph* record = m_atlas->find(glyph.face, glyph.glyphId, pixelSize);
            if(!record){
                NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Prepared UI glyph must remain present"));
                return false;
            }
            const SharedGlyphPage page = m_atlas->page(record->pageIndex);
            if(!page)
                return false;
            Rect rectangle;
            if(!TextGlyphVisibility::coverageRectangle(glyph, *record, rasterScale, topLeft, rectangle, deviceScale))
                return false;
            const Rect uv{
                record->pixels.x / s_GlyphAtlasPageExtent,
                record->pixels.y / s_GlyphAtlasPageExtent,
                record->pixels.width / s_GlyphAtlasPageExtent,
                record->pixels.height / s_GlyphAtlasPageExtent
            };
            if(!paint.drawGlyph(page, rectangle, uv, color)){
                NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Admitted UI glyph draw must succeed"));
                return false;
            }
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

