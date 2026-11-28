// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset_file_discovery.h"
#include "built_asset_format.h"
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

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline Expected<b8> ParseMetadataBoolField(
    const ::Path<PathArenaT>& nwbFilePath,
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

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline b8 ReadOptionalMetadataBoolField(
    const ::Path<PathArenaT>& nwbFilePath,
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

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline b8 ReadOptionalFiniteF32Field(
    const ::Path<PathArenaT>& nwbFilePath,
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

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline b8 ReadOptionalI32Field(
    const ::Path<PathArenaT>& nwbFilePath,
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

template<typename EntryT, typename Reader>
[[nodiscard]] inline Expected<EntryT> ReadCookDependencyWith(const CookDependencyItemContext& context, const AStringView prefix, Reader&& reader){
    const auto read = [&](const Name path, const Core::Metascript::Value& value){
        if constexpr(requires{ reader(context.arena, path, context.nwbFilePath, value); })
            return reader(context.arena, path, context.nwbFilePath, value);
        else
            return reader(path, context.nwbFilePath, value);
    };
    if(context.document)
        return ParseCookMetadataDocument<EntryT>(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, prefix, read, context.scratchArena);
    return read(context.virtualPath, context.asset);
}

template<typename EntryT>
[[nodiscard]] inline Expected<EntryT> ReadCookDependencyEntry(const CookDependencyItemContext& context, const AStringView prefix, Expected<EntryT>(*reader)(Name, const Path&, const Core::Metascript::Value&)){
    return ReadCookDependencyWith<EntryT>(context, prefix, reader);
}

template<typename EntryT>
[[nodiscard]] inline Expected<EntryT> ReadCookDependencyEntry(const CookDependencyItemContext& context, const AStringView prefix, Expected<EntryT>(*reader)(::ArenaBase&, Name, const Path&, const Core::Metascript::Value&)){
    return ReadCookDependencyWith<EntryT>(context, prefix, reader);
}

template<typename EntryT, ParseMetadataValueFn<EntryT> ParseValFn>
[[nodiscard]] inline Expected<EntryT> ParseCookDependencyEntry(const CookDependencyItemContext& context, const AStringView diagnosticPrefix){
    return context.document
        ? ParseCookMetadataDocument<EntryT, ParseValFn>(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, diagnosticPrefix, context.arena, context.scratchArena)
        : ParseValFn(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbFileAssetCacheRecord{
    Path cacheFilePath;
    CookString sourcePathText;
    CookVector<Name> cachedVirtualPaths;
    Sha256Digest paramSha256 = {};
    b8 standalone = false;
    b8 loadedFromCache = false;
    b8 canWriteCache = false;


public:
    explicit NwbFileAssetCacheRecord(CookArena& arena)noexcept
        : cacheFilePath(arena)
        , sourcePathText(arena)
        , cachedVirtualPaths(arena)
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

template<typename Asset>
class CompositeRootRegistration final{
public:
    CompositeRootRegistration() : m_registration(Asset::AssetTypeName()){}

private:
    CompositeRootAssetTypeAutoRegistrar m_registration;
};

struct AssetPairedSidecarRule{
    AStringView sidecarExtension;
};

struct CookExternalSourceCollectContext{
    const DiscoveredNwbFile& discoveredNwbFile;
    AStringView metaText;
    ScratchArena& scratchArena;
    const Core::Metascript::Document* (*ensureParsedDocument)(void* user) = nullptr;
    void* ensureParsedDocumentUser = nullptr;
};

using CookExternalSourceCollectorFunction = b8 (*)(
    const CookExternalSourceCollectContext& context,
    Vector<::Path<ScratchArena>, ScratchArena>& outPaths,
    b8& outCanUseCache
);

class NonStandaloneAssetTypeAutoRegistrar final{
public:
    explicit NonStandaloneAssetTypeAutoRegistrar(const Name& assetType, AStringView tokenText = {});
};

template<typename Asset>
class NonStandaloneAssetRegistration final{
public:
    NonStandaloneAssetRegistration() : m_registration(Asset::AssetTypeName(), Asset::s_AssetTypeText){}
private:
    NonStandaloneAssetTypeAutoRegistrar m_registration;
};

class NonStandaloneFirstTokenSuffixAutoRegistrar final{
public:
    explicit NonStandaloneFirstTokenSuffixAutoRegistrar(AStringView tokenSuffix);
};

class AssetPairedSidecarRuleAutoRegistrar final{
public:
    explicit AssetPairedSidecarRuleAutoRegistrar(const AssetPairedSidecarRule& rule);
};

class CookExternalSourceCollectorAutoRegistrar final{
public:
    explicit CookExternalSourceCollectorAutoRegistrar(CookExternalSourceCollectorFunction function);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RegisterCompositeRootAssetType(const Name& assetType);
[[nodiscard]] b8 IsCompositeRootAssetType(const Name& assetType);
void RegisterNonStandaloneAssetType(const Name& assetType, AStringView tokenText = {});
void RegisterNonStandaloneFirstTokenSuffix(AStringView tokenSuffix);
[[nodiscard]] b8 IsNonStandaloneAssetType(const Name& assetType);
[[nodiscard]] b8 IsLikelyNonStandaloneFirstToken(AStringView firstToken);
void RegisterAssetPairedSidecarRule(const AssetPairedSidecarRule& rule);
void RegisterCookExternalSourceCollector(CookExternalSourceCollectorFunction function);
[[nodiscard]] b8 AddPlannedFileCount(u64 additionalFileCount, u64& inOutPlannedFileCount);
[[nodiscard]] AssetMetadataParseResult::Enum TryAutoCollectedDocumentMetadataParsers(AssetDocumentMetadataParseContext& context);
[[nodiscard]] AssetMetadataParseResult::Enum TryAutoCollectedValueMetadataParsers(AssetValueMetadataParseContext& context);
[[nodiscard]] Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> TryAutoCollectedAssetBunchExpanders(const AssetBunchExpandContext& context);
[[nodiscard]] b8 ParseAssetMetadata(Span<const DiscoveredNwbFile> nwbFiles, const Path& assetCacheDirectory, CookArena& cookArena, Core::CpuTaskScheduler& cpuScheduler, ScratchArena& scratchArena, ParsedAssetMetadata& outMetadata);

#define NWB_COOK_DISPATCH(fn) \
    [](auto&&... __nwbCookArgs) -> decltype(fn(::Forward<decltype(__nwbCookArgs)>(__nwbCookArgs)...)){ \
        return fn(::Forward<decltype(__nwbCookArgs)>(__nwbCookArgs)...); \
    }

namespace CookEntryRegistrationDetail{
template<typename Read>
[[nodiscard]] consteval b8 CanInvokeDocumentReader()noexcept{
    return requires(
        Read read,
        CookEntryParseContext& context,
        const Path& root,
        AStringView virtualRoot,
        const Path& path,
        const Core::Metascript::Document& document
    ){
        read(context.cookArena, root, virtualRoot, path, document);
    } || requires(
        Read read,
        const Path& root,
        AStringView virtualRoot,
        const Path& path,
        const Core::Metascript::Document& document
    ){
        read(root, virtualRoot, path, document);
    };
}

template<typename Read>
[[nodiscard]] inline decltype(auto) InvokeDocumentReader(
    CookEntryParseContext& context,
    const Path& root,
    const AStringView virtualRoot,
    const Path& path,
    const Core::Metascript::Document& document
){
    if constexpr(requires(Read read){ read(context.cookArena, root, virtualRoot, path, document); })
        return Read{}(context.cookArena, root, virtualRoot, path, document);
    else
        return Read{}(root, virtualRoot, path, document);
}

template<typename Read>
[[nodiscard]] inline decltype(auto) InvokeValueReader(
    CookEntryParseContext& context,
    const Name virtualPath,
    const Path& path,
    const Core::Metascript::Value& value
){
    if constexpr(requires(Read read){ read(context.cookArena, virtualPath, path, value, context.cpuScheduler); })
        return Read{}(context.cookArena, virtualPath, path, value, context.cpuScheduler);
    else if constexpr(requires(Read read){ read(virtualPath, path, value, context.cpuScheduler); })
        return Read{}(virtualPath, path, value, context.cpuScheduler);
    else if constexpr(requires(Read read){ read(context.cookArena, virtualPath, path, value); })
        return Read{}(context.cookArena, virtualPath, path, value);
    else
        return Read{}(virtualPath, path, value);
}

template<typename Build>
[[nodiscard]] inline decltype(auto) InvokeAssetBuilder(AssetArena& arena, auto& entry){
    if constexpr(requires(Build build){ build(arena, entry); })
        return Build{}(arena, entry);
    else
        return Build{}(entry);
}
};

template<typename Entry, typename Asset, typename Codec, const AStringView& DiagnosticPrefix, b8 AcceptsValue, typename Read, typename Build>
[[nodiscard]] inline b8 RegisterCookEntry(
    CookEntryRegistry& registry,
    const TStringView label,
    const b8 logsFailure,
    Read,
    Build
){
    using Bucket = CookEntryBucket<Entry, Asset, Codec>;
    typename Bucket::DocumentParseFunction readDocument;
    typename Bucket::ValueParseFunction readValue = nullptr;
    if constexpr(!AcceptsValue && CookEntryRegistrationDetail::CanInvokeDocumentReader<Read>()){
        readDocument = [](const Path& root, const AStringView virtualRoot, const Path& path, const Core::Metascript::Document& document, CookEntryParseContext& context){
            return CookEntryRegistrationDetail::InvokeDocumentReader<Read>(context, root, virtualRoot, path, document);
        };
    }
    else{
        readDocument = [](const Path& root, const AStringView virtualRoot, const Path& path, const Core::Metascript::Document& document, CookEntryParseContext& context){
            return ParseCookMetadataDocument<Entry>(root, virtualRoot, path, document, DiagnosticPrefix, [&](const Name virtualPath, const Core::Metascript::Value& value){
                return CookEntryRegistrationDetail::InvokeValueReader<Read>(context, virtualPath, path, value);
            }, context.scratchArena);
        };
        if constexpr(AcceptsValue){
            readValue = [](const Name virtualPath, const Path& path, const Core::Metascript::Value& value, CookEntryParseContext& context){
                return CookEntryRegistrationDetail::InvokeValueReader<Read>(context, virtualPath, path, value);
            };
        }
    }
    const typename Bucket::BuildAssetFunction build = [](Entry& entry, AssetArena& arena, ScratchArena&){
        return CookEntryRegistrationDetail::InvokeAssetBuilder<Build>(arena, entry);
    };
    return RegisterDocumentValueCookEntry<Entry, Asset, Codec>(label, readDocument, readValue, build, logsFailure, registry);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

