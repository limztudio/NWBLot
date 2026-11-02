// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/global.h>
#include <impl/ecs_ui/toolkit/text/types.adhd.h>
#include <impl/assets_font/asset.h>

#include <core/assets/ref.h>

#include <global/expected.h>
#include <global/refcount_ptr.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Coverage is linear R8.
// Published pages are immutable; versions sharing an atlas/page identity only append glyphs.
// Repacking or replacing existing texels requires a new atlas identity.
// The caller's arena outlives all consumers.
class GlyphPage : NoCopy{
public:
    using Pixels = Vector<u8, Core::Alloc::GlobalArena>;


public:
    GlyphPage(const GlyphPageBinding& binding, Pixels&& pixels)noexcept
        : m_binding(binding)
        , m_pixels(Move(pixels))
    {}


public:
    [[nodiscard]] const GlyphPageBinding& binding()const noexcept{ return m_binding; }

    [[nodiscard]] const Pixels& pixels()const noexcept{ return m_pixels; }


private:
    GlyphPageBinding m_binding;
    Pixels m_pixels;
};

using SharedGlyphPage = RefCountPtr<RefCounter<GlyphPage>, ArenaRefDeleter<RefCounter<GlyphPage>, Core::Alloc::GlobalArena>>;

[[nodiscard]] inline Expected<SharedGlyphPage> CreateGlyphPage(Core::Alloc::GlobalArena& arena, const GlyphPageBinding& binding, GlyphPage::Pixels&& pixels)noexcept{
    if(!ValidGlyphPageBinding(binding, pixels.size()))
        return MakeUnexpected(Failure{});
    auto* const page = NewArenaObject<RefCounter<GlyphPage>>(arena, binding, Move(pixels));
    if(!page)
        return MakeUnexpected(Failure{});
    return SharedGlyphPage(page, ArenaRefDeleter<RefCounter<GlyphPage>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

