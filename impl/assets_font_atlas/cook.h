// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/alloc/scratch.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FontAtlasCookEntry{
    FontAtlasPayload payload;
    Name virtualPath = s_NameNone;


public:
    explicit FontAtlasCookEntry(Core::Assets::AssetArena& memoryArena)noexcept
        : payload(memoryArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<u32> ReadFontAtlasMetadataU32(const Path& path, const Core::Metascript::Value& map, AStringView diagnosticPrefix, AStringView field, u32 minimum, u32 maximum);
[[nodiscard]] Expected<Core::Assets::AssetVector<FontAtlasGlyph>> ParseFontAtlasGlyphMetadata(const Path& nwbFilePath, const Core::Metascript::Value& asset, const FontAtlasPayload& payload);
[[nodiscard]] Expected<FontAtlasCookEntry> ParseFontAtlasCookMetadata(const Path& assetRoot, AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<FontAtlasCookEntry> ParseFontAtlasCookMetadataValue(Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<FontAtlas> BuildFontAtlasAsset(const FontAtlasCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<FontAtlas> BuildFontAtlasAsset(FontAtlasCookEntry&& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

