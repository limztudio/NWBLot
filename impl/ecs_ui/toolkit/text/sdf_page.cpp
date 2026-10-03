// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sdf_page.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool operator==(const SdfAtlasPageBinding& lhs, const SdfAtlasPageBinding& rhs)noexcept{
    return
        lhs.font == rhs.font && lhs.fontSha256 == rhs.fontSha256 && lhs.pixelsSha256 == rhs.pixelsSha256
        && lhs.fontGeneration == rhs.fontGeneration && lhs.atlasIdentity == rhs.atlasIdentity && lhs.generation == rhs.generation
        && lhs.index == rhs.index && lhs.width == rhs.width && lhs.height == rhs.height
        && lhs.channelCount == rhs.channelCount
        && lhs.spreadPixels == rhs.spreadPixels && lhs.distanceEncoding == rhs.distanceEncoding
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SdfAtlasPage::SdfAtlasPage(Core::Alloc::GlobalArena& arena, const SdfAtlasPageBinding& binding, Pixels&& pixels)
    : m_binding(binding)
    , m_pixels(arena)
{
    m_pixels = Move(pixels);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SharedSdfAtlasPage CreateSdfAtlasPage(
    Core::Alloc::GlobalArena& arena,
    const SdfAtlasPageBinding& binding,
    SdfAtlasPage::Pixels&& pixels){
    if(
        !binding.font.valid() || binding.fontGeneration == 0u || binding.atlasIdentity == 0u || binding.generation == 0u
        || binding.index >= s_FontAtlasMaxGroupCount || binding.width == 0u || binding.height == 0u
        || binding.width > s_FontAtlasMaxExtent || binding.height > s_FontAtlasMaxExtent
        || binding.channelCount == 0u || binding.channelCount > 4u
        || binding.spreadPixels < s_FontAtlasMinSpreadPixels || binding.spreadPixels > s_FontAtlasMaxSpreadPixels
        || binding.distanceEncoding != s_SdfDistanceEncodingFreeTypeU8
        || pixels.size() != static_cast<usize>(binding.width) * binding.height * binding.channelCount
    )
        return {};
    SdfAtlasPageBinding immutable = binding;
    immutable.pixelsSha256 = ComputeSha256(BinaryByteView(pixels.data(), pixels.size()));
    return
        SharedSdfAtlasPage(
            NewArenaObject<RefCounter<SdfAtlasPage>>(arena, arena, immutable, Move(pixels)),
            ArenaRefDeleter<RefCounter<SdfAtlasPage>, Core::Alloc::GlobalArena>(&arena),
            AdoptRef
        )
    ;
}

SharedSdfAtlasPage CreateSdfAtlasPage(
    Core::Alloc::GlobalArena& arena,
    const FontAtlas& atlas,
    const u64 fontGeneration,
    const u64 atlasIdentity,
    const u32 groupIndex){
    // The asset codec/font installation validates the whole payload once before publishing matching glyph metadata.
    const FontAtlasPayload& payload = atlas.payload();
    if(groupIndex >= payload.groups.size())
        return {};
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

