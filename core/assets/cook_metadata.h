// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_entry_registry.h"

#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ParsedMetadataExtensionMap = CookMap<Name, void*>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResolvedAssetRoot{
    Path path;
    ACompactString virtualRoot;

    explicit ResolvedAssetRoot(CookArena& arena)
        : path(arena)
    {}

    ResolvedAssetRoot(Path&& inPath, const ACompactString& inVirtualRoot)
        : path(Move(inPath))
        , virtualRoot(inVirtualRoot)
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
    Name assetType = NAME_NONE;
    const Core::Metascript::Document& doc;
    ParsedAssetMetadata& parsedMetadata;
    ScratchArena& scratchArena;
};

struct AssetValueMetadataParseContext{
    CookArena& cookArena;
    const DiscoveredNwbFile& discoveredNwbFile;
    Name assetType = NAME_NONE;
    Name virtualPath = NAME_NONE;
    const Core::Metascript::Value& value;
    ParsedAssetMetadata& parsedMetadata;
    ScratchArena& scratchArena;
};

using AssetDocumentMetadataParseFunction = AssetMetadataParseResult::Enum (*)(AssetDocumentMetadataParseContext& context);
using AssetValueMetadataParseFunction = AssetMetadataParseResult::Enum (*)(AssetValueMetadataParseContext& context);

class AssetMetadataParserAutoRegistrar final{
public:
    AssetMetadataParserAutoRegistrar(
        AssetDocumentMetadataParseFunction documentFunction,
        AssetValueMetadataParseFunction valueFunction
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ExpandedAssetMetadata{
    Name assetType = NAME_NONE;
    Name virtualPath = NAME_NONE;
    const Core::Metascript::Value* value = nullptr;
};

using ExpandedAssetMetadataVector = Vector<ExpandedAssetMetadata, ScratchArena>;

namespace AssetBunchExpandResult{
enum Enum : u8{
    Unsupported,
    Parsed,
    Error
};
};

struct AssetBunchExpandContext{
    const Path& assetRoot;
    AStringView virtualRoot;
    const Path& nwbFilePath;
    const Core::Metascript::Document& doc;
    ExpandedAssetMetadataVector& outAssets;
    ScratchArena& scratchArena;
};

using AssetBunchExpandFunction = AssetBunchExpandResult::Enum (*)(AssetBunchExpandContext& context);

class AssetBunchExpanderAutoRegistrar final{
public:
    explicit AssetBunchExpanderAutoRegistrar(AssetBunchExpandFunction function);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool DiscoverFilesWithExtension(
    const CookVector<ResolvedAssetRoot>& assetRoots,
    AStringView expectedExtension,
    DiscoveredNwbFileVector& outFiles,
    ScratchArena& scratchArena
);
[[nodiscard]] bool AddPlannedFileCount(u64 additionalFileCount, u64& inOutPlannedFileCount);
[[nodiscard]] AssetMetadataParseResult::Enum TryAutoCollectedDocumentMetadataParsers(AssetDocumentMetadataParseContext& context);
[[nodiscard]] AssetMetadataParseResult::Enum TryAutoCollectedValueMetadataParsers(AssetValueMetadataParseContext& context);
[[nodiscard]] AssetBunchExpandResult::Enum TryAutoCollectedAssetBunchExpanders(AssetBunchExpandContext& context);
[[nodiscard]] bool ParseAssetMetadata(
    CookArena& cookArena,
    const DiscoveredNwbFileVector& nwbFiles,
    ParsedAssetMetadata& outMetadata,
    Core::Alloc::ThreadPool& threadPool,
    ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

