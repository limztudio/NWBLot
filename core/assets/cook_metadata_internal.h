// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_metadata.h"

#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CookMetadataDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using DiscoveredPathSet = HashSet<ScratchString, ScratchArena, Hasher<AStringView>, EqualTo<>>;
using EnsureCookParsedDocumentFunction = const Core::Metascript::Document* (*)(void* user);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NwbCacheLoadResult{
    enum Enum : u8{
        Miss,
        Hit,
        Error
    };
};

struct AutoMetadataParser final{
    AssetDocumentMetadataParseFunction documentFunction = nullptr;
    AssetValueMetadataParseFunction valueFunction = nullptr;
};

struct CookMetadataRegistrySnapshot final{
    Vector<AutoMetadataParser, ScratchArena> metadataParsers;
    Vector<NotNull<AssetBunchExpandFunction>, ScratchArena> bunchExpanders;
    Vector<Name, ScratchArena> nonStandaloneTypes;
    Vector<AStringView, ScratchArena> nonStandaloneTokens;
    Vector<AStringView, ScratchArena> nonStandaloneTokenSuffixes;


public:
    explicit CookMetadataRegistrySnapshot(ScratchArena& arena)noexcept
        : metadataParsers(arena)
        , bunchExpanders(arena)
        , nonStandaloneTypes(arena)
        , nonStandaloneTokens(arena)
        , nonStandaloneTokenSuffixes(arena)
    {}
};

class CookExternalSourceDigestCache final : NoCopy{
public:
    struct Entry final{
        Sha256Digest digest = {};
        b8 ok = false;
        b8 exists = false;
    };


public:
    CookExternalSourceDigestCache()
        : m_arena(Name("cook_meta/ext_digest"))
        , m_entries(0, Hasher<AStringView>(), EqualTo<>(), m_arena)
    {}

    [[nodiscard]] b8 find(AStringView canonicalPathText, Entry& outEntry)const noexcept;
    void insert(AStringView canonicalPathText, const Entry& entry);


private:
    mutable Futex m_mutex;
    ScratchArena m_arena;
    HashMap<ScratchString, Entry, ScratchArena, Hasher<AStringView>, EqualTo<>> m_entries;
};

void CopyPairedSidecarRules(Vector<AssetPairedSidecarRule, ScratchArena>& outRules);
void CopyCookExternalSourceCollectors(Vector<NotNull<CookExternalSourceCollectorFunction>, ScratchArena>& outCollectors);
void CaptureMetadataRegistrySnapshot(CookMetadataRegistrySnapshot& outSnapshot);
[[nodiscard]] AssetMetadataParseResult::Enum TryDocumentMetadataParsers(const CookMetadataRegistrySnapshot& snapshot, AssetDocumentMetadataParseContext& context);
[[nodiscard]] AssetMetadataParseResult::Enum TryValueMetadataParsers(const CookMetadataRegistrySnapshot& snapshot, AssetValueMetadataParseContext& context);
[[nodiscard]] Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> TryAssetBunchExpanders(const CookMetadataRegistrySnapshot& snapshot, const AssetBunchExpandContext& context);
[[nodiscard]] b8 IsNonStandaloneAssetType(const CookMetadataRegistrySnapshot& snapshot, const Name& assetType)noexcept;
[[nodiscard]] b8 IsLikelyNonStandaloneFirstToken(const CookMetadataRegistrySnapshot& snapshot, AStringView firstToken)noexcept;
[[nodiscard]] b8 ComputeNwbFileAssetCacheData(
    const DiscoveredNwbFile& discoveredNwbFile,
    AStringView metaText,
    const Sha256Digest& producerDigest,
    Span<const AssetPairedSidecarRule> pairedSidecarRules,
    Span<const NotNull<CookExternalSourceCollectorFunction>> externalSourceCollectors,
    ScratchArena& scratchArena,
    ScratchString& outSourcePathText,
    ScratchString& outCacheFileName,
    Sha256Digest& outParamSha256,
    CookExternalSourceDigestCache* externalDigestCache = nullptr,
    EnsureCookParsedDocumentFunction ensureParsedDocument = nullptr,
    void* ensureParsedDocumentUser = nullptr
);
void PopulateNwbFileAssetCacheRecord(
    const Path& assetCacheDirectory,
    AStringView sourcePathText,
    AStringView cacheFileName,
    const Sha256Digest& paramSha256,
    b8 canUseCache,
    NwbFileAssetCacheRecord& outRecord
);
void PrepareNwbFileAssetCacheRecord(
    const Path& assetCacheDirectory,
    const DiscoveredNwbFile& discoveredNwbFile,
    AStringView metaText,
    const Sha256Digest& producerDigest,
    Span<const AssetPairedSidecarRule> pairedSidecarRules,
    Span<const NotNull<CookExternalSourceCollectorFunction>> externalSourceCollectors,
    ScratchArena& scratchArena,
    ScratchString& inOutSourcePathText,
    ScratchString& inOutCacheFileName,
    NwbFileAssetCacheRecord& outRecord,
    CookExternalSourceDigestCache* externalDigestCache = nullptr,
    EnsureCookParsedDocumentFunction ensureParsedDocument = nullptr,
    void* ensureParsedDocumentUser = nullptr
);
void PrepareNwbFileAssetCacheRecord(
    const Path& assetCacheDirectory,
    const DiscoveredNwbFile& discoveredNwbFile,
    AStringView metaText,
    const Sha256Digest& producerDigest,
    ScratchArena& scratchArena,
    ScratchString& inOutSourcePathText,
    ScratchString& inOutCacheFileName,
    NwbFileAssetCacheRecord& outRecord
);
[[nodiscard]] NwbCacheLoadResult::Enum TryLoadNwbFileAssetCache(
    NwbFileAssetCacheRecord& inOutRecord,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    const ParsedAssetMetadata& metadata,
    const CookMetadataRegistrySnapshot* registrySnapshot = nullptr
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

