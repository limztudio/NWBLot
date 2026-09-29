// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <impl/assets_font_atlas/model.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BakeOptions{
    Path source;
    Path output;
    u32 ppem = 64u;
    u32 spread = 8u;
    u32 extent = 1024u;
    u32 maxGroups = 8u;
    bool outline = false;
    bool overwrite = false;

    explicit BakeOptions(Core::Alloc::GlobalArena& arena)
        : source(arena)
        , output(arena)
    {}
};

struct RasterGlyph{
    Impl::FontAtlasGlyph record;
    Core::Assets::AssetBytes pixels;

    explicit RasterGlyph(Core::Assets::AssetArena& arena)
        : pixels(arena)
    {}
};

using RasterGlyphs = Core::Assets::AssetVector<RasterGlyph>;

[[nodiscard]] bool ValidateOptions(const BakeOptions& options);
[[nodiscard]] bool Bake(const BakeOptions& options, Impl::FontAtlasPayload& outPayload, Core::Alloc::ScratchArena& scratch);
[[nodiscard]] bool PackGlyphs(const BakeOptions& options, const RasterGlyphs& glyphs, Impl::FontAtlasPayload& outPayload, Core::Alloc::ScratchArena& scratch);
[[nodiscard]] bool WriteOutputs(const BakeOptions& options, const Impl::FontAtlasPayload& payload);
int Run(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

