// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "font.h"
#include "glyph_page.h"

#include <global/containers.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_GlyphAtlasMaxGlyphs = 4096u;
inline constexpr u32 s_GlyphAtlasNoPage = 0xffffffffu;

struct AtlasGlyph{
    SharedFontFace face;
    u32 glyphId = 0u;
    u32 pixelSize = 0u;
    u32 pageIndex = s_GlyphAtlasNoPage;
    Rect pixels;
    i32 bearingX = 0;
    i32 bearingY = 0;
};

struct AtlasPage{
    SharedFontFace face;
    GlyphPage::Pixels pixels;
    SharedGlyphPage published;
    u32 cursorX = 0u;
    u32 cursorY = 0u;
    u32 rowHeight = 0u;
    u64 generation = 0u;
    bool dirty = false;

    AtlasPage(Core::Alloc::GlobalArena& arena, const SharedFontFace& font)
        : face(font)
        , pixels(arena)
    {
        pixels.resize(static_cast<usize>(s_GlyphAtlasPageExtent) * s_GlyphAtlasPageExtent, 0u);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Only unused padded rectangles are written; page publication copies after a batch of mutations.
class GlyphAtlas final : NoCopy{
private:
    struct GlyphKey{
        SharedFontFace::pointer face = nullptr;
        u32 glyphId = 0u;
        u32 pixelSize = 0u;
    };

    struct GlyphKeyHasher{
        [[nodiscard]] usize operator()(const GlyphKey& key)const noexcept{
            usize seed = 0u;
            ::HashCombine(seed, key.face);
            ::HashCombine(seed, key.glyphId);
            ::HashCombine(seed, key.pixelSize);
            return seed;
        }
    };

    struct GlyphKeyEqualTo{
        [[nodiscard]] bool operator()(const GlyphKey& lhs, const GlyphKey& rhs)const noexcept{
            return lhs.face == rhs.face && lhs.glyphId == rhs.glyphId && lhs.pixelSize == rhs.pixelSize;
        }
    };

    using GlyphIndex = HashMap<GlyphKey, usize, Core::Alloc::GlobalArena, GlyphKeyHasher, GlyphKeyEqualTo>;


public:
    explicit GlyphAtlas(Core::Alloc::GlobalArena& arena);


public:
    void reset();
    [[nodiscard]] bool prepare(const SharedFontFace& face, u32 glyphId, u32 pixelSize);
    [[nodiscard]] const AtlasGlyph* find(const SharedFontFace& face, u32 glyphId, u32 pixelSize)const;
    [[nodiscard]] SharedGlyphPage page(u32 index);
    [[nodiscard]] usize pageCount()const{ return m_pages.size(); }
    [[nodiscard]] usize glyphCount()const{ return m_glyphs.size(); }


private:
    void appendGlyph(AtlasGlyph&& record);


private:
    Core::Alloc::GlobalArena& m_arena;
    PaintVector<AtlasGlyph> m_glyphs;
    GlyphIndex m_index;
    PaintVector<AtlasPage> m_pages;
    GlyphBitmap m_bitmap;
    u64 m_identity = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

