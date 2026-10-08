// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/module.h>

#include <global/filesystem.h>
#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PreparedFontImageView{
    u32 width = 0u;
    u32 height = 0u;
    u32 channelCount = 0u;
    BinaryByteView pixels;
};

struct PreparedFontImageGroup{
    u32 width = 0u;
    u32 height = 0u;
    u32 channelCount = 0u;
    Sha256Digest sha256;
    Core::Assets::AssetBytes pixels;


    explicit PreparedFontImageGroup(Core::Assets::AssetArena& arena)
        : pixels(arena)
    {}
};

struct PreparedFontSource{
    Core::Assets::AssetBytes fontBytes;
    Core::Assets::AssetVector<PreparedFontImageGroup> groups;
    u32 faceIndex = 0u;
    Sha256Digest fontSha256;


    explicit PreparedFontSource(Core::Assets::AssetArena& arena)
        : fontBytes(arena)
        , groups(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// FON2 stores the exact prepared SFNT and compact images; readable atlas mappings remain in paired .nwb metadata.
[[nodiscard]] Expected<Core::Assets::AssetBytes, AStringView> SerializePreparedFontSource(
    BinaryByteView sfnt,
    u32 faceIndex,
    const PreparedFontImageView* groups,
    usize groupCount,
    Core::Assets::AssetArena& arena
);

// Font-only reads retain image directory metadata, with empty pixel arrays, and validate the complete file layout.
[[nodiscard]] Expected<PreparedFontSource, AStringView> ReadPreparedFontSource(const Path& path, Core::Assets::AssetArena& arena, bool includePixels);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

