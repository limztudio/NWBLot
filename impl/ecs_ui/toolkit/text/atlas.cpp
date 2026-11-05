// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "atlas.h"

#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_atlas{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextAtlasIdentity{ 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AtlasPage::AtlasPage(Core::Alloc::GlobalArena& arena, const SharedFontFace& font)
    : face(font)
    , pixels(arena)
{
    pixels.resize(static_cast<usize>(s_GlyphAtlasPageExtent) * s_GlyphAtlasPageExtent, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize GlyphAtlas::GlyphKeyHasher::operator()(const GlyphKey& key)const noexcept{
    usize seed = 0u;
    ::HashCombine(seed, key.face);
    ::HashCombine(seed, key.glyphId);
    ::HashCombine(seed, key.pixelSize);
    return seed;
}

GlyphAtlas::GlyphAtlas(Core::Alloc::GlobalArena& arena)
    : m_arena(arena)
    , m_glyphs(arena)
    , m_index(arena)
    , m_pages(arena)
    , m_bitmap(arena)
    , m_identity(NextNonWrappingIdentity(__hidden_ui_text_atlas::s_NextAtlasIdentity))
{
    m_glyphs.reserve(s_GlyphAtlasMaxGlyphs);
    m_pages.reserve(s_GlyphAtlasMaxPages);
}

void GlyphAtlas::reset()noexcept{
    m_index.clear();
    m_glyphs.clear();
    m_pages.clear();
    m_identity = NextNonWrappingIdentity(__hidden_ui_text_atlas::s_NextAtlasIdentity);
}

b8 GlyphAtlas::prepare(const SharedFontFace& face, u32 glyphId, u32 pixelSize){
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
    if(m_bitmap.width > s_GlyphAtlasPageExtent - s_GlyphAtlasGlyphGutter || m_bitmap.height > s_GlyphAtlasPageExtent - s_GlyphAtlasGlyphGutter)
        return false;
    const u32 paddedWidth = m_bitmap.width + s_GlyphAtlasGlyphGutter;
    const u32 paddedHeight = m_bitmap.height + s_GlyphAtlasGlyphGutter;
    GlyphAtlasPlacement placement;
    usize pageIndex = 0u;
    while(pageIndex < m_pages.size()){
        const AtlasPage& page = m_pages[pageIndex];
        if(page.face.get() == face.get()){
            const auto placed = TryPlaceGlyphInAtlasPage(page.cursorX, page.cursorY, page.rowHeight, paddedWidth, paddedHeight);
            if(placed){
                placement = *placed;
                break;
            }
        }
        ++pageIndex;
    }
    if(pageIndex == m_pages.size()){
        if(m_pages.size() >= s_GlyphAtlasMaxPages)
            return false;
        m_pages.emplace_back(m_arena, face);
        placement = { 0u, 0u, paddedHeight };
    }
    AtlasPage& page = m_pages[pageIndex];
    record.pageIndex = static_cast<u32>(pageIndex);
    record.pixels = { static_cast<f32>(placement.x + 1u), static_cast<f32>(placement.y + 1u), static_cast<f32>(m_bitmap.width), static_cast<f32>(m_bitmap.height) };
    CopyGlyphBitmapToAtlasPage(placement.x, placement.y, m_bitmap.width, m_bitmap.height, m_bitmap.pixels, page.pixels);
    page.cursorX = placement.x + paddedWidth;
    page.cursorY = placement.y;
    page.rowHeight = placement.rowHeight;
    page.dirty = true;
    appendGlyph(Move(record));
    return true;
}

const AtlasGlyph* GlyphAtlas::find(const SharedFontFace& face, u32 glyphId, u32 pixelSize)const noexcept{
    const auto found = m_index.find(GlyphKey{ face.get(), glyphId, pixelSize });
    return found == m_index.end() ? nullptr : &m_glyphs[found.value()];
}

Expected<SharedGlyphPage> GlyphAtlas::page(u32 index){
    if(index >= m_pages.size())
        return MakeUnexpected(Failure{});
    AtlasPage& page = m_pages[index];
    if(page.dirty){
        if(page.generation == Limit<u64>::s_Max)
            return MakeUnexpected(Failure{});
        GlyphPage::Pixels copy(m_arena);
        copy.assign(page.pixels.begin(), page.pixels.end());
        const GlyphPageBinding binding{ page.face->identity(), page.face->generation(), m_identity, page.generation + 1u, index, s_GlyphAtlasPageExtent, s_GlyphAtlasPageExtent };
        auto published = CreateGlyphPage(m_arena, binding, Move(copy));
        if(!published)
            return MakeUnexpected(Failure{});
        page.published = Move(*published);
        ++page.generation;
        page.dirty = false;
    }
    return page.published;
}

void GlyphAtlas::appendGlyph(AtlasGlyph&& record){
    const GlyphKey key{ record.face.get(), record.glyphId, record.pixelSize };
    const usize index = m_glyphs.size();
    m_glyphs.push_back(Move(record));
    if(!m_index.emplace(key, index).second)
        TerminateInvariant();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

