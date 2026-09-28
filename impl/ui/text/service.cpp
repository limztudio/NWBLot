// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "service.h"

#include "atlas.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_service{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextServiceIdentity{ 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 NewIdentity(){
    const u64 identity = s_NextServiceIdentity.fetch_add(1u, MemoryOrder::relaxed);
    NWB_FATAL_ASSERT_MSG(identity != 0u, NWB_TEXT("UI text service identity overflow"));
    return identity;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextService::TextService(Core::Alloc::GlobalArena& arena)
    : m_shaper(arena)
    , m_layoutBuilder(arena, m_shaper)
    , m_atlas(Core::MakeGlobalUnique<GlyphAtlas>(arena, arena))
    , m_pages(arena)
    , m_identity(__hidden_ui_text_service::NewIdentity())
{
    m_pages.reserve(s_GlyphAtlasMaxPages);
}

TextService::~TextService() = default;

bool TextService::setFonts(const FontSource* sources, usize count){
    if(m_generation == Limit<u64>::s_Max || !m_shaper.setFonts(sources, count))
        return false;
    m_pages.clear();
    m_atlas->reset();
    ++m_generation;
    return true;
}

TextLayoutStatus::Enum TextService::layout(const ShapeRequest& request, TextLayout& output){
    return m_layoutBuilder.layout(request, output);
}

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
        if(!m_atlas->prepare(glyph.face, glyph.glyphId, pixelSize))
            return false;
    }
    m_pages.clear();
    for(const PlacedGlyph& glyph : layout.glyphs()){
        const AtlasGlyph* record = m_atlas->find(glyph.face, glyph.glyphId, pixelSize);
        NWB_FATAL_ASSERT_MSG(record, NWB_TEXT("Prepared UI glyph must be present"));
        if(record->pageIndex == s_GlyphAtlasNoPage)
            continue;
        SharedGlyphPage page = m_atlas->page(record->pageIndex);
        if(!page)
            return false;
        bool present = false;
        for(const SharedGlyphPage& existing : m_pages){
            if(existing.get() == page.get()){
                present = true;
                break;
            }
        }
        if(!present)
            m_pages.push_back(Move(page));
    }
    if(!paint.prepareGlyphPages(m_pages.data(), m_pages.size()))
        return false;
    for(const PlacedGlyph& glyph : layout.glyphs()){
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
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

