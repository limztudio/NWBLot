// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <impl/assets_font_atlas/model.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_PreparedFontExtension = ".font";
inline constexpr AStringView s_TrueTypeExtension = ".ttf";
inline constexpr AStringView s_OpenTypeExtension = ".otf";
inline constexpr AStringView s_RasterModeBitmapText = "bitmap";
inline constexpr AStringView s_RasterModeOutlineText = "outline";
// One guard pixel on each side keeps neighbouring glyphs from bleeding into each other.
inline constexpr u32 s_GlyphGuardExtent = 2u;


inline constexpr u32 s_DefaultBakeExtent = 1024u;

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

[[nodiscard]] b8 ValidateOptions(const BakeOptions& options);
// Options must already pass ValidateOptions.
[[nodiscard]] Expected<Impl::FontAtlasPayload> Bake(const BakeOptions& options, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratch);
[[nodiscard]] b8 PackGlyphs(const BakeOptions& options, Span<const RasterGlyph> glyphs, Core::Alloc::ScratchArena& scratch, Impl::FontAtlasPayload& outPayload);
[[nodiscard]] b8 WriteOutputs(const BakeOptions& options, const Impl::FontAtlasPayload& payload, Core::Alloc::ScratchArena& scratch);
int Run(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

