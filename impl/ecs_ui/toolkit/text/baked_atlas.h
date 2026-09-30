// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "sdf_page.h"

#include <impl/assets_font_atlas/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Native coverage preserves small descenders that lose their bottom row when the baked SDF is downscaled.
inline constexpr f32 s_BakedFontAtlasMinScale = 0.75f;
inline constexpr f32 s_BakedFontAtlasMaxScale = 1.5f;

// This version copies only rendering metadata and image bytes. FontFace keeps it with its exact shaping source.
class BakedFontAtlas : NoCopy{
public:
    BakedFontAtlas(Core::Alloc::GlobalArena& arena, const FontAtlas& atlas, u64 fontGeneration);


public:
    [[nodiscard]] bool valid()const{ return m_ready; }
    [[nodiscard]] u32 unitsPerEm()const{ return m_unitsPerEm; }
    [[nodiscard]] u32 bakePpem()const{ return m_bakePpem; }
    [[nodiscard]] const FontAtlasGlyph* glyph(u32 glyphId)const;
    [[nodiscard]] const SharedSdfAtlasPage& page(u32 group)const;


private:
    Vector<FontAtlasGlyph, Core::Alloc::GlobalArena> m_glyphs;
    Vector<SharedSdfAtlasPage, Core::Alloc::GlobalArena> m_pages;
    u32 m_unitsPerEm = 0u;
    u32 m_bakePpem = 0u;
    bool m_ready = false;
};

using SharedBakedFontAtlas = RefCountPtr<RefCounter<BakedFontAtlas>, ArenaRefDeleter<RefCounter<BakedFontAtlas>, Core::Alloc::GlobalArena>>;

[[nodiscard]] SharedBakedFontAtlas CreateBakedFontAtlas(Core::Alloc::GlobalArena& arena, const FontAtlas& atlas, u64 fontGeneration);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

