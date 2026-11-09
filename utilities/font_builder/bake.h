// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <utilities/font_builder/bake_types.adhd.h>
#include <utilities/font_builder/font_atlas_packer.adhd.h>

#include <impl/assets_font_atlas/model.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BakeOptions{
    Path source;
    Path output;
    u32 ppem = Impl::s_FontAtlasDefaultBakePpem;
    u32 spread = Impl::s_FontAtlasDefaultSpreadPixels;
    u32 extent = s_DefaultBakeExtent;
    u32 maxGroups = Impl::s_FontAtlasMaxGroupCount;
    b8 outline = false;
    b8 overwrite = false;


public:
    explicit BakeOptions(Core::Alloc::GlobalArena& arena)noexcept
        : source(arena)
        , output(arena)
    {}
};

struct RasterGlyph{
    Impl::FontAtlasGlyph record;
    Core::Assets::AssetBytes pixels;


public:
    explicit RasterGlyph(Core::Assets::AssetArena& arena)noexcept
        : pixels(arena)
    {}
};

using RasterGlyphs = Core::Assets::AssetVector<RasterGlyph>;
using MetadataString = ::AString<Core::Alloc::ScratchArena>;

[[nodiscard]] b8 ValidateOptions(const BakeOptions& options);
// Options must already pass ValidateOptions.
[[nodiscard]] Expected<Impl::FontAtlasPayload> Bake(const BakeOptions& options, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratch, Core::Assets::AssetBytes& outSourceFontBytes);
[[nodiscard]] b8 PackGlyphs(const BakeOptions& options, Span<const RasterGlyph> glyphs, Core::Alloc::ScratchArena& scratch, Impl::FontAtlasPayload& outPayload);
inline void CompactAtlasGroups(Impl::FontAtlasPayload& payload){
    for(usize groupIndex = 0u; groupIndex < payload.groups.size(); ++groupIndex){
        auto& group = payload.groups[groupIndex];
        const Impl::FontAtlasGroupCompactBounds bounds = Impl::ComputeFontAtlasGroupCompactBounds(
            payload.glyphs,
            static_cast<u32>(groupIndex),
            group.width,
            group.height,
            group.channelCount,
            payload.guardTexels
        );
        if(bounds.unchanged)
            continue;

        Core::Assets::AssetBytes compactPixels(group.pixels.get_allocator().arena());
        FontAtlasPacker{}.compactGroupPixels(bounds, group.width, group.channelCount, group.pixels, compactPixels);
        for(auto& glyph : payload.glyphs)
            Impl::OffsetFontAtlasGroupGlyph(static_cast<u32>(groupIndex), bounds.firstX, bounds.firstY, glyph);
        group.width = bounds.width;
        group.height = bounds.height;
        group.channelCount = bounds.channelCount;
        group.pixels = Move(compactPixels);
    }
}
void BuildFontMetadata(const Impl::FontAtlasPayload& payload, MetadataString& outText);
[[nodiscard]] Expected<Core::Assets::AssetBytes, AStringView> BuildPreparedFont(Span<const u8> sourceFontBytes, const Impl::FontAtlasPayload& atlas, Core::Alloc::ScratchArena& scratch);
[[nodiscard]] Expected<Core::Assets::AssetBytes, AStringView> ReadFontSourceInput(const Path& path, Core::Assets::AssetArena& arena);
[[nodiscard]] b8 WriteOutputs(const BakeOptions& options, Span<const u8> sourceFontBytes, const Impl::FontAtlasPayload& payload, Core::Alloc::ScratchArena& scratch);
int Run(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

