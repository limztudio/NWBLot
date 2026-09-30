// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/global.h>
#include <impl/assets_font_atlas/asset.h>

#include <global/refcount_ptr.h>
#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_SdfDistanceEncodingFreeTypeU8 = s_FontAtlasDistanceEncoding;

struct SdfAtlasPageBinding{
    Core::Assets::AssetRef<Font> font;
    Sha256Digest fontSha256;
    Sha256Digest pixelsSha256;
    u64 fontGeneration = 0u;
    u64 atlasIdentity = 0u;
    u64 generation = 0u;
    u32 index = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 spreadPixels = 0u;
    u32 distanceEncoding = s_SdfDistanceEncodingFreeTypeU8;
};

[[nodiscard]] bool operator==(const SdfAtlasPageBinding& lhs, const SdfAtlasPageBinding& rhs)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// RGBA channels are four independent linear SDF pages. Published pixels and their binding never change.
// A replacement/repack uses a fresh atlas identity; the caller's arena outlives snapshots and GPU consumers.
class SdfAtlasPage : NoCopy{
public:
    using Pixels = Vector<u8, Core::Alloc::GlobalArena>;


public:
    SdfAtlasPage(Core::Alloc::GlobalArena& arena, const SdfAtlasPageBinding& binding, Pixels&& pixels);


public:
    [[nodiscard]] const SdfAtlasPageBinding& binding()const{ return m_binding; }
    [[nodiscard]] const Pixels& pixels()const{ return m_pixels; }


private:
    SdfAtlasPageBinding m_binding;
    Pixels m_pixels;
};

using SharedSdfAtlasPage = RefCountPtr<RefCounter<SdfAtlasPage>, ArenaRefDeleter<RefCounter<SdfAtlasPage>, Core::Alloc::GlobalArena>>;

// The factory calculates pixelsSha256 from the exact immutable bytes; callers need not supply it.
[[nodiscard]] SharedSdfAtlasPage CreateSdfAtlasPage(
    Core::Alloc::GlobalArena& arena,
    const SdfAtlasPageBinding& binding,
    SdfAtlasPage::Pixels&& pixels
);
[[nodiscard]] SharedSdfAtlasPage CreateSdfAtlasPage(
    Core::Alloc::GlobalArena& arena,
    const FontAtlas& atlas,
    u64 fontGeneration,
    u64 atlasIdentity,
    u32 groupIndex
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

