// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/global.h>
#include <impl/ecs_ui/toolkit/paint_types.adhd.h>
#include <impl/ecs_ui/toolkit/text/types.adhd.h>
#include <impl/assets_font_atlas/asset.h>

#include <global/expected.h>
#include <global/refcount_ptr.h>
#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One to four channels store independent linear SDF pages.
// Published pixels and their binding never change.
// A replacement/repack uses a fresh atlas identity; the caller's arena outlives snapshots and GPU consumers.
class SdfAtlasPage : NoCopy{
public:
    using Pixels = Vector<u8, Core::Alloc::GlobalArena>;


public:
    SdfAtlasPage(const SdfAtlasPageBinding& binding, Pixels&& pixels)noexcept
        : m_binding(binding)
        , m_pixels(Move(pixels))
    {}


public:
    [[nodiscard]] const SdfAtlasPageBinding& binding()const noexcept{ return m_binding; }

    [[nodiscard]] const Pixels& pixels()const noexcept{ return m_pixels; }


private:
    SdfAtlasPageBinding m_binding;
    Pixels m_pixels;
};

using SharedSdfAtlasPage = RefCountPtr<RefCounter<SdfAtlasPage>, ArenaRefDeleter<RefCounter<SdfAtlasPage>, Core::Alloc::GlobalArena>>;

// The factory calculates pixelsSha256 from the exact immutable bytes; callers need not supply it.
[[nodiscard]] Expected<SharedSdfAtlasPage> CreateSdfAtlasPage(Core::Alloc::GlobalArena& arena, const SdfAtlasPageBinding& binding, SdfAtlasPage::Pixels&& pixels)noexcept;
[[nodiscard]] Expected<SharedSdfAtlasPage> CreateSdfAtlasPage(Core::Alloc::GlobalArena& arena, const FontAtlas& atlas, u64 fontGeneration, u64 atlasIdentity, u32 groupIndex);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

