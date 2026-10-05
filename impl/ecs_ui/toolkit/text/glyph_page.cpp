// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "glyph_page.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool operator==(const GlyphPageBinding& lhs, const GlyphPageBinding& rhs)noexcept{
    return
        lhs.font == rhs.font && lhs.fontGeneration == rhs.fontGeneration && lhs.atlasIdentity == rhs.atlasIdentity
        && lhs.generation == rhs.generation && lhs.index == rhs.index && lhs.width == rhs.width && lhs.height == rhs.height
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GlyphPage::GlyphPage(Core::Alloc::GlobalArena& arena, const GlyphPageBinding& binding, Pixels&& pixels)
    : m_binding(binding)
    , m_pixels(arena)
{
    m_pixels = Move(pixels);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SharedGlyphPage CreateGlyphPage(Core::Alloc::GlobalArena& arena, const GlyphPageBinding& binding, GlyphPage::Pixels&& pixels){
    if(
        !binding.font.valid() || binding.fontGeneration == 0u || binding.atlasIdentity == 0u || binding.generation == 0u
        || binding.index >= s_GlyphAtlasMaxPages || binding.width == 0u || binding.height == 0u
        || binding.width > s_GlyphAtlasPageExtent || binding.height > s_GlyphAtlasPageExtent
        || pixels.size() != static_cast<usize>(binding.width) * binding.height
    )
        return {};
    return SharedGlyphPage(
        NewArenaObject<RefCounter<GlyphPage>>(arena, arena, binding, Move(pixels)),
        ArenaRefDeleter<RefCounter<GlyphPage>, Core::Alloc::GlobalArena>(&arena),
        s_AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

