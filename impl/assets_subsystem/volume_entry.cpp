// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_subsystem_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<SubsystemCookEntry> ParseSubsystemDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSubsystemCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static Expected<SubsystemCookEntry> ParseSubsystemValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSubsystemCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

static bool ExtractSubsystemDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto subsystemEntry = context.document
        ? ParseSubsystemCookMetadata(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena)
        : ParseSubsystemCookMetadata(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    if(!subsystemEntry)
        return false;

    for(const Subsystem::SubsystemRef& childSubsystem : subsystemEntry->subsystems){
        if(!collector.addSubsystemDependency(childSubsystem.name()))
            return false;
    }

    Vector<Pair<Name, Name>, Core::Alloc::ScratchArena> sortedAssets(context.scratchArena);
    sortedAssets.reserve(subsystemEntry->assets.size());
    for(const auto& [nickname, assetRef] : subsystemEntry->assets)
        sortedAssets.emplace_back(nickname, assetRef.name());
    Sort(sortedAssets.begin(), sortedAssets.end(), [](const Pair<Name, Name>& lhs, const Pair<Name, Name>& rhs)noexcept{
        if(lhs.first() != rhs.first())
            return lhs.first() < rhs.first();
        return lhs.second() < rhs.second();
    });
    for(const Pair<Name, Name>& entry : sortedAssets){
        if(!collector.addAssetDependency(entry.second()))
            return false;
    }
    return true;
}

static bool RegisterSubsystemCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<SubsystemCookEntry, Subsystem, SubsystemAssetCodec>(
        registry,
        SubsystemBinaryPayload::s_SubsystemAssetKindLabel,
        &ParseSubsystemDocument,
        &ParseSubsystemValue,
        &BuildSubsystemAsset
    );
}

static bool RegisterSubsystemCookDependency(Core::Assets::CookDependencyRegistry& registry){
    return registry.registerType(Subsystem::AssetTypeName(), &ExtractSubsystemDependencies);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_SubsystemCookEntryRegistrar, RegisterSubsystemCookEntry);
NWB_DEFINE_COOK_DEPENDENCY_REGISTRAR(s_SubsystemCookDependencyRegistrar, RegisterSubsystemCookDependency);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

