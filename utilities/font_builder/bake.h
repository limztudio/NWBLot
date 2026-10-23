// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <impl/assets_font_atlas/model.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_MetadataFileExtension = ".nwb";
inline constexpr AStringView s_PreparedFontExtension = ".font";
inline constexpr AStringView s_TrueTypeExtension = ".ttf";
inline constexpr AStringView s_OpenTypeExtension = ".otf";
inline constexpr AStringView s_RasterModeBitmapText = "bitmap";
inline constexpr AStringView s_RasterModeOutlineText = "outline";
// One guard pixel on each side keeps neighbouring glyphs from bleeding into each other.
inline constexpr u32 s_GlyphGuardExtent = 2u;


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
[[nodiscard]] bool PackGlyphs(const BakeOptions& options, const RasterGlyphs& glyphs, Core::Alloc::ScratchArena& scratch, Impl::FontAtlasPayload& outPayload);
[[nodiscard]] bool WriteOutputs(const BakeOptions& options, const Impl::FontAtlasPayload& payload, Core::Alloc::ScratchArena& scratch);
int Run(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

