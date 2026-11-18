// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#if defined(NWB_COOK)
#include "cook_metadata.h"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Asset>
struct PreparedAssetCookEntry{
    Name virtualPath;
    Asset asset;


public:
    PreparedAssetCookEntry(AssetArena& arena, const Name path)
        : virtualPath(path)
        , asset(arena, path)
    {}
};

template<typename Family, typename Asset, typename Codec, typename Policy>
class FamilyCookRegistration;


#if defined(NWB_COOK)

template<typename Family, typename Asset, typename Codec, typename Policy>
class FamilyCookRegistration final{
private:
    using Entry = PreparedAssetCookEntry<Asset>;


    [[nodiscard]] static Expected<Entry> Read(const Name path, const Path& file, const Core::Metascript::Value& value, AssetArena& arena, ScratchArena&){
        const Policy policy;
        if(!CheckMetadataAssetMap(file, value, policy.prefix()))
            return MakeUnexpected(Failure{});
        Entry entry(arena, path);
        if(!AssignCookEntryVirtualPath(path, file, policy.prefix(), entry) || !policy.read(file, value, entry.asset))
            return MakeUnexpected(Failure{});
        return entry;
    }

    [[nodiscard]] static b8 RegisterEntry(CookEntryRegistry& registry){
        const Policy policy;
        policy.registerType(Asset::AssetTypeName());
        ScratchArena scratch;
        const auto label = ConvertString(scratch, policy.kind());
        return RegisterDocumentValueCookEntry<Entry, Asset, Codec>(
            label,
            [](const Path& root, const AStringView virtualRoot, const Path& file, const Core::Metascript::Document& doc, CookEntryParseContext& context){
                return ParseCookMetadataDocument<Entry, &Read>(root, virtualRoot, file, doc, Policy{}.prefix(), context.cookArena, context.scratchArena);
            },
            [](const Name path, const Path& file, const Core::Metascript::Value& value, CookEntryParseContext& context){
                return Read(path, file, value, context.cookArena, context.scratchArena);
            },
            [](Entry& entry, AssetArena&, ScratchArena&) -> Expected<Asset>{
                if(!Policy{}.validate(entry.asset))
                    return MakeUnexpected(Failure{});
                return Move(entry.asset);
            },
            true, registry
        );
    }

    [[nodiscard]] static b8 RegisterDependency(CookDependencyRegistry& registry){
        const Policy policy;
        policy.registerType(Asset::AssetTypeName());
        return registry.registerType(Asset::AssetTypeName(), [](const CookDependencyItemContext& context, ICookDependencyCollector& collector){
            const auto entry = ParseCookDependencyEntry<Entry, &Read>(context, Policy{}.prefix());
            return entry && Policy{}.collect(entry->asset, collector);
        }, Family::AssetTypeName());
    }


public:
    FamilyCookRegistration() : m_entry(&RegisterEntry), m_dependency(&RegisterDependency){}


private:
    CookEntryAutoRegistrar m_entry;
    CookDependencyAutoRegistrar m_dependency;
};

#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

