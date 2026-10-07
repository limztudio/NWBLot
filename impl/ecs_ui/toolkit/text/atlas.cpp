// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "atlas.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_atlas{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextAtlasIdentity{ 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 NewIdentity(){
    const u64 identity = s_NextAtlasIdentity.fetch_add(1u, MemoryOrder::relaxed);
    NWB_FATAL_ASSERT_MSG(identity != 0u, NWB_TEXT("UI glyph atlas identity overflow"));
    return identity;
}

[[nodiscard]] static bool Place(const AtlasPage& page, u32 width, u32 height, u32& x, u32& y, u32& rowHeight)noexcept{
    x = page.cursorX;
    y = page.cursorY;
    rowHeight = page.rowHeight;
    if(x + width > s_GlyphAtlasPageExtent){
        x = 0u;
        y += rowHeight;
        rowHeight = 0u;
    }
    if(y + height > s_GlyphAtlasPageExtent)
        return false;
    rowHeight = Max(rowHeight, height);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GlyphAtlas::GlyphAtlas(Core::Alloc::GlobalArena& arena)
    : m_arena(arena)
    , m_glyphs(arena)
    , m_index(arena)
    , m_pages(arena)
    , m_bitmap(arena)
    , m_identity(__hidden_ui_text_atlas::NewIdentity())
{
    m_glyphs.reserve(s_GlyphAtlasMaxGlyphs);
    m_pages.reserve(s_GlyphAtlasMaxPages);
}

void GlyphAtlas::reset(){
    m_index.clear();
    m_glyphs.clear();
    m_pages.clear();
    m_identity = __hidden_ui_text_atlas::NewIdentity();
}

bool GlyphAtlas::prepare(const SharedFontFace& face, u32 glyphId, u32 pixelSize){
    if(!face || !face->valid() || pixelSize == 0u)
        return false;
    if(find(face, glyphId, pixelSize))
        return true;
    if(m_glyphs.size() >= s_GlyphAtlasMaxGlyphs || !face->rasterize(glyphId, pixelSize, m_bitmap))
        return false;
    AtlasGlyph record;
    record.face = face;
    record.glyphId = glyphId;
    record.pixelSize = pixelSize;
    record.bearingX = m_bitmap.bearingX;
    record.bearingY = m_bitmap.bearingY;
    if(m_bitmap.width == 0u || m_bitmap.height == 0u){
        appendGlyph(Move(record));
        return true;
    }
    if(m_bitmap.width > s_GlyphAtlasPageExtent - 2u || m_bitmap.height > s_GlyphAtlasPageExtent - 2u)
        return false;
    const u32 paddedWidth = m_bitmap.width + 2u;
    const u32 paddedHeight = m_bitmap.height + 2u;
    u32 x = 0u;
    u32 y = 0u;
    u32 rowHeight = 0u;
    usize pageIndex = 0u;
    while(pageIndex < m_pages.size()){
        const AtlasPage& page = m_pages[pageIndex];
        if(page.face.get() == face.get() && __hidden_ui_text_atlas::Place(page, paddedWidth, paddedHeight, x, y, rowHeight))
            break;
        ++pageIndex;
    }
    if(pageIndex == m_pages.size()){
        if(m_pages.size() >= s_GlyphAtlasMaxPages)
            return false;
        m_pages.emplace_back(m_arena, face);
        x = 0u;
        y = 0u;
        rowHeight = paddedHeight;
    }
    AtlasPage& page = m_pages[pageIndex];
    record.pageIndex = static_cast<u32>(pageIndex);
    record.pixels = { static_cast<f32>(x + 1u), static_cast<f32>(y + 1u),
        static_cast<f32>(m_bitmap.width), static_cast<f32>(m_bitmap.height) };
    for(u32 row = 0u; row < m_bitmap.height; ++row){
        const usize destination = static_cast<usize>(y + row + 1u) * s_GlyphAtlasPageExtent + x + 1u;
        NWB_MEMCPY(
            page.pixels.data() + destination,
            m_bitmap.width,
            m_bitmap.pixels.data() + static_cast<usize>(row) * m_bitmap.width,
            m_bitmap.width
        );
    }
    page.cursorX = x + paddedWidth;
    page.cursorY = y;
    page.rowHeight = rowHeight;
    page.dirty = true;
    appendGlyph(Move(record));
    return true;
}

const AtlasGlyph* GlyphAtlas::find(const SharedFontFace& face, u32 glyphId, u32 pixelSize)const{
    const auto found = m_index.find(GlyphKey{ face.get(), glyphId, pixelSize });
    return found == m_index.end() ? nullptr : &m_glyphs[found.value()];
}

void GlyphAtlas::appendGlyph(AtlasGlyph&& record){
    const GlyphKey key{ record.face.get(), record.glyphId, record.pixelSize };
    const usize index = m_glyphs.size();
    m_glyphs.push_back(Move(record));
    if(!m_index.emplace(key, index).second)
        TerminateInvariant();
}

SharedGlyphPage GlyphAtlas::page(u32 index){
    if(index >= m_pages.size())
        return {};
    AtlasPage& page = m_pages[index];
    if(page.dirty){
        if(page.generation == Limit<u64>::s_Max)
            return {};
        GlyphPage::Pixels copy(m_arena);
        copy.assign(page.pixels.begin(), page.pixels.end());
        const GlyphPageBinding binding{ page.face->identity(), page.face->generation(), m_identity,
            page.generation + 1u, index, s_GlyphAtlasPageExtent, s_GlyphAtlasPageExtent };
        SharedGlyphPage published = CreateGlyphPage(m_arena, binding, Move(copy));
        if(!published)
            return {};
        page.published = Move(published);
        ++page.generation;
        page.dirty = false;
    }
    return page.published;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

