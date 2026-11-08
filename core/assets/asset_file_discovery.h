// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


#pragma once


#include "paths.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


struct DiscoveredNwbFile{
    Path assetRoot;
    Path filePath;
    CookString normalizedPathText;
    ACompactString virtualRoot;


public:
    template<typename FileArenaT>
    DiscoveredNwbFile(CookArena& arena, const Path& inAssetRoot, const ::Path<FileArenaT>& inFilePath, AStringView inNormalizedPathText, AStringView inVirtualRoot)
        : assetRoot(arena, inAssetRoot)
        , filePath(arena, inFilePath)
        , normalizedPathText(inNormalizedPathText, arena)
        , virtualRoot(inVirtualRoot)
    {}
};

using DiscoveredNwbFileVector = CookVector<DiscoveredNwbFile>;


class AssetFileDiscovery{
public:
    [[nodiscard]] static Expected<DiscoveredNwbFileVector> Discover(Span<const ResolvedAssetRoot> assetRoots, AStringView expectedExtension, CookArena& cookArena, ScratchArena& scratchArena);
};


NWB_ASSETS_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
