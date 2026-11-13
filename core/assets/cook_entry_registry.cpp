// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_entry_registry.h"
#include "arena_names.h"

#include <global/assert.h>
#include <global/auto_registration.h>
#include <global/overflow.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CookEntryRegistryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename AssetKindArg>
b8 RegisterParsedVirtualPathImpl(const AssetKindArg& assetKind, const Name& virtualPath, CookEntryPathHashSet& inOutSeenVirtualPathHashes){
    if(!virtualPath)
        return true;

    const NameHash pathHash = virtualPath.hash();
    if(inOutSeenVirtualPathHashes.insert(pathHash).second)
        return true;

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: duplicate property asset virtual path '{}' for {}"), ConvertString(virtualPath.resolvedText()), assetKind);
    return false;
}

template<typename AssetKindArg>
b8 RegisterCookedVirtualPathImpl(const AssetKindArg& assetKind, const Name& virtualPath, CookEntryPathHashSet& inOutSeenVirtualPathHashes){
    if(!virtualPath){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: invalid {} virtual path"), assetKind);
        return false;
    }

    const NameHash virtualPathHash = virtualPath.hash();
    if(inOutSeenVirtualPathHashes.insert(virtualPathHash).second)
        return true;

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: duplicate {} virtual path '{}'"), assetKind, ConvertString(virtualPath.resolvedText()));
    return false;
}

b8 RegisterParsedVirtualPath(const TStringView assetKind, const Name& virtualPath, CookEntryPathHashSet& inOutSeenVirtualPathHashes){
    return RegisterParsedVirtualPathImpl(assetKind, virtualPath, inOutSeenVirtualPathHashes);
}

b8 RegisterCookedVirtualPath(const TStringView assetKind, const Name& virtualPath, CookEntryPathHashSet& inOutSeenVirtualPathHashes){
    return RegisterCookedVirtualPathImpl(assetKind, virtualPath, inOutSeenVirtualPathHashes);
}

#if defined(FOUNDATION_UNICODE)
b8 RegisterParsedVirtualPath(const AStringView assetKind, const Name& virtualPath, CookEntryPathHashSet& inOutSeenVirtualPathHashes){
    return RegisterParsedVirtualPathImpl(ConvertString(assetKind), virtualPath, inOutSeenVirtualPathHashes);
}

b8 RegisterCookedVirtualPath(const AStringView assetKind, const Name& virtualPath, CookEntryPathHashSet& inOutSeenVirtualPathHashes){
    return RegisterCookedVirtualPathImpl(ConvertString(assetKind), virtualPath, inOutSeenVirtualPathHashes);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_UNICODE)
namespace __hidden_cook_entry_registry{


template<typename Fn>
b8 WithConvertedAssetKind(const AStringView assetKind, Fn&& fn){
    ScratchArena scratchArena(AssetsArenaScope::s_CookEntryRegistryScratch);
    const TString convertedAssetKind = ConvertString(scratchArena, assetKind);
    return fn(TStringView(convertedAssetKind));
}


};

b8 ICookedAssetWriter::writeCookedAsset(const AStringView assetKind, const Name& virtualPath, const IAsset& asset, const IAssetCodec& codec, const CookEntrySourceLocation& location){
    return __hidden_cook_entry_registry::WithConvertedAssetKind(assetKind, [&](const TStringView convertedAssetKind){
        return writeCookedAsset(convertedAssetKind, virtualPath, asset, codec, location);
    });
}

b8 ICookedAssetWriter::writeCachedCookedAsset(const AStringView assetKind, const Name& virtualPath, const IAssetCodec& codec, const Span<const u8> payloadBytes){
    return __hidden_cook_entry_registry::WithConvertedAssetKind(assetKind, [&](const TStringView convertedAssetKind){
        return writeCachedCookedAsset(convertedAssetKind, virtualPath, codec, payloadBytes);
    });
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ICookEntryBucket* CookEntryRegistry::find(const Name& assetType)const noexcept{
    const auto found = m_lookup.find(assetType);
    return found == m_lookup.end() ? nullptr : found.value().get();
}

b8 CookEntryRegistry::has(const Name& assetType)const noexcept{
    return find(assetType) != nullptr;
}

b8 CookEntryRegistry::parseDocument(const Name& assetType, const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, CookEntryParseContext& context){
    ICookEntryBucket* bucket = find(assetType);
    if(bucket)
        return bucket->parseDocument(assetRoot, virtualRoot, nwbFilePath, doc, context);

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: unsupported asset type '{}' in meta '{}'"), ConvertString(assetType.resolvedText()), PathToString<tchar>(nwbFilePath));
    return false;
}

b8 CookEntryRegistry::parseValue(const Name& assetType, const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, CookEntryParseContext& context){
    ICookEntryBucket* bucket = find(assetType);
    if(bucket)
        return bucket->parseValue(virtualPath, nwbFilePath, asset, context);

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: unsupported asset type '{}' in asset_bunch from meta '{}'"), ConvertString(assetType.resolvedText()), PathToString<tchar>(nwbFilePath));
    return false;
}

b8 CookEntryRegistry::appendCachedEntry(const Name& assetType, const CookEntrySourceLocation& location, const Name& virtualPath, const Span<const u8> payloadBytes, CookEntryParseContext& context){
    ICookEntryBucket* bucket = find(assetType);
    if(bucket)
        return bucket->appendCachedEntry(location, virtualPath, payloadBytes, context);

    return false;
}

usize CookEntryRegistry::bucketCount()const noexcept{
    return m_buckets.size();
}

b8 CookEntryRegistry::writeBucket(const usize bucketIndex, CookEntryWriteContext& context){
    if(bucketIndex >= m_buckets.size()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: invalid cook entry bucket index {}"), bucketIndex);
        return false;
    }

    return m_buckets[bucketIndex]->writeCookedAssets(context);
}

u64 CookEntryRegistry::entryCount()const noexcept{
    u64 count = 0;
    for(const BucketPtr& bucket : m_buckets){
        const u64 bucketSize = static_cast<u64>(bucket->size());
        const auto total = AddNoOverflow(count, bucketSize);
        if(!total)
            return Limit<u64>::s_Max;
        count = *total;
    }
    return count;
}

b8 CookEntryRegistry::registerBucket(const Name& assetType, BucketPtr owner){
    if(!owner){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: bucket allocation returned null for '{}'"), ConvertString(assetType.resolvedText()));
        return false;
    }

    const NotNull<ICookEntryBucket*> bucket(owner.get());
    m_buckets.push_back(Move(owner));
    {
        ScopeExit rollback([&]()noexcept{
            owner = Move(m_buckets.back());
            m_buckets.pop_back();
        });
        if(m_lookup.emplace(assetType, bucket).second){
            rollback.release();
            return true;
        }
    }

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: duplicate cook entry type registration '{}'"), ConvertString(assetType.resolvedText()));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CookEntryAutoRegistrar::CookEntryAutoRegistrar(const CookEntryRegistrationFunction function){
    ::RegisterAutoFactory(QueryAssetAutoRegistrationQueue<NotNull<CookEntryRegistrationFunction>, AssetsArenaScope::s_AutoRegistrationQueueArena>(), function);
}

b8 RegisterAutoCollectedCookEntryTypes(CookEntryRegistry& registry, ScratchArena& scratchArena){
    Vector<NotNull<CookEntryRegistrationFunction>, Core::Alloc::ScratchArena> functions{scratchArena};
    return RunAutoCollectedFunctions(
        QueryAssetAutoRegistrationQueue<NotNull<CookEntryRegistrationFunction>, AssetsArenaScope::s_AutoRegistrationQueueArena>(), registry, functions, [](){ NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: collected null cook entry registration function")); },
        [](){ NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: failed to register auto-collected cook entry type")); }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

