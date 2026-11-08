// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_private.h"
#include "bind_typed_value.h"

#include <impl/assets_material/bind_parameter_application.adhd.h>
#include <impl/assets_material/layout_validation.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ApplyMaterialBindTypedLayoutParameterValue(
    const MaterialBindTypedLayout& layout,
    const Name& materialName,
    const ACompactString& parameterName,
    const AStringView parameterValue,
    Material::ResourceReferenceVector& outResourceReferences,
    Material::TypedBlockByteVector& inOutBlockBytes
){
    const AStringView interfacePath = layout.bindEntry ? AStringView(layout.bindEntry->virtualPath) : AStringView("<unknown>");
    return MaterialBindParameterApplication{}.applyMaterialBindTypedLayoutParameterValueToStorage(
        interfacePath,
        layout.parameterLookup,
        layout.typedLayoutFields,
        materialName,
        parameterName,
        parameterValue,
        outResourceReferences,
        inOutBlockBytes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialBindTypedLayout> BuildMaterialBindTypedLayout(const MaterialBindEntry& bindEntry, const Name& contextName, MaterialCookArena& arena, Core::Alloc::ScratchArena&){
    MaterialBindTypedLayout layout(arena);
    layout.bindEntry = &bindEntry;
    if(!BuildMaterialBindTypedLayoutData(
        AStringView(bindEntry.virtualPath),
        contextName,
        bindEntry.structs,
        bindEntry.instances,
        layout.layoutHash,
        layout.typedLayoutBlocks,
        layout.typedLayoutFields,
        layout.typedBlockBytes,
        layout.blockLookup,
        layout.parameterLookup
    ))
        return MakeUnexpected(Failure{});
    return layout;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NotNull<const MaterialBindTypedLayout*>> FindOrBuildMaterialBindTypedLayout(const Name& materialInterface, const MaterialBindEntry& bindEntry, Core::Alloc::ScratchArena& scratchArena, MaterialBindTypedLayoutCache& inOutCache){
    const auto cacheIt = inOutCache.lookup.find(materialInterface);
    if(cacheIt != inOutCache.lookup.end()){
        const usize cacheIndex = cacheIt.value();
        NWB_ASSERT(cacheIndex < inOutCache.entries.size());
        return MakeNotNull(&inOutCache.entries[cacheIndex]);
    }

    const usize cacheIndex = inOutCache.entries.size();
    auto layout = BuildMaterialBindTypedLayout(bindEntry, materialInterface, inOutCache.entries.get_allocator().arena(), scratchArena);
    if(!layout)
        return MakeUnexpected(Failure{});

    inOutCache.entries.reserve(cacheIndex + 1u);
    if(!inOutCache.lookup.emplace(materialInterface, cacheIndex).second){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate cache entry for interface '{}'"), ConvertString(materialInterface.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    inOutCache.entries.push_back(Move(*layout));
    return MakeNotNull(&inOutCache.entries.back());
}

b8 ApplyMaterialBindTypedLayoutParameters(const MaterialBindTypedLayout& layout, const Name& materialName, const MaterialBindParameterMap& parameters, Material::TypedBlockByteVector& inOutBlockBytes, Material::ResourceReferenceVector& outResourceReferences){
    const AStringView interfacePath = layout.bindEntry ? AStringView(layout.bindEntry->virtualPath) : AStringView("<unknown>");
    return MaterialBindParameterApplication{}.applyMaterialBindTypedLayoutParametersToStorage(
        interfacePath,
        layout.parameterLookup,
        layout.typedLayoutFields,
        materialName,
        parameters,
        inOutBlockBytes,
        outResourceReferences
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

