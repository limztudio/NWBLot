#if defined(NWB_COOK)


#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook{


using MaterialBindInterfaceLookup = HashMap<
    Name,
    const MaterialBindEntry*,
    Hasher<Name>,
    EqualTo<Name>,
    Core::Alloc::ScratchArena
>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void BuildMaterialBindInterfaceLookup(
    const CookVector<MaterialBindEntry>& materialBindEntries,
    MaterialBindInterfaceLookup& outLookup
){
    outLookup.reserve(materialBindEntries.size());
    for(const MaterialBindEntry& bindEntry : materialBindEntries)
        outLookup.emplace(Name(bindEntry.virtualPath.c_str()), &bindEntry);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateMaterialCookInterfaces(
    const CookVector<MaterialBindEntry>& materialBindEntries,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){
    MaterialBindInterfaceLookup materialBindLookup(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    BuildMaterialBindInterfaceLookup(materialBindEntries, materialBindLookup);

    const usize cacheReserveCount = Min(materialBindEntries.size(), materialEntries.size());
    MaterialBindTypedLayoutCache layoutCache(materialEntries.get_allocator().arena());
    layoutCache.reserve(cacheReserveCount);

    for(MaterialCookEntry& materialEntry : materialEntries){
        materialEntry.typedLayoutHash = 0u;
        materialEntry.typedLayoutBlocks.clear();
        materialEntry.typedLayoutFields.clear();
        materialEntry.typedBlockBytes.clear();

        if(materialEntry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' is missing required material interface")
                , StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }

        // The interface is stored as text; build the Name hash key the bind lookup + typed-layout machinery need.
        const Name materialInterfaceName(AStringView(materialEntry.materialInterface));
        const auto bindEntryIt = materialBindLookup.find(materialInterfaceName);
        if(bindEntryIt == materialBindLookup.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' references unknown material interface '{}'")
                , StringConvert(materialEntry.virtualPath.c_str())
                , StringConvert(materialEntry.materialInterface.c_str())
            );
            return false;
        }
        const MaterialBindEntry* bindEntry = bindEntryIt.value();

        const MaterialBindTypedLayout* layout = nullptr;
        if(!FindOrBuildMaterialBindTypedLayout(
            materialInterfaceName,
            *bindEntry,
            layoutCache,
            layout,
            scratchArena
        ))
            return false;
        if(!layout){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' failed to resolve typed layout cache for interface '{}'")
                , StringConvert(materialEntry.virtualPath.c_str())
                , StringConvert(materialEntry.materialInterface.c_str())
            );
            return false;
        }

        CopyMaterialBindTypedLayoutDefaults(
            *layout,
            materialEntry.typedLayoutHash,
            materialEntry.typedLayoutBlocks,
            materialEntry.typedLayoutFields,
            materialEntry.typedBlockBytes
        );
        if(!ApplyMaterialBindTypedLayoutParameters(
            *layout,
            Name(AStringView(materialEntry.virtualPath)),
            materialEntry.parameters,
            materialEntry.typedBlockBytes
        ))
            return false;
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

