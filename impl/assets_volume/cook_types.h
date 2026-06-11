// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_entry_registry.h"

#include <core/alloc/arena_object.h>
#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_IncludeAssetTypeName("include");
inline constexpr AStringView s_AssetVolumeName = "graphics";
inline constexpr AStringView s_AssetVolumeCookerLogPrefix = "AssetVolumeCooker";

using ScratchArena = Core::Alloc::ScratchArena;
using ScratchString = AString<ScratchArena>;
using StagedVolumePaths = StagedDirectoryPaths;
using VirtualPathHashSet = CookEntryPathHashSet;
using ParsedMetadataExtensionMap = CookMap<Name, void*>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResolvedCookPaths{
    Path repoRoot;
    CookVector<Path> assetRoots;
    Path outputDirectory;
    Path cacheDirectory;

    explicit ResolvedCookPaths(CookArena& arena)
        : assetRoots(arena)
    {}
};

struct DiscoveredNwbFile{
    Path assetRoot;
    Path filePath;
    CookString normalizedPathText;
    ACompactString virtualRoot;

    DiscoveredNwbFile(CookArena& arena, const Path& inAssetRoot, const Path& inFilePath, AStringView inNormalizedPathText, ACompactString inVirtualRoot)
        : assetRoot(inAssetRoot)
        , filePath(inFilePath)
        , normalizedPathText(inNormalizedPathText, arena)
        , virtualRoot(inVirtualRoot)
    {}
};

using DiscoveredNwbFileVector = CookVector<DiscoveredNwbFile>;
using DiscoveredBindFileVector = CookVector<DiscoveredNwbFile>;

struct ParsedAssetMetadata{
    CookArena& arena;
    CookEntryRegistry entryRegistry;
    ParsedMetadataExtensionMap extensions;

    explicit ParsedAssetMetadata(CookArena& arena)
        : arena(arena)
        , entryRegistry(arena)
        , extensions(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
};

template<typename T>
[[nodiscard]] T* FindParsedMetadataExtension(ParsedAssetMetadata& metadata, const Name& extensionName){
    auto found = metadata.extensions.find(extensionName);
    return found == metadata.extensions.end() ? nullptr : static_cast<T*>(found.value());
}

template<typename T>
[[nodiscard]] const T* FindParsedMetadataExtension(const ParsedAssetMetadata& metadata, const Name& extensionName){
    auto found = metadata.extensions.find(extensionName);
    return found == metadata.extensions.end() ? nullptr : static_cast<const T*>(found.value());
}

template<typename T, typename... Args>
[[nodiscard]] T& RequireParsedMetadataExtension(ParsedAssetMetadata& metadata, const Name& extensionName, Args&&... args){
    if(T* existing = FindParsedMetadataExtension<T>(metadata, extensionName))
        return *existing;

    T* created = Core::Alloc::NewArenaObject<T>(metadata.arena, Forward<Args>(args)...);
    metadata.extensions.emplace(extensionName, created);
    return *created;
}

struct AssetVolumeWriteResult{
    ACompactString volumeName;
    u64 fileCount = 0;
    u64 segmentCount = 0;
};

using AssetVolumeExternalWriter = Function<bool(Core::Filesystem::VolumeSession&, VirtualPathHashSet&, ScratchArena&)>;
using AssetVolumeExternalWriterVector = CookVector<AssetVolumeExternalWriter>;

[[nodiscard]] bool AddPlannedFileCount(u64 additionalFileCount, u64& inOutPlannedFileCount);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

