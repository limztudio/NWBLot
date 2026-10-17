// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MaterialBindInterfaceLookup = HashMap<Name, const MaterialBindEntry*, Core::Alloc::ScratchArena, Hasher<Name>, EqualTo<Name>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialBindInterfaceLookup> BuildMaterialBindInterfaceLookup(const CookVector<MaterialBindEntry>& materialBindEntries, ScratchArena& scratchArena){
    MaterialBindInterfaceLookup lookup(0, Hasher<Name>(), EqualTo<Name>(), scratchArena);
    lookup.reserve(materialBindEntries.size());
    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        const auto interfaceName = Name::FromText(AStringView(bindEntry.virtualPath));
        if(!interfaceName)
            return MakeUnexpected(Failure{});
        if(!lookup.emplace(*interfaceName, &bindEntry).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind interface '{}' is duplicated"), StringConvert(AStringView(bindEntry.virtualPath)));
            return MakeUnexpected(Failure{});
        }
    }
    return lookup;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateMaterialCookInterfaces(const CookVector<MaterialBindEntry>& materialBindEntries, CookVector<MaterialCookEntry>& materialEntries, ScratchArena& scratchArena){
    auto materialBindLookupResult = BuildMaterialBindInterfaceLookup(materialBindEntries, scratchArena);
    if(!materialBindLookupResult)
        return false;
    const MaterialBindInterfaceLookup& materialBindLookup = *materialBindLookupResult;

    const usize cacheReserveCount = Min(materialBindEntries.size(), materialEntries.size());
    MaterialBindTypedLayoutCache layoutCache(materialEntries.get_allocator().arena());
    layoutCache.reserve(cacheReserveCount);

    for(MaterialCookEntry& materialEntry : materialEntries){
        materialEntry.typedLayoutHash = 0u;
        materialEntry.typedLayoutBlocks.clear();
        materialEntry.typedLayoutFields.clear();
        materialEntry.typedBlockBytes.clear();
        materialEntry.resourceReferences.clear();

        if(materialEntry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' is missing required material interface"), StringConvert(AStringView(materialEntry.virtualPath)));
            return false;
        }

        const auto materialInterfaceNameResult = Name::FromText(AStringView(materialEntry.materialInterface));
        if(!materialInterfaceNameResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' references unknown material interface '{}'"), StringConvert(AStringView(materialEntry.virtualPath)), StringConvert(AStringView(materialEntry.materialInterface)));
            return false;
        }
        const Name materialInterfaceName = *materialInterfaceNameResult;
        const auto bindEntryIt = materialBindLookup.find(materialInterfaceName);
        if(bindEntryIt == materialBindLookup.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' references unknown material interface '{}'"), StringConvert(AStringView(materialEntry.virtualPath)), StringConvert(AStringView(materialEntry.materialInterface)));
            return false;
        }
        const MaterialBindEntry* bindEntry = bindEntryIt.value();

        const auto layoutResult = FindOrBuildMaterialBindTypedLayout(materialInterfaceName, *bindEntry, layoutCache, scratchArena);
        if(!layoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' failed to resolve typed layout cache for interface '{}'"), StringConvert(AStringView(materialEntry.virtualPath)), StringConvert(AStringView(materialEntry.materialInterface)));
            return false;
        }

        const MaterialBindTypedLayout* layout = *layoutResult;
        CopyMaterialBindTypedLayoutDefaults(*layout, materialEntry.typedLayoutHash, materialEntry.typedLayoutBlocks, materialEntry.typedLayoutFields, materialEntry.typedBlockBytes);
        const auto materialName = Name::FromText(AStringView(materialEntry.virtualPath));
        if(!materialName)
            return false;
        if(!ApplyMaterialBindTypedLayoutParameters(*layout, *materialName, materialEntry.parameters, materialEntry.typedBlockBytes, materialEntry.resourceReferences))
            return false;
        if(!MaterialBinaryPayload::ValidateMaterialResourceReferences(materialEntry.typedLayoutBlocks, materialEntry.typedLayoutFields, materialEntry.resourceReferences)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}': resource fields must be assigned engine or project asset paths in parameters"), StringConvert(AStringView(materialEntry.virtualPath)));
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

