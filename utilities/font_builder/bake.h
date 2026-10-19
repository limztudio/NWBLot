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

[[nodiscard]] bool ValidateOptions(const BakeOptions& options, Core::Alloc::ScratchArena& scratch);
// Options must already pass ValidateOptions.
[[nodiscard]] Expected<Impl::FontAtlasPayload> Bake(const BakeOptions& options, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratch);
[[nodiscard]] bool PackGlyphs(const BakeOptions& options, const RasterGlyphs& glyphs, Impl::FontAtlasPayload& outPayload, Core::Alloc::ScratchArena& scratch);
[[nodiscard]] bool WriteOutputs(const BakeOptions& options, const Impl::FontAtlasPayload& payload, Core::Alloc::ScratchArena& scratch);
int Run(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

