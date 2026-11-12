// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/task/cpu/scheduler.h>
#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/metascript/parser.h>

#include <global/not_null.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookEntryPathHashSet = HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ICookedAssetWriter;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CookEntrySourceLocation{
    usize nwbFileIndex = Limit<usize>::s_Max;
    u32 nwbSubIndex = 0u;
};

struct CachedCookedBucketEntry{
    Name virtualPath = s_NameNone;
    AssetBytes payloadBytes;
    usize nwbFileIndex = Limit<usize>::s_Max;
    u32 nwbSubIndex = 0u;


public:
    explicit CachedCookedBucketEntry(CookArena& arena)noexcept
        : payloadBytes(arena)
    {}
};

struct CookEntryParseContext{
    CookArena& cookArena;
    Core::CpuTaskScheduler& cpuScheduler;
    ScratchArena& scratchArena;
    CookEntryPathHashSet& seenVirtualPathHashes;
    usize nwbFileIndex = Limit<usize>::s_Max;
    u32 nwbSubIndex = 0u;
};

struct CookEntryWriteContext{
    ICookedAssetWriter& writer;
    CookEntryPathHashSet& seenVirtualPathHashes;
    ScratchArena& scratchArena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ICookedAssetWriter{
public:
    virtual ~ICookedAssetWriter()noexcept = default;


public:
    virtual b8 writeCookedAsset(TStringView assetKind, const Name& virtualPath, const IAsset& asset, const IAssetCodec& codec, const CookEntrySourceLocation& location) = 0;
    virtual b8 writeCachedCookedAsset(TStringView assetKind, const Name& virtualPath, const IAssetCodec& codec, Span<const u8> payloadBytes) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ICookEntryBucket : NoCopy{
public:
    virtual ~ICookEntryBucket()noexcept = default;


public:
    [[nodiscard]] virtual usize size()const noexcept = 0;


public:
    virtual b8 parseDocument(const Path& assetRoot, AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, CookEntryParseContext& context) = 0;
    virtual b8 parseValue(Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, CookEntryParseContext& context) = 0;
    virtual b8 appendCachedEntry(const CookEntrySourceLocation& location, const Name& virtualPath, Span<const u8> payloadBytes, CookEntryParseContext& context) = 0;
    virtual b8 writeCookedAssets(CookEntryWriteContext& context) = 0;
};

template<typename EntryT>
class CookEntryBucketTyped : public ICookEntryBucket{
public:
    [[nodiscard]] virtual CookVector<EntryT>& entries()noexcept = 0;
    [[nodiscard]] virtual const CookVector<EntryT>& entries()const noexcept = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CookEntryRegistryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Resolves a cook entry's virtualPath to a Name; strings hash on demand.
[[nodiscard]] constexpr Expected<Optional<Name>, NameTextError::Enum> ToCookEntryName(const Name& virtualPath)noexcept{
    if(!virtualPath)
        return Optional<Name>{};
    return Optional<Name>{ virtualPath };
}

template<typename StringT>
    requires requires(const StringT& text){ text.data(); text.size(); }
[[nodiscard]] inline Expected<Optional<Name>, NameTextError::Enum> ToCookEntryName(const StringT& virtualPath){
    return ToName(virtualPath);
}

[[nodiscard]] inline AStringView ToCookEntryText(const Name& virtualPath){
    return virtualPath.resolvedText();
}

template<typename StringT>
    requires requires(const StringT& text){ typename StringT::value_type; text.data(); text.size(); }
[[nodiscard]] inline BasicStringView<typename StringT::value_type> ToCookEntryText(const StringT& virtualPath)noexcept(noexcept(BasicStringView<typename StringT::value_type>(virtualPath.data(), virtualPath.size()))){
    return BasicStringView<typename StringT::value_type>(virtualPath.data(), virtualPath.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 RegisterParsedVirtualPath(TStringView assetKind, const Name& virtualPath, CookEntryPathHashSet& inOutSeenVirtualPathHashes);
[[nodiscard]] b8 RegisterCookedVirtualPath(TStringView assetKind, const Name& virtualPath, CookEntryPathHashSet& inOutSeenVirtualPathHashes);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename EntryT, typename AssetT, typename CodecT>
class CookEntryBucket final : public CookEntryBucketTyped<EntryT>{
public:
    using DocumentParseFunction = Expected<EntryT> (*)(const Path& assetRoot, AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, CookEntryParseContext& context);
    using ValueParseFunction = Expected<EntryT> (*)(Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, CookEntryParseContext& context);
    using BuildAssetFunction = Expected<AssetT> (*)(EntryT& entry, AssetArena& arena, ScratchArena& scratchArena);


public:
    CookEntryBucket(CookArena& arena, const Name& assetType, const TStringView assetKindText, DocumentParseFunction parseDocument, ValueParseFunction parseValue, const NotNull<BuildAssetFunction> buildAsset, const b8 logBuildFailure)noexcept
        : m_entries(arena)
        , m_entryLocations(arena)
        , m_cachedEntries(arena)
        , m_assetKindText(assetKindText)
        , m_parseDocument(parseDocument)
        , m_parseValue(parseValue)
        , m_buildAsset(buildAsset)
        , m_assetType(assetType)
        , m_logBuildFailure(logBuildFailure)
    {}


public:
    [[nodiscard]] virtual usize size()const noexcept override{ return m_entries.size() + m_cachedEntries.size(); }

    [[nodiscard]] virtual CookVector<EntryT>& entries()noexcept override{ return m_entries; }

    [[nodiscard]] virtual const CookVector<EntryT>& entries()const noexcept override{ return m_entries; }


public:
    virtual b8 parseDocument(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, CookEntryParseContext& context)override{
        if(!m_parseDocument){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: asset type '{}' cannot be parsed from document '{}'"), ConvertString(m_assetType.resolvedText()), PathToString<tchar>(nwbFilePath));
            return false;
        }

        auto entry = m_parseDocument(assetRoot, virtualRoot, nwbFilePath, doc, context);
        if(!entry)
            return false;

        return appendParsedEntry(Move(*entry), context);
    }

    virtual b8 parseValue(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, CookEntryParseContext& context)override{
        if(!m_parseValue){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: asset type '{}' cannot be parsed from asset_bunch item in '{}'"), ConvertString(m_assetType.resolvedText()), PathToString<tchar>(nwbFilePath));
            return false;
        }

        auto entry = m_parseValue(virtualPath, nwbFilePath, asset, context);
        if(!entry)
            return false;

        return appendParsedEntry(Move(*entry), context);
    }

    virtual b8 appendCachedEntry(const CookEntrySourceLocation& location, const Name& virtualPath, const Span<const u8> payloadBytes, CookEntryParseContext& context)override{
        Core::Assets::AssetArena& assetArena = m_entries.get_allocator().arena();
        CachedCookedBucketEntry cachedEntry(assetArena);
        cachedEntry.nwbFileIndex = location.nwbFileIndex;
        cachedEntry.nwbSubIndex = location.nwbSubIndex;
        cachedEntry.virtualPath = virtualPath;
        if(!payloadBytes.empty())
            cachedEntry.payloadBytes.assign(payloadBytes.begin(), payloadBytes.end());
        m_cachedEntries.reserve(m_cachedEntries.size() + 1u);

        if(!CookEntryRegistryDetail::RegisterParsedVirtualPath(m_assetKindText, virtualPath, context.seenVirtualPathHashes))
            return false;
        ScopeExit rollbackSeenPath([&]()noexcept{
            if(virtualPath)
                context.seenVirtualPathHashes.erase(virtualPath.hash());
        });

        m_cachedEntries.push_back(Move(cachedEntry));
        rollbackSeenPath.release();
        return true;
    }

    virtual b8 writeCookedAssets(CookEntryWriteContext& context)override{
        FOUNDATION_ASSERT(m_entryLocations.size() == m_entries.size());

        CodecT codec;
        Core::Assets::AssetArena& assetArena = m_entries.get_allocator().arena();

        usize entryIndex = 0u;
        usize cachedIndex = 0u;
        while(entryIndex < m_entries.size() || cachedIndex < m_cachedEntries.size()){
            const b8 takeCached = [&]()noexcept{
                if(cachedIndex >= m_cachedEntries.size())
                    return false;
                if(entryIndex >= m_entries.size())
                    return true;
                const CookEntrySourceLocation& entryLocation = m_entryLocations[entryIndex];
                const CachedCookedBucketEntry& cachedEntry = m_cachedEntries[cachedIndex];
                if(cachedEntry.nwbFileIndex != entryLocation.nwbFileIndex)
                    return cachedEntry.nwbFileIndex < entryLocation.nwbFileIndex;
                return cachedEntry.nwbSubIndex < entryLocation.nwbSubIndex;
            }();

            if(takeCached){
                const CachedCookedBucketEntry& cachedEntry = m_cachedEntries[cachedIndex++];
                if(!CookEntryRegistryDetail::RegisterCookedVirtualPath(m_assetKindText, cachedEntry.virtualPath, context.seenVirtualPathHashes))
                    return false;
                if(!context.writer.writeCachedCookedAsset(m_assetKindText, cachedEntry.virtualPath, codec, cachedEntry.payloadBytes))
                    return false;
                continue;
            }

            const CookEntrySourceLocation entryLocation = m_entryLocations[entryIndex];
            EntryT& entry = m_entries[entryIndex++];
            const auto virtualPath = CookEntryRegistryDetail::ToCookEntryName(entry.virtualPath);
            if(!virtualPath){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: {} virtual path contains an embedded null"), m_assetKindText);
                return false;
            }
            const Name resolvedVirtualPath = *virtualPath ? **virtualPath : s_NameNone;
            if(!CookEntryRegistryDetail::RegisterCookedVirtualPath(m_assetKindText, resolvedVirtualPath, context.seenVirtualPathHashes))
                return false;

            auto asset = m_buildAsset.get()(entry, assetArena, context.scratchArena);
            if(!asset){
                if(m_logBuildFailure){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: failed to build {} '{}'"), m_assetKindText, ConvertString(CookEntryRegistryDetail::ToCookEntryText(entry.virtualPath)));
                }
                return false;
            }

            if(!context.writer.writeCookedAsset(m_assetKindText, resolvedVirtualPath, *asset, codec, entryLocation))
                return false;
        }

        return true;
    }


private:
    b8 appendParsedEntry(EntryT&& entry, CookEntryParseContext& context){
        const auto virtualPath = CookEntryRegistryDetail::ToCookEntryName(entry.virtualPath);
        if(!virtualPath){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: {} virtual path contains an embedded null"), m_assetKindText);
            return false;
        }
        const usize nextCount = m_entries.size() + 1u;
        m_entries.reserve(nextCount);
        m_entryLocations.reserve(nextCount);

        const Name resolvedVirtualPath = *virtualPath ? **virtualPath : s_NameNone;
        if(!CookEntryRegistryDetail::RegisterParsedVirtualPath(m_assetKindText, resolvedVirtualPath, context.seenVirtualPathHashes))
            return false;
        ScopeExit rollbackSeenPath([&]()noexcept{
            if(resolvedVirtualPath)
                context.seenVirtualPathHashes.erase(resolvedVirtualPath.hash());
        });

        m_entries.push_back(Move(entry));
        m_entryLocations.push_back(CookEntrySourceLocation{ context.nwbFileIndex, context.nwbSubIndex });
        rollbackSeenPath.release();
        return true;
    }


private:
    CookVector<EntryT> m_entries;
    CookVector<CookEntrySourceLocation> m_entryLocations;
    CookVector<CachedCookedBucketEntry> m_cachedEntries;
    TStringView m_assetKindText;
    DocumentParseFunction m_parseDocument = nullptr;
    ValueParseFunction m_parseValue = nullptr;
    NotNull<BuildAssetFunction> m_buildAsset;
    Name m_assetType = s_NameNone;
    b8 m_logBuildFailure = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CookEntryRegistry : NoCopy{
private:
    using BucketPtr = UniquePtr<ICookEntryBucket>;
    using BucketVector = CookVector<BucketPtr>;
    using BucketLookup = CookMap<Name, NotNull<ICookEntryBucket*>>;


public:
    explicit CookEntryRegistry(CookArena& arena)
        : m_arena(arena)
        , m_buckets(arena)
        , m_lookup(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    template<typename BucketT>
    b8 registerCustomType(const Name& assetType){
        if(!assetType){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: tried to register an unnamed custom cook entry type"));
            return false;
        }

        auto bucket = MakeUnique<BucketT>(m_arena);
        return registerBucket(assetType, Move(bucket));
    }


public:
    // The asset label's backing text must outlive this registry.
    template<typename EntryT, typename AssetT, typename CodecT>
    b8 registerType(
        const Name& assetType,
        const TStringView assetKindText,
        typename CookEntryBucket<EntryT, AssetT, CodecT>::DocumentParseFunction parseDocument,
        typename CookEntryBucket<EntryT, AssetT, CodecT>::ValueParseFunction parseValue,
        typename CookEntryBucket<EntryT, AssetT, CodecT>::BuildAssetFunction buildAsset,
        const b8 logBuildFailure = true
    ){
        if(!assetType){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: tried to register an unnamed cook entry type"));
            return false;
        }
        if(!buildAsset){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: cook entry type '{}' has no build function"), ConvertString(assetType.resolvedText()));
            return false;
        }

        auto bucket = MakeUnique<CookEntryBucket<EntryT, AssetT, CodecT>>(m_arena, assetType, assetKindText, parseDocument, parseValue, MakeNotNull(buildAsset), logBuildFailure);
        return registerBucket(assetType, Move(bucket));
    }

    [[nodiscard]] ICookEntryBucket* find(const Name& assetType)const noexcept;
    [[nodiscard]] b8 has(const Name& assetType)const noexcept;

    template<typename EntryT>
    [[nodiscard]] CookVector<EntryT>& entries(const Name& assetType)const noexcept{
        const NotNull<ICookEntryBucket*> bucket(find(assetType));
        return checked_cast<CookEntryBucketTyped<EntryT>*>(bucket.get())->entries();
    }

    [[nodiscard]] b8 parseDocument(const Name& assetType, const Path& assetRoot, AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, CookEntryParseContext& context);
    [[nodiscard]] b8 parseValue(const Name& assetType, Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, CookEntryParseContext& context);

    template<typename RootArenaT, typename FileArenaT>
        requires (!IsSame_V<RootArenaT, Alloc::GlobalArena> || !IsSame_V<FileArenaT, Alloc::GlobalArena>)
    [[nodiscard]] FOUNDATION_INLINE b8 parseDocument(const Name& assetType, const ::Path<RootArenaT>& assetRoot, const AStringView virtualRoot, const ::Path<FileArenaT>& nwbFilePath, const Core::Metascript::Document& doc, CookEntryParseContext& context){
        return parseDocument(assetType, Path(context.cookArena, assetRoot), virtualRoot, Path(context.cookArena, nwbFilePath), doc, context);
    }

    template<typename FileArenaT>
        requires (!IsSame_V<FileArenaT, Alloc::GlobalArena>)
    [[nodiscard]] FOUNDATION_INLINE b8 parseValue(const Name& assetType, const Name virtualPath, const ::Path<FileArenaT>& nwbFilePath, const Core::Metascript::Value& asset, CookEntryParseContext& context){
        return parseValue(assetType, virtualPath, Path(context.cookArena, nwbFilePath), asset, context);
    }
    [[nodiscard]] b8 appendCachedEntry(const Name& assetType, const CookEntrySourceLocation& location, const Name& virtualPath, Span<const u8> payloadBytes, CookEntryParseContext& context);
    [[nodiscard]] usize bucketCount()const noexcept;
    [[nodiscard]] b8 writeBucket(usize bucketIndex, CookEntryWriteContext& context);
    [[nodiscard]] u64 entryCount()const noexcept;


private:
    b8 registerBucket(const Name& assetType, BucketPtr owner);


private:
    CookArena& m_arena;
    BucketVector m_buckets;
    BucketLookup m_lookup;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookEntryRegistrationFunction = b8 (*)(CookEntryRegistry& registry);

class CookEntryAutoRegistrar final{
public:
    explicit CookEntryAutoRegistrar(CookEntryRegistrationFunction function);
};

#define NWB_DEFINE_COOK_ENTRY_REGISTRAR(registrarVariable, registerFunction) \
    Core::Assets::CookEntryAutoRegistrar registrarVariable(&registerFunction)

template<typename EntryT, typename AssetT, typename CodecT>
[[nodiscard]] inline b8 RegisterDocumentValueCookEntry(
    const TStringView assetKindText,
    typename CookEntryBucket<EntryT, AssetT, CodecT>::DocumentParseFunction parseDocument,
    typename CookEntryBucket<EntryT, AssetT, CodecT>::ValueParseFunction parseValue,
    typename CookEntryBucket<EntryT, AssetT, CodecT>::BuildAssetFunction buildAsset,
    const b8 logBuildFailure,
    CookEntryRegistry& outRegistry
){
    return outRegistry.registerType<EntryT, AssetT, CodecT>(AssetT::AssetTypeName(), assetKindText, parseDocument, parseValue, buildAsset, logBuildFailure);
}

[[nodiscard]] b8 RegisterAutoCollectedCookEntryTypes(CookEntryRegistry& registry, ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

