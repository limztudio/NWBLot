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


template<typename SubsystemT>
static Expected<SubsystemCookEntry<SubsystemT>> ParseSubsystemDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSubsystemCookMetadata<SubsystemT>(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

template<typename SubsystemT>
static Expected<SubsystemCookEntry<SubsystemT>> ParseSubsystemValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSubsystemCookMetadata<SubsystemT>(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

template<typename SubsystemT>
static bool ExtractSubsystemDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto subsystemEntry = context.document
        ? ParseSubsystemCookMetadata<SubsystemT>(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena)
        : ParseSubsystemCookMetadata<SubsystemT>(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena)
    ;
    if(!subsystemEntry)
        return false;

    for(const ISubsystem::SubsystemRef& childSubsystem : subsystemEntry->subsystem.subsystems()){
        if(!collector.addSubsystemDependency(childSubsystem.name()))
            return false;
    }

    const auto descriptors = TypedSubsystem<SubsystemT>::FieldDescriptors();
    for(const SubsystemFieldDescriptor<SubsystemT>& descriptor : descriptors){
        const Name assetName = descriptor.get(subsystemEntry->subsystem);
        Name expectedType = s_NameNone;
        if(!descriptor.expectedAssetTypeText.empty()){
            const auto parsedType = Name::FromText(descriptor.expectedAssetTypeText);
            if(!parsedType)
                return false;
            expectedType = *parsedType;
        }
        if(!collector.addAssetDependency(assetName, expectedType))
            return false;
    }
    return true;
}

static bool RegisterSubsystemCookEntry(Core::Assets::CookEntryRegistry& registry){
#define NWB_REGISTER_SUBSYSTEM_COOK_ENTRY(Type) \
    if(!Core::Assets::RegisterDocumentValueCookEntry<SubsystemCookEntry<Type>, Type, Type##AssetCodec>( \
        registry, \
        SubsystemBinaryPayload::s_SubsystemAssetKindLabel, \
        &ParseSubsystemDocument<Type>, \
        &ParseSubsystemValue<Type>, \
        &BuildSubsystemAsset<Type> \
    )) \
        return false;
    NWB_SUBSYSTEM_ASSET_TYPE_ENTRIES(NWB_REGISTER_SUBSYSTEM_COOK_ENTRY)
#undef NWB_REGISTER_SUBSYSTEM_COOK_ENTRY
    return true;
}

static bool RegisterSubsystemCookDependency(Core::Assets::CookDependencyRegistry& registry){
#define NWB_REGISTER_SUBSYSTEM_COOK_DEPENDENCY(Type) \
    if(!registry.registerSubsystemType(Type::AssetTypeName(), &ExtractSubsystemDependencies<Type>)) \
        return false;
    NWB_SUBSYSTEM_ASSET_TYPE_ENTRIES(NWB_REGISTER_SUBSYSTEM_COOK_DEPENDENCY)
#undef NWB_REGISTER_SUBSYSTEM_COOK_DEPENDENCY
    return true;
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

