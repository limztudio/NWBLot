// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset_file_discovery.h"
#include "cook_dependency_registry.h"
#include "cook_entry_registry.h"
#include "expanded_metadata.h"
#include "metadata_extension.h"
#include "metadata_fields.h"

#include <core/assets/paths.h>
#include <core/task/cpu/scheduler.h>

#include <global/sha256.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ParsedMetadataExtensionMap = CookMap<Name, ParsedMetadataExtension>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename EntryT>
using ParseMetadataValueFn = Expected<EntryT>(*)(Name, const Path&, const Core::Metascript::Value&, AssetArena&, Alloc::ScratchArena&);

template<typename MetadataValue>
[[nodiscard]] inline Expected<b8> ParseMetadataBoolField(
    const Path& nwbFilePath,
    const AStringView diagnosticPrefix,
    const MetadataValue& value,
    const AStringView fieldLabel,
    const AStringView errorDetailText = "must be an integer boolean (0 or 1)"
){
    if(!value.isInteger()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' {}"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(errorDetailText));
        return MakeUnexpected(Failure{});
    }
    const i64 integerValue = value.asInteger();
    if(integerValue != 0 && integerValue != 1){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' must be 0 or 1"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    return integerValue != 0;
}

template<typename MetadataValue>
[[nodiscard]] inline b8 ReadOptionalMetadataBoolField(
    const Path& nwbFilePath,
    const MetadataValue& asset,
    const AStringView diagnosticPrefix,
    const AStringView fieldLabel,
    b8& outValue
){
    const MetadataValue* const fieldValue = asset.findField(fieldLabel);
    if(!fieldValue)
        return true;
    const auto parsed = ParseMetadataBoolField(nwbFilePath, diagnosticPrefix, *fieldValue, fieldLabel, "must be 0 or 1");
    if(!parsed)
        return false;
    outValue = *parsed;
    return true;
}

template<typename MetadataValue>
[[nodiscard]] inline b8 ReadOptionalFiniteF32Field(
    const Path& nwbFilePath,
    const MetadataValue& asset,
    const AStringView diagnosticPrefix,
    const AStringView fieldLabel,
    f32& outValue
){
    const MetadataValue* const fieldValue = asset.findField(fieldLabel);
    if(!fieldValue)
        return true;
    const auto parsed = ReadMetadataFiniteF32Value(nwbFilePath, *fieldValue, diagnosticPrefix, fieldLabel);
    if(!parsed)
        return false;
    outValue = *parsed;
    return true;
}

template<typename MetadataValue>
[[nodiscard]] inline b8 ReadOptionalI32Field(
    const Path& nwbFilePath,
    const MetadataValue& asset,
    const AStringView diagnosticPrefix,
    const AStringView fieldLabel,
    i32& outValue
){
    const MetadataValue* const fieldValue = asset.findField(fieldLabel);
    if(!fieldValue)
        return true;
    if(!fieldValue->isInteger()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' must be an integer"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return false;
    }
    const i64 integerValue = fieldValue->asInteger();
    if(integerValue < static_cast<i64>(Limit<i32>::s_Min) || integerValue > static_cast<i64>(Limit<i32>::s_Max)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' is outside the supported 32-bit signed integer range"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return false;
    }
    outValue = static_cast<i32>(integerValue);
    return true;
}

template<typename EntryT, typename ParseValueFn>
[[nodiscard]] inline Expected<EntryT> ParseCookMetadataDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    const AStringView diagnosticPrefix,
    ParseValueFn&& parseValue,
    Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, diagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return parseValue(*virtualPathResult, **asset);
}

template<typename EntryT, ParseMetadataValueFn<EntryT> ParseValFn>
[[nodiscard]] inline Expected<EntryT> ParseCookMetadataDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    const AStringView diagnosticPrefix,
    AssetArena& arena,
    Alloc::ScratchArena& scratchArena
){
    return ParseCookMetadataDocument<EntryT>(assetRoot, virtualRoot, nwbFilePath, doc, diagnosticPrefix, [&](const Name virtualPath, const Core::Metascript::Value& asset){
        return ParseValFn(virtualPath, nwbFilePath, asset, arena, scratchArena);
    }, scratchArena);
}

template<typename EntryT, ParseMetadataValueFn<EntryT> ParseValFn>
[[nodiscard]] inline Expected<EntryT> ParseCookDependencyEntry(const CookDependencyItemContext& context, const AStringView diagnosticPrefix){
    return context.document
        ? ParseCookMetadataDocument<EntryT, ParseValFn>(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, diagnosticPrefix, context.arena, context.scratchArena)
        : ParseValFn(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_NwbAssetCacheMagic = 0x4341574eu; // NWAC
inline constexpr u16 s_NwbAssetCacheVersion = 2u;
inline constexpr u32 s_NwbAssetCacheFlagStandalone = 1u << 0u;

struct NwbAssetCacheFileHeader{
    u32 magic = s_NwbAssetCacheMagic;
    u16 version = s_NwbAssetCacheVersion;
    u16 headerSize = sizeof(NwbAssetCacheFileHeader);
    u32 flags = 0u;
    u32 entryCount = 0u;
    Sha256Digest paramSha256 = {};
    u64 payloadSectionSize = 0u;
    u64 payloadSectionHashFnv64 = 0u;
};

struct NwbFileAssetCacheRecord{
    Path cacheFilePath;
    CookString relativePathText;
    Sha256Digest paramSha256 = {};
    b8 standalone = false;
    b8 loadedFromCache = false;
    b8 canWriteCache = false;


public:
    explicit NwbFileAssetCacheRecord(CookArena& arena)noexcept
        : cacheFilePath(arena)
        , relativePathText(arena)
    {}
};

using NwbFileAssetCacheRecordVector = CookVector<NwbFileAssetCacheRecord>;

struct ParsedAssetMetadata{
    CookArena& arena;
    CookEntryRegistry entryRegistry;
    ParsedMetadataExtensionMap extensions;
    NwbFileAssetCacheRecordVector fileCacheRecords;


public:
    explicit ParsedAssetMetadata(CookArena& arena)
        : arena(arena)
        , entryRegistry(arena)
        , extensions(0, Hasher<Name>(), EqualTo<Name>(), arena)
        , fileCacheRecords(arena)
    {}
};

template<typename T>
[[nodiscard]] T* FindParsedMetadataExtension(const Name& extensionName, ParsedAssetMetadata& inOutMetadata)noexcept{
    auto found = inOutMetadata.extensions.find(extensionName);
    return found == inOutMetadata.extensions.end() ? nullptr : static_cast<T*>(found.value().get());
}

template<typename T>
[[nodiscard]] const T* FindParsedMetadataExtension(const Name& extensionName, const ParsedAssetMetadata& metadata)noexcept{
    auto found = metadata.extensions.find(extensionName);
    return found == metadata.extensions.end() ? nullptr : static_cast<const T*>(found.value().get());
}

template<typename T, typename... Args>
[[nodiscard]] T& RequireParsedMetadataExtension(const Name& extensionName, ParsedAssetMetadata& inOutMetadata, Args&&... args){
    if(T* existing = FindParsedMetadataExtension<T>(extensionName, inOutMetadata))
        return *existing;

    ParsedMetadataExtension pending = MakeParsedMetadataExtension<T>(inOutMetadata.arena, Forward<Args>(args)...);
    auto inserted = inOutMetadata.extensions.try_emplace(extensionName, Move(pending));
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

class CompositeRootAssetTypeAutoRegistrar final{
public:
    explicit CompositeRootAssetTypeAutoRegistrar(const Name& assetType);
};

struct AssetPairedSidecarRule{
    AStringView sidecarExtension;
};

class NonStandaloneAssetTypeAutoRegistrar final{
public:
    explicit NonStandaloneAssetTypeAutoRegistrar(const Name& assetType, AStringView tokenText = {});
};

class NonStandaloneFirstTokenSuffixAutoRegistrar final{
public:
    explicit NonStandaloneFirstTokenSuffixAutoRegistrar(AStringView tokenSuffix);
};

class AssetPairedSidecarRuleAutoRegistrar final{
public:
    explicit AssetPairedSidecarRuleAutoRegistrar(const AssetPairedSidecarRule& rule);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RegisterCompositeRootAssetType(const Name& assetType);
[[nodiscard]] b8 IsCompositeRootAssetType(const Name& assetType);
void RegisterNonStandaloneAssetType(const Name& assetType, AStringView tokenText = {});
void RegisterNonStandaloneFirstTokenSuffix(AStringView tokenSuffix);
[[nodiscard]] b8 IsNonStandaloneAssetType(const Name& assetType);
[[nodiscard]] b8 IsLikelyNonStandaloneFirstToken(AStringView firstToken);
void RegisterAssetPairedSidecarRule(const AssetPairedSidecarRule& rule);
[[nodiscard]] b8 AddPlannedFileCount(u64 additionalFileCount, u64& inOutPlannedFileCount);
[[nodiscard]] AssetMetadataParseResult::Enum TryAutoCollectedDocumentMetadataParsers(AssetDocumentMetadataParseContext& context);
[[nodiscard]] AssetMetadataParseResult::Enum TryAutoCollectedValueMetadataParsers(AssetValueMetadataParseContext& context);
[[nodiscard]] Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> TryAutoCollectedAssetBunchExpanders(const AssetBunchExpandContext& context);
[[nodiscard]] b8 ParseAssetMetadata(Span<const DiscoveredNwbFile> nwbFiles, const Path& repoRoot, const Path& assetCacheDirectory, CookArena& cookArena, Core::CpuTaskScheduler& cpuScheduler, ScratchArena& scratchArena, ParsedAssetMetadata& outMetadata);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

