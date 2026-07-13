
#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/alloc/scratch.h>
#include <core/filesystem/module.h>

#include <core/common/log.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookArena = Core::Alloc::GlobalArena;
using CookString = AString<CookArena>;
template<typename T>
using CookVector = Vector<T, CookArena>;
template<typename T, typename V>
using CookMap = HashMap<T, V, Hasher<T>, EqualTo<T>, CookArena>;
template<typename T>
using CookHashSet = HashSet<T, Hasher<T>, EqualTo<T>, CookArena>;
using ScratchArena = Core::Alloc::ScratchArena;
using ScratchString = AString<ScratchArena>;
using CookEntryPathHashSet = CookHashSet<NameHash>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ICookedAssetWriter;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CookEntryParseContext{
    CookArena& cookArena;
    Core::Alloc::ThreadPool& threadPool;
    ScratchArena& scratchArena;
    CookEntryPathHashSet& seenVirtualPathHashes;
};

struct CookEntryWriteContext{
    ICookedAssetWriter& writer;
    CookEntryPathHashSet& seenVirtualPathHashes;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ICookedAssetWriter{
public:
    virtual ~ICookedAssetWriter() = default;

public:
    virtual bool writeCookedAsset(
        const tchar* assetKind,
        const Name& virtualPath,
        const IAsset& asset,
        const IAssetCodec& codec
    ) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ICookEntryBucket : NoCopy{
public:
    virtual ~ICookEntryBucket() = default;

public:
    [[nodiscard]] virtual usize size()const noexcept = 0;

public:
    virtual void reserve(usize entryCount) = 0;
    virtual bool parseDocument(
        const Path& assetRoot,
        AStringView virtualRoot,
        const Path& nwbFilePath,
        const Core::Metascript::Document& doc,
        CookEntryParseContext& context
    ) = 0;
    virtual bool parseValue(
        Name virtualPath,
        const Path& nwbFilePath,
        const Core::Metascript::Value& asset,
        CookEntryParseContext& context
    ) = 0;
    virtual bool writeCookedAssets(CookEntryWriteContext& context) = 0;
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


// Resolves a cook entry's `virtualPath` member to a Name regardless of how the entry stores it. Most asset entries
// store `virtualPath` as a Name -- this overload is a zero-cost identity pass-through, so their dedup/output is
// unchanged. An entry that instead keeps the readable source TEXT (e.g. the material entry, which needs the text for
// generated-shader file paths / `#include`s / identities -- Name::c_str() is only a hash off debug) stores a string
// `virtualPath`; this overload hashes it to the Name on demand. Either way the framework keys dedup + cooked output
// by the same Name. Logging keeps using entry.virtualPath.c_str() directly (Name and string both provide it).
[[nodiscard]] inline const Name& ToCookEntryName(const Name& virtualPath){
    return virtualPath;
}
template<typename StringT>
    requires requires(const StringT& text){ text.data(); text.size(); }
[[nodiscard]] inline Name ToCookEntryName(const StringT& virtualPath){
    return ToName(virtualPath);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool RegisterParsedVirtualPath(
    const tchar* assetKind,
    const Name& virtualPath,
    CookEntryPathHashSet& inOutSeenVirtualPathHashes
){
    if(!virtualPath)
        return true;

    const NameHash pathHash = virtualPath.hash();
    if(inOutSeenVirtualPathHashes.insert(pathHash).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: duplicate property asset virtual path '{}' for {}")
        , StringConvert(virtualPath.c_str())
        , assetKind
    );
    return false;
}

[[nodiscard]] inline bool RegisterCookedVirtualPath(
    const tchar* assetKind,
    const Name& virtualPath,
    CookEntryPathHashSet& inOutSeenVirtualPathHashes
){
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: invalid {} virtual path"), assetKind);
        return false;
    }

    const NameHash virtualPathHash = virtualPath.hash();
    if(inOutSeenVirtualPathHashes.insert(virtualPathHash).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: duplicate {} virtual path '{}'")
        , assetKind
        , StringConvert(virtualPath.c_str())
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename EntryT, typename AssetT, typename CodecT>
class CookEntryBucket final : public CookEntryBucketTyped<EntryT>{
public:
    using DocumentParseFunction = bool (*)(
        const Path& assetRoot,
        AStringView virtualRoot,
        const Path& nwbFilePath,
        const Core::Metascript::Document& doc,
        EntryT& outEntry,
        CookEntryParseContext& context
    );
    using ValueParseFunction = bool (*)(
        Name virtualPath,
        const Path& nwbFilePath,
        const Core::Metascript::Value& asset,
        EntryT& outEntry,
        CookEntryParseContext& context
    );
    using BuildAssetFunction = bool (*)(EntryT& entry, AssetT& outAsset);

public:
    CookEntryBucket(
        CookArena& arena,
        const Name& assetType,
        const tchar* assetKindText,
        DocumentParseFunction parseDocument,
        ValueParseFunction parseValue,
        BuildAssetFunction buildAsset,
        const bool logBuildFailure
    )
        : m_entries(arena)
        , m_assetType(assetType)
        , m_assetKindText(assetKindText)
        , m_parseDocument(parseDocument)
        , m_parseValue(parseValue)
        , m_buildAsset(buildAsset)
        , m_logBuildFailure(logBuildFailure)
    {}

public:
    [[nodiscard]] virtual usize size()const noexcept override{ return m_entries.size(); }
    [[nodiscard]] virtual CookVector<EntryT>& entries()noexcept override{ return m_entries; }
    [[nodiscard]] virtual const CookVector<EntryT>& entries()const noexcept override{ return m_entries; }

public:
    virtual void reserve(const usize entryCount)override{
        m_entries.reserve(entryCount);
    }

    virtual bool parseDocument(
        const Path& assetRoot,
        const AStringView virtualRoot,
        const Path& nwbFilePath,
        const Core::Metascript::Document& doc,
        CookEntryParseContext& context
    )override{
        if(!m_parseDocument){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: asset type '{}' cannot be parsed from document '{}'")
                , StringConvert(m_assetType.c_str())
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }

        EntryT entry(context.cookArena);
        if(!m_parseDocument(assetRoot, virtualRoot, nwbFilePath, doc, entry, context))
            return false;

        return appendParsedEntry(Move(entry), context);
    }

    virtual bool parseValue(
        const Name virtualPath,
        const Path& nwbFilePath,
        const Core::Metascript::Value& asset,
        CookEntryParseContext& context
    )override{
        if(!m_parseValue){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: asset type '{}' cannot be parsed from asset_bunch item in '{}'")
                , StringConvert(m_assetType.c_str())
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }

        EntryT entry(context.cookArena);
        if(!m_parseValue(virtualPath, nwbFilePath, asset, entry, context))
            return false;

        return appendParsedEntry(Move(entry), context);
    }

    virtual bool writeCookedAssets(CookEntryWriteContext& context)override{
        CodecT codec;
        Core::Assets::AssetArena& assetArena = m_entries.get_allocator().arena();

        for(EntryT& entry : m_entries){
            if(!CookEntryRegistryDetail::RegisterCookedVirtualPath(
                m_assetKindText,
                CookEntryRegistryDetail::ToCookEntryName(entry.virtualPath),
                context.seenVirtualPathHashes
            ))
                return false;

            AssetT asset(assetArena);
            if(!m_buildAsset(entry, asset)){
                if(m_logBuildFailure){
                    NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to build {} '{}'")
                        , m_assetKindText
                        , StringConvert(entry.virtualPath.c_str())
                    );
                }
                return false;
            }

            if(!context.writer.writeCookedAsset(
                m_assetKindText,
                CookEntryRegistryDetail::ToCookEntryName(entry.virtualPath),
                asset,
                codec
            ))
                return false;
        }

        return true;
    }

private:
    bool appendParsedEntry(EntryT&& entry, CookEntryParseContext& context){
        if(!CookEntryRegistryDetail::RegisterParsedVirtualPath(
            m_assetKindText,
            CookEntryRegistryDetail::ToCookEntryName(entry.virtualPath),
            context.seenVirtualPathHashes
        ))
            return false;

        m_entries.push_back(Move(entry));
        return true;
    }

private:
    CookVector<EntryT> m_entries;
    Name m_assetType = NAME_NONE;
    const tchar* m_assetKindText = NWB_TEXT("asset");
    DocumentParseFunction m_parseDocument = nullptr;
    ValueParseFunction m_parseValue = nullptr;
    BuildAssetFunction m_buildAsset = nullptr;
    bool m_logBuildFailure = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CookEntryRegistry : NoCopy{
private:
    using BucketPtr = UniquePtr<ICookEntryBucket>;
    using BucketVector = CookVector<BucketPtr>;
    using BucketLookup = CookMap<Name, ICookEntryBucket*>;

public:
    explicit CookEntryRegistry(CookArena& arena)
        : m_arena(arena)
        , m_buckets(arena)
        , m_lookup(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}

public:
    template<typename EntryT, typename AssetT, typename CodecT>
    bool registerType(
        const Name& assetType,
        const tchar* assetKindText,
        typename CookEntryBucket<EntryT, AssetT, CodecT>::DocumentParseFunction parseDocument,
        typename CookEntryBucket<EntryT, AssetT, CodecT>::ValueParseFunction parseValue,
        typename CookEntryBucket<EntryT, AssetT, CodecT>::BuildAssetFunction buildAsset,
        const bool logBuildFailure = true
    ){
        if(!assetType){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: tried to register an unnamed cook entry type"));
            return false;
        }
        if(!buildAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: cook entry type '{}' has no build function")
                , StringConvert(assetType.c_str())
            );
            return false;
        }

        auto bucket = MakeUnique<CookEntryBucket<EntryT, AssetT, CodecT>>(
            m_arena,
            assetType,
            assetKindText,
            parseDocument,
            parseValue,
            buildAsset,
            logBuildFailure
        );
        ICookEntryBucket* bucketPtr = bucket.get();
        if(!m_lookup.emplace(assetType, bucketPtr).second){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: duplicate cook entry type registration '{}'")
                , StringConvert(assetType.c_str())
            );
            return false;
        }

        m_buckets.push_back(Move(bucket));
        return true;
    }

    [[nodiscard]] ICookEntryBucket* find(const Name& assetType)const{
        const auto found = m_lookup.find(assetType);
        return found == m_lookup.end() ? nullptr : found.value();
    }

    [[nodiscard]] bool has(const Name& assetType)const{
        return find(assetType) != nullptr;
    }

    template<typename EntryT>
    [[nodiscard]] CookVector<EntryT>& entries(const Name& assetType)const{
        ICookEntryBucket* bucket = find(assetType);
        NWB_ASSERT(bucket != nullptr);
        return static_cast<CookEntryBucketTyped<EntryT>*>(bucket)->entries();
    }

    void reserveEntries(const usize entryCount){
        for(BucketPtr& bucket : m_buckets)
            bucket->reserve(entryCount);
    }

    [[nodiscard]] bool parseDocument(
        const Name& assetType,
        const Path& assetRoot,
        const AStringView virtualRoot,
        const Path& nwbFilePath,
        const Core::Metascript::Document& doc,
        CookEntryParseContext& context
    ){
        ICookEntryBucket* bucket = find(assetType);
        if(bucket)
            return bucket->parseDocument(assetRoot, virtualRoot, nwbFilePath, doc, context);

        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: unsupported asset type '{}' in meta '{}'")
            , StringConvert(assetType.c_str())
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    [[nodiscard]] bool parseValue(
        const Name& assetType,
        const Name virtualPath,
        const Path& nwbFilePath,
        const Core::Metascript::Value& asset,
        CookEntryParseContext& context
    ){
        ICookEntryBucket* bucket = find(assetType);
        if(bucket)
            return bucket->parseValue(virtualPath, nwbFilePath, asset, context);

        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: unsupported asset type '{}' in asset_bunch from meta '{}'")
            , StringConvert(assetType.c_str())
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    [[nodiscard]] usize bucketCount()const{
        return m_buckets.size();
    }

    [[nodiscard]] bool writeBucket(const usize bucketIndex, CookEntryWriteContext& context){
        if(bucketIndex >= m_buckets.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: invalid cook entry bucket index {}"), bucketIndex);
            return false;
        }

        return m_buckets[bucketIndex]->writeCookedAssets(context);
    }

    [[nodiscard]] u64 entryCount()const{
        u64 count = 0;
        for(const BucketPtr& bucket : m_buckets){
            const u64 bucketSize = static_cast<u64>(bucket->size());
            if(count > Limit<u64>::s_Max - bucketSize)
                return Limit<u64>::s_Max;
            count += bucketSize;
        }
        return count;
    }

private:
    CookArena& m_arena;
    BucketVector m_buckets;
    BucketLookup m_lookup;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookEntryRegistrationFunction = bool (*)(CookEntryRegistry& registry);

class CookEntryAutoRegistrar final{
public:
    explicit CookEntryAutoRegistrar(CookEntryRegistrationFunction function);
};

[[nodiscard]] bool RegisterAutoCollectedCookEntryTypes(CookEntryRegistry& registry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

