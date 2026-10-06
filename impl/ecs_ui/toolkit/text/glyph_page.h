// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/global.h>
#include <impl/assets_font/asset.h>

#include <core/assets/ref.h>

#include <global/refcount_ptr.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_GlyphAtlasPageExtent = 512u;
inline constexpr usize s_GlyphAtlasMaxPages = 16u;
inline constexpr usize s_PaintMaxGlyphPages = 64u;

struct GlyphPageBinding{
    Core::Assets::AssetRef<Font> font;
    u64 fontGeneration = 0u;
    u64 atlasIdentity = 0u;
    u64 generation = 0u;
    u32 index = 0u;
    u32 width = 0u;
    u32 height = 0u;
};

[[nodiscard]] inline bool SameGlyphPageKey(const GlyphPageBinding& first, const GlyphPageBinding& second)noexcept{
    return
        first.font == second.font && first.fontGeneration == second.fontGeneration
        && first.atlasIdentity == second.atlasIdentity && first.index == second.index
    ;
}

[[nodiscard]] bool operator==(const GlyphPageBinding& lhs, const GlyphPageBinding& rhs)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Coverage is linear R8. Published pages are immutable; versions sharing an atlas/page identity only append glyphs.
// Repacking or replacing existing texels requires a new atlas identity. The caller's arena outlives all consumers.
class GlyphPage : NoCopy{
public:
    using Pixels = Vector<u8, Core::Alloc::GlobalArena>;


public:
    GlyphPage(Core::Alloc::GlobalArena& arena, const GlyphPageBinding& binding, Pixels&& pixels)noexcept;


public:
    [[nodiscard]] const GlyphPageBinding& binding()const noexcept{ return m_binding; }
    [[nodiscard]] const Pixels& pixels()const noexcept{ return m_pixels; }


private:
    GlyphPageBinding m_binding;
    Pixels m_pixels;
};

using SharedGlyphPage = RefCountPtr<RefCounter<GlyphPage>, ArenaRefDeleter<RefCounter<GlyphPage>, Core::Alloc::GlobalArena>>;

[[nodiscard]] SharedGlyphPage CreateGlyphPage(Core::Alloc::GlobalArena& arena, const GlyphPageBinding& binding, GlyphPage::Pixels&& pixels);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

