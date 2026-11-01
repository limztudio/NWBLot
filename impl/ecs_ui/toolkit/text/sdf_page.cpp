// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sdf_page.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SharedSdfAtlasPage> CreateSdfAtlasPage(Core::Alloc::GlobalArena& arena, const SdfAtlasPageBinding& binding, SdfAtlasPage::Pixels&& pixels)noexcept{
    if(!ValidSdfAtlasPageBinding(binding, pixels.size()))
        return MakeUnexpected(Failure{});
    SdfAtlasPageBinding immutable = binding;
    immutable.pixelsSha256 = ComputeSha256(BinaryByteView(pixels.data(), pixels.size()));
    auto* const page = NewArenaObject<RefCounter<SdfAtlasPage>>(arena, immutable, Move(pixels));
    if(!page)
        return MakeUnexpected(Failure{});
    return SharedSdfAtlasPage(page, ArenaRefDeleter<RefCounter<SdfAtlasPage>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}

Expected<SharedSdfAtlasPage> CreateSdfAtlasPage(Core::Alloc::GlobalArena& arena, const FontAtlas& atlas, const u64 fontGeneration, const u64 atlasIdentity, const u32 groupIndex){
    // The asset codec/font installation validates the whole payload once before publishing matching glyph metadata.
    const FontAtlasPayload& payload = atlas.payload();
    if(groupIndex >= payload.groups.size())
        return MakeUnexpected(Failure{});
    const FontAtlasGroup& group = payload.groups[groupIndex];
    const SdfAtlasPageBinding binding{
        .font = payload.font,
        .fontSha256 = payload.fontSha256,
        .pixelsSha256 = {},
        .fontGeneration = fontGeneration,
        .atlasIdentity = atlasIdentity,
        .generation = 1u,
        .index = groupIndex,
        .width = group.width,
        .height = group.height,
        .channelCount = group.channelCount,
        .spreadPixels = payload.spreadPixels,
    };
    SdfAtlasPage::Pixels pixels(arena);
    pixels.assign(group.pixels.begin(), group.pixels.end());
    return CreateSdfAtlasPage(arena, binding, Move(pixels));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

