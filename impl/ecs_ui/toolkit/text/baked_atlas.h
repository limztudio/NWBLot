// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "sdf_page.h"

#include <impl/assets_font_atlas/asset.h>

#include <global/atomic_identity.h>
#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This version copies only rendering metadata and image bytes. FontFace keeps it with its exact shaping source.
class BakedFontAtlas : NoCopy{
private:
    [[nodiscard]] static u64 NextIdentity()noexcept{
        static Atomic<u64> s_NextIdentity{ 1u };
        return NextNonWrappingIdentity(s_NextIdentity);
    }


public:
    BakedFontAtlas(Core::Alloc::GlobalArena& arena, const FontAtlas& atlas, const u64 fontGeneration)
        : m_glyphs(arena)
        , m_pages(arena)
    {
        const FontAtlasPayload& payload = atlas.payload();
        m_unitsPerEm = payload.unitsPerEm;
        m_bakePpem = payload.bakePpem;
        m_glyphs.assign(payload.glyphs.begin(), payload.glyphs.end());
        m_pages.reserve(payload.groups.size());
        const u64 identity = NextIdentity();
        for(u32 group = 0u; group < payload.groups.size(); ++group){
            auto page = CreateSdfAtlasPage(arena, atlas, fontGeneration, identity, group);
            if(!page)
                return;
            m_pages.push_back(Move(*page));
        }
        m_ready = true;
    }


public:
    [[nodiscard]] b8 valid()const noexcept{ return m_ready; }

    [[nodiscard]] u32 unitsPerEm()const noexcept{ return m_unitsPerEm; }

    [[nodiscard]] u32 bakePpem()const noexcept{ return m_bakePpem; }

    [[nodiscard]] const FontAtlasGlyph* glyph(const u32 glyphId)const noexcept{ return glyphId < m_glyphs.size() ? &m_glyphs[glyphId] : nullptr; }
    [[nodiscard]] const SharedSdfAtlasPage& page(const u32 group)const noexcept{
        NWB_FATAL_ASSERT(group < m_pages.size());
        return m_pages[group];
    }


private:
    Vector<FontAtlasGlyph, Core::Alloc::GlobalArena> m_glyphs;
    Vector<SharedSdfAtlasPage, Core::Alloc::GlobalArena> m_pages;
    u32 m_unitsPerEm = 0u;
    u32 m_bakePpem = 0u;
    b8 m_ready = false;
};

using SharedBakedFontAtlas = RefCountPtr<RefCounter<BakedFontAtlas>, ArenaRefDeleter<RefCounter<BakedFontAtlas>, Core::Alloc::GlobalArena>>;

[[nodiscard]] inline Expected<SharedBakedFontAtlas> CreateBakedFontAtlas(Core::Alloc::GlobalArena& arena, const FontAtlas& atlas, const u64 fontGeneration){
    auto* const atlasObject = NewArenaObject<RefCounter<BakedFontAtlas>>(arena, arena, atlas, fontGeneration);
    if(!atlasObject)
        return MakeUnexpected(Failure{});
    SharedBakedFontAtlas result(atlasObject, ArenaRefDeleter<RefCounter<BakedFontAtlas>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
    if(!result->valid())
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

