// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "service.h"

#include "atlas.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_paint{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static const BakedFontAtlas* SelectAtlas(const PlacedGlyph& glyph, f32 physicalSize){
    const SharedBakedFontAtlas& atlas = glyph.face->bakedAtlas();
    if(
        !atlas || physicalSize < static_cast<f32>(atlas->bakePpem()) * s_BakedFontAtlasMinScale
        || physicalSize > static_cast<f32>(atlas->bakePpem()) * s_BakedFontAtlasMaxScale || !atlas->glyph(glyph.glyphId)
    )
        return nullptr;
    return atlas.get();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TextService::paint(PaintBuilder& paint, const TextLayout& layout, Point topLeft, const Color& color){
    const DisplayMetrics& metrics = paint.displayMetrics();
    const f32 pixelScale = Max(metrics.pixelScaleX, metrics.pixelScaleY);
    const f32 physicalSize = Ceil(layout.fontSize() * pixelScale);
    if(
        layout.lines().empty() || !IsFinite(topLeft.x) || !IsFinite(topLeft.y) || !IsFinite(physicalSize)
        || physicalSize < 1.0f || physicalSize > 4096.0f || !IsFinite(color.r) || !IsFinite(color.g) || !IsFinite(color.b)
        || !IsFinite(color.a) || color.a < 0.0f || color.a > 1.0f
    )
        return false;
    const u32 pixelSize = static_cast<u32>(physicalSize);
    const f32 rasterScale = physicalSize / layout.fontSize();
    for(const PlacedGlyph& glyph : layout.glyphs()){
        if(!__hidden_ui_text_paint::SelectAtlas(glyph, physicalSize) && !m_atlas->prepare(glyph.face, glyph.glyphId, pixelSize))
            return false;
    }
    m_pages.clear();
    m_sdfPages.clear();
    for(const PlacedGlyph& glyph : layout.glyphs()){
        if(const BakedFontAtlas* atlas = __hidden_ui_text_paint::SelectAtlas(glyph, physicalSize)){
            const FontAtlasGlyph& record = *atlas->glyph(glyph.glyphId);
            if(record.drawable == 0u)
                continue;
            const SharedSdfAtlasPage& page = atlas->page(record.group);
            const bool present = FindIf(m_sdfPages.begin(), m_sdfPages.end(), [&page](const SharedSdfAtlasPage& existing){
                return existing.get() == page.get();
            }) != m_sdfPages.end();
            if(!present)
                m_sdfPages.push_back(page);
        }
        else{
            const AtlasGlyph* record = m_atlas->find(glyph.face, glyph.glyphId, pixelSize);
            NWB_FATAL_ASSERT_MSG(record, NWB_TEXT("Prepared UI glyph must be present"));
            if(record->pageIndex == s_GlyphAtlasNoPage)
                continue;
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
    for(const PlacedGlyph& glyph : layout.glyphs()){
        if(const BakedFontAtlas* atlas = __hidden_ui_text_paint::SelectAtlas(glyph, physicalSize)){
            const FontAtlasGlyph& record = *atlas->glyph(glyph.glyphId);
            if(record.drawable == 0u)
                continue;
            const SharedSdfAtlasPage& page = atlas->page(record.group);
            const f32 scale = layout.fontSize() / static_cast<f32>(atlas->unitsPerEm());
            const Rect rectangle{ topLeft.x + glyph.position.x + record.planeLeft * scale,
                topLeft.y + glyph.position.y + record.planeTop * scale,
                (record.planeRight - record.planeLeft) * scale, (record.planeBottom - record.planeTop) * scale };
            const Rect uv{ static_cast<f32>(record.x) / page->binding().width,
                static_cast<f32>(record.y) / page->binding().height, static_cast<f32>(record.width) / page->binding().width,
                static_cast<f32>(record.height) / page->binding().height };
            if(!paint.drawSdfGlyph(page, record.channel, rectangle, uv, color)){
                NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Admitted UI SDF glyph draw must succeed"));
                return false;
            }
        }
        else{
            const AtlasGlyph* record = m_atlas->find(glyph.face, glyph.glyphId, pixelSize);
            NWB_FATAL_ASSERT_MSG(record, NWB_TEXT("Prepared UI glyph must remain present"));
            if(record->pageIndex == s_GlyphAtlasNoPage)
                continue;
            const SharedGlyphPage page = m_atlas->page(record->pageIndex);
            const Rect rectangle{ topLeft.x + glyph.position.x + static_cast<f32>(record->bearingX) / rasterScale,
                topLeft.y + glyph.position.y - static_cast<f32>(record->bearingY) / rasterScale,
                record->pixels.width / rasterScale, record->pixels.height / rasterScale };
            const Rect uv{ record->pixels.x / s_GlyphAtlasPageExtent, record->pixels.y / s_GlyphAtlasPageExtent,
                record->pixels.width / s_GlyphAtlasPageExtent, record->pixels.height / s_GlyphAtlasPageExtent };
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

