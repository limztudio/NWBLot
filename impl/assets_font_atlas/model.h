// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../assets_font/asset.h"

#include <core/assets/ref.h>

#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_FontAtlasMaxGlyphCount = 65535u;
inline constexpr u32 s_FontAtlasMaxGroupCount = 8u;
inline constexpr u32 s_FontAtlasMaxExtent = 2048u;
inline constexpr u32 s_FontAtlasMaxPixelBytes = 128u * 1024u * 1024u;
inline constexpr u32 s_FontAtlasMinBakePpem = 16u;
inline constexpr u32 s_FontAtlasMaxBakePpem = 256u;
inline constexpr u32 s_FontAtlasMinSpreadPixels = 2u;
inline constexpr u32 s_FontAtlasMaxSpreadPixels = 32u;
inline constexpr u32 s_FontAtlasDistanceEncoding = 1u;
inline constexpr u32 s_FontAtlasMaxPositioningBytes = 32u * 1024u * 1024u;
inline constexpr u32 s_FontAtlasKernTag = 0x6b65726eu;
inline constexpr u32 s_FontAtlasGposTag = 0x47504f53u;
inline constexpr u32 s_FontAtlasGdefTag = 0x47444546u;

namespace FontAtlasRasterMode{
    enum Enum : u32{
        Outline = 0u,
        Bitmap = 1u,
    };
};

struct FontAtlasGlyph{
    u32 glyphId = 0u;
    u32 group = 0u;
    u32 channel = 0u;
    u32 x = 0u;
    u32 y = 0u;
    u32 width = 0u;
    u32 height = 0u;
    f32 planeLeft = 0.f;
    f32 planeTop = 0.f;
    f32 planeRight = 0.f;
    f32 planeBottom = 0.f;
    f32 advanceUnits = 0.f;
    u32 drawable = 0u;
};

struct FontAtlasGroup{
    u32 width = 0u;
    u32 height = 0u;
    u32 channelCount = 4u;
    Sha256Digest sha256;
    Core::Assets::AssetBytes pixels;


    explicit FontAtlasGroup(Core::Assets::AssetArena& arena)noexcept
        : pixels(arena)
    {}
};

// Exact source positioning tables preserve GPOS classes, script/language selection and lookup order without expansion.
struct FontAtlasPositioningTable{
    u32 tag = 0u;
    Sha256Digest sha256;
    Core::Assets::AssetBytes bytes;


    explicit FontAtlasPositioningTable(Core::Assets::AssetArena& arena)noexcept
        : bytes(arena)
    {}
};

struct FontAtlasPayload{
    Core::Assets::AssetRef<Font> font;
    Sha256Digest fontSha256;
    u32 faceIndex = 0u;
    u32 unitsPerEm = 0u;
    u32 sourceGlyphCount = 0u;
    u32 bakePpem = 64u;
    u32 spreadPixels = 8u;
    u32 guardTexels = 1u;
    FontAtlasRasterMode::Enum rasterMode = FontAtlasRasterMode::Bitmap;
    f32 ascenderUnits = 0.f;
    f32 descenderUnits = 0.f;
    f32 lineGapUnits = 0.f;
    Core::Assets::AssetVector<FontAtlasGlyph> glyphs;
    Core::Assets::AssetVector<FontAtlasGroup> groups;
    Core::Assets::AssetVector<FontAtlasPositioningTable> positioningTables;


    explicit FontAtlasPayload(Core::Assets::AssetArena& arena)noexcept
        : glyphs(arena)
        , groups(arena)
        , positioningTables(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidateFontAtlasPayload(const FontAtlasPayload& payload);
[[nodiscard]] bool ValidateFontAtlasSourceMatch(const FontAtlasPayload& payload, const Font& font);
[[nodiscard]] Expected<Core::Assets::AssetVector<FontAtlasPositioningTable>> CopyFontAtlasPositioningTables(
    const Font& font,
    u32 sourceGlyphCount,
    Core::Assets::AssetArena& arena
);
[[nodiscard]] bool ValidateFontAtlasPositioningTable(const FontAtlasPositioningTable& table, u32 glyphCount);
[[nodiscard]] Expected<Core::Assets::AssetBytes> SerializeFontAtlasPayload(
    const FontAtlasPayload& payload,
    Core::Assets::AssetArena& arena
);
[[nodiscard]] Expected<FontAtlasPayload> DeserializeFontAtlasPayload(
    const Core::Assets::AssetBytes& binary,
    Core::Assets::AssetArena& arena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

