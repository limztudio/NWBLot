// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../assets_font/asset.h"
#include <impl/assets_font_atlas/types.adhd.h>
#include <impl/assets_font_atlas/positioning_validation.adhd.h>

#include <core/alloc/scratch.h>
#include <core/assets/ref.h>

#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_FontAtlasValidatePayloadContext = NWB_TEXT("FontAtlas::validatePayload");

struct FontAtlasGroup{
    u32 width = 0u;
    u32 height = 0u;
    u32 channelCount = s_FontAtlasMaxChannelCount;
    Sha256Digest sha256;
    Core::Assets::AssetBytes pixels;


public:
    explicit FontAtlasGroup(Core::Assets::AssetArena& arena)noexcept
        : pixels(arena)
    {}
};

// Exact source positioning tables preserve GPOS classes, script/language selection and lookup order without expansion.
struct FontAtlasPositioningTable{
    u32 tag = 0u;
    Sha256Digest sha256;
    Core::Assets::AssetBytes bytes;


public:
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
    u32 bakePpem = s_FontAtlasDefaultBakePpem;
    u32 spreadPixels = s_FontAtlasDefaultSpreadPixels;
    u32 guardTexels = s_FontAtlasDefaultGuardTexels;
    FontAtlasRasterMode::Enum rasterMode = FontAtlasRasterMode::Bitmap;
    f32 ascenderUnits = 0.f;
    f32 descenderUnits = 0.f;
    f32 lineGapUnits = 0.f;
    Core::Assets::AssetVector<FontAtlasGlyph> glyphs;
    Core::Assets::AssetVector<FontAtlasGroup> groups;
    Core::Assets::AssetVector<FontAtlasPositioningTable> positioningTables;


public:
    explicit FontAtlasPayload(Core::Assets::AssetArena& arena)noexcept
        : glyphs(arena)
        , groups(arena)
        , positioningTables(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 ValidateFontAtlasPayload(const FontAtlasPayload& payload, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] b8 ValidateFontAtlasSourceMatch(const FontAtlasPayload& payload, const Font& font);
[[nodiscard]] Expected<Core::Assets::AssetVector<FontAtlasPositioningTable>> CopyFontAtlasPositioningTables(const Font& font, u32 sourceGlyphCount, Core::Assets::AssetArena& arena);
[[nodiscard]] inline b8 ValidateFontAtlasPositioningTable(const FontAtlasPositioningTable& table, const u32 glyphCount)noexcept{
    return ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, glyphCount);
}
[[nodiscard]] Expected<Core::Assets::AssetBytes> SerializeFontAtlasPayload(const FontAtlasPayload& payload, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<FontAtlasPayload> DeserializeFontAtlasPayload(Span<const u8> binary, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

