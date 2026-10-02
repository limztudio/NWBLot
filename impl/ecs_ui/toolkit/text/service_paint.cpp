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


namespace __hidden_ui_text_paint{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    Core::Alloc::ScratchArena scratchArena(Name("impl/ecs_ui/toolkit/text/paint_candidates"));
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
    Vector<__hidden_ui_text_paint::PreparedGlyph, Core::Alloc::ScratchArena> prepared(scratchArena);
    prepared.reserve(candidates.size());
    for(const usize index : candidates){
        const PlacedGlyph& glyph = layout.glyphs()[index];
        __hidden_ui_text_paint::PreparedGlyph item;
        if(const BakedFontAtlas* atlas = TextGlyphVisibility::selectAtlas(glyph, physicalSize)){
            if(!TextGlyphVisibility::atlasRectangle(glyph, *atlas, layout.fontSize(), topLeft, item.rectangle))
                return false;
            if(item.rectangle.width > 0.0f && item.rectangle.height > 0.0f){
                const FontAtlasGlyph& record = *atlas->glyph(glyph.glyphId);
                item.sdfPage = &atlas->page(record.group);
                item.channel = record.channel;
                const SdfAtlasPageBinding& binding = (*item.sdfPage)->binding();
                item.uv = { static_cast<f32>(record.x) / binding.width, static_cast<f32>(record.y) / binding.height,
                    static_cast<f32>(record.width) / binding.width, static_cast<f32>(record.height) / binding.height };
            }
        }
        else{
            const AtlasGlyph* record = m_atlas->find(glyph.face, glyph.glyphId, pixelSize);
            if(!record){
                NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Prepared UI glyph must be present"));
                return false;
            }
            if(!TextGlyphVisibility::coverageRectangle(glyph, *record, rasterScale, topLeft, item.rectangle, deviceScale))
                return false;
            item.pageIndex = record->pageIndex;
            item.uv = { record->pixels.x / s_GlyphAtlasPageExtent, record->pixels.y / s_GlyphAtlasPageExtent,
                record->pixels.width / s_GlyphAtlasPageExtent, record->pixels.height / s_GlyphAtlasPageExtent };
        }
        const TextGlyphIntersection::Enum status = TextGlyphVisibility::intersect(item.rectangle, clip);
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
        if(glyph.sdfPage){
            const SharedSdfAtlasPage& page = *glyph.sdfPage;
            const auto existing = FindIf(m_sdfPages.begin(), m_sdfPages.end(), [&page](const SharedSdfAtlasPage& item){
                return item.get() == page.get();
            });
            glyph.pageIndex = static_cast<u32>(existing - m_sdfPages.begin());
            if(existing == m_sdfPages.end())
                m_sdfPages.push_back(page);
        }
        else{
            SharedGlyphPage page = m_atlas->page(glyph.pageIndex);
            if(!page)
                return false;
            const auto existing = FindIf(m_pages.begin(), m_pages.end(), [&page](const SharedGlyphPage& item){
                return item.get() == page.get();
            });
            glyph.pageIndex = static_cast<u32>(existing - m_pages.begin());
            if(existing == m_pages.end())
                m_pages.push_back(Move(page));
        }
    }
    if(!paint.prepareImages(m_pages.data(), m_pages.size(), m_sdfPages.data(), m_sdfPages.size()))
        return false;
    for(const auto& glyph : prepared){
        const bool drawn = glyph.sdfPage
            ? paint.drawSdfGlyph(m_sdfPages[glyph.pageIndex], glyph.channel, glyph.rectangle, glyph.uv, color)
            : paint.drawGlyph(m_pages[glyph.pageIndex], glyph.rectangle, glyph.uv, color)
        ;
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

