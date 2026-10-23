// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_entry_registry.h"
#include "expanded_metadata.h"
#include "metadata_extension.h"

#include <core/assets/paths.h>
#include <core/task/cpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ParsedMetadataExtensionMap = CookMap<Name, ParsedMetadataExtension>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResolvedAssetRoot{
    Path path;
    ACompactString virtualRoot;


public:
    explicit ResolvedAssetRoot(CookArena& arena)noexcept
        : path(arena)
    {}
    ResolvedAssetRoot(Path&& inPath, const ACompactString& inVirtualRoot)noexcept
        : path(Move(inPath))
        , virtualRoot(inVirtualRoot)
    {}
};

struct DiscoveredNwbFile{
    Path assetRoot;
    Path filePath;
    CookString normalizedPathText;
    ACompactString virtualRoot;


public:
    template<typename FileArenaT>
    DiscoveredNwbFile(CookArena& arena, const Path& inAssetRoot, const ::Path<FileArenaT>& inFilePath, AStringView inNormalizedPathText, const ACompactString& inVirtualRoot)
        : assetRoot(arena, inAssetRoot)
        , filePath(arena, inFilePath)
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


public:
    explicit ParsedAssetMetadata(CookArena& arena)
        : arena(arena)
        , entryRegistry(arena)
        , extensions(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
};

template<typename T>
[[nodiscard]] T* FindParsedMetadataExtension(ParsedAssetMetadata& metadata, const Name& extensionName)noexcept{
    auto found = metadata.extensions.find(extensionName);
    return found == metadata.extensions.end() ? nullptr : static_cast<T*>(found.value().get());
}

template<typename T>
[[nodiscard]] const T* FindParsedMetadataExtension(const ParsedAssetMetadata& metadata, const Name& extensionName)noexcept{
    auto found = metadata.extensions.find(extensionName);
    return found == metadata.extensions.end() ? nullptr : static_cast<const T*>(found.value().get());
}

template<typename T, typename... Args>
[[nodiscard]] T& RequireParsedMetadataExtension(ParsedAssetMetadata& metadata, const Name& extensionName, Args&&... args){
    if(T* existing = FindParsedMetadataExtension<T>(metadata, extensionName))
        return *existing;

    ParsedMetadataExtension pending = MakeParsedMetadataExtension<T>(metadata.arena, Forward<Args>(args)...);
    auto inserted = metadata.extensions.try_emplace(extensionName, Move(pending));
    if(!inserted.second && !inserted.first.value())
        inserted.first.value() = Move(pending);
    return *static_cast<T*>(inserted.first.value().get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetMetadataParseResult{
    enum Enum : u8{
        Unsupported,
        Parsed,
        Error
    };
};

struct AssetDocumentMetadataParseContext{
    CookArena& cookArena;
    const DiscoveredNwbFile& discoveredNwbFile;
    Name assetType = s_NameNone;
    const Core::Metascript::Document& doc;
    ParsedAssetMetadata& parsedMetadata;
    ScratchArena& scratchArena;
};

struct AssetValueMetadataParseContext{
    CookArena& cookArena;
    const DiscoveredNwbFile& discoveredNwbFile;
    Name assetType = s_NameNone;
    Name virtualPath = s_NameNone;
    const Core::Metascript::Value& value;
    ParsedAssetMetadata& parsedMetadata;
    ScratchArena& scratchArena;
};

using AssetDocumentMetadataParseFunction = AssetMetadataParseResult::Enum (*)(AssetDocumentMetadataParseContext& context);
using AssetValueMetadataParseFunction = AssetMetadataParseResult::Enum (*)(AssetValueMetadataParseContext& context);

class AssetMetadataParserAutoRegistrar final{
public:
    AssetMetadataParserAutoRegistrar(AssetDocumentMetadataParseFunction documentFunction, AssetValueMetadataParseFunction valueFunction);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetBunchExpandFailure{
    enum Enum : u8{
        Unsupported,
        Error
    };
};

struct AssetBunchExpandContext{
    const Path& assetRoot;
    AStringView virtualRoot;
    const Path& nwbFilePath;
    const Core::Metascript::Document& doc;
    ScratchArena& scratchArena;
};

using AssetBunchExpandFunction = Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> (*)(const AssetBunchExpandContext& context);

class AssetBunchExpanderAutoRegistrar final{
public:
    explicit AssetBunchExpanderAutoRegistrar(AssetBunchExpandFunction function);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<DiscoveredNwbFileVector> DiscoverFilesWithExtension(CookArena& cookArena, const CookVector<ResolvedAssetRoot>& assetRoots, AStringView expectedExtension, ScratchArena& scratchArena);
[[nodiscard]] bool AddPlannedFileCount(u64 additionalFileCount, u64& inOutPlannedFileCount);
[[nodiscard]] AssetMetadataParseResult::Enum TryAutoCollectedDocumentMetadataParsers(AssetDocumentMetadataParseContext& context);
[[nodiscard]] AssetMetadataParseResult::Enum TryAutoCollectedValueMetadataParsers(AssetValueMetadataParseContext& context);
[[nodiscard]] Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> TryAutoCollectedAssetBunchExpanders(const AssetBunchExpandContext& context);
[[nodiscard]] bool ParseAssetMetadata(CookArena& cookArena, const DiscoveredNwbFileVector& nwbFiles, Core::CpuTaskScheduler& cpuScheduler, ScratchArena& scratchArena, ParsedAssetMetadata& outMetadata);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

