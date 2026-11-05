// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_private.h"
#include "bind_typed_value.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialBlockClass::Enum> ParseMaterialBindBlockClass(const MaterialBindStruct& bindStruct)noexcept{
    if(bindStruct.findAttribute(s_MaterialConstantAttribute))
        return MaterialBlockClass::MaterialConstant;
    if(bindStruct.findAttribute(s_MaterialMutableAttribute))
        return MaterialBlockClass::MaterialMutable;
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 ReserveMaterialBindTypedLayoutVectors(const MaterialBindEntry& bindEntry, const Name& contextName, const Span<const NotNull<const MaterialBindInstance*>> sortedInstances, MaterialBindTypedLayout& outLayout){
    usize fieldReserveCount = 0u;
    usize byteReserveCount = 0u;
    for(const NotNull<const MaterialBindInstance*> instance : sortedInstances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance->type));
        if(!bindStruct)
            continue;

        if(fieldReserveCount > Limit<usize>::s_Max - bindStruct->fields.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' exceeds supported field count for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(contextName.resolvedText()));
            return false;
        }
        fieldReserveCount += bindStruct->fields.size();

        for(const MaterialBindField& bindField : bindStruct->fields){
            usize fieldByteReserve = sizeof(UInt4U);
            if(const Expected<MaterialLayoutFieldType::Enum> fieldType = TryParseMaterialLayoutFieldTypeText(AStringView(bindField.type))){
                const u32 fieldByteSize = MaterialLayoutFieldByteSize(*fieldType);
                if(fieldByteSize != 0u)
                    fieldByteReserve = fieldByteSize;
            }

            if(byteReserveCount > Limit<usize>::s_Max - fieldByteReserve){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' exceeds supported byte count for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(contextName.resolvedText()));
                return false;
            }
            byteReserveCount += fieldByteReserve;
        }
    }

    outLayout.typedLayoutBlocks.reserve(sortedInstances.size());
    outLayout.typedLayoutFields.reserve(fieldReserveCount);
    outLayout.typedBlockBytes.reserve(byteReserveCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<ScratchVector<NotNull<const MaterialBindInstance*>>> BuildSortedMaterialBindInstances(const MaterialBindEntry& bindEntry, const Name& contextName, ScratchArena& arena){
    ScratchVector<NotNull<const MaterialBindInstance*>> instances(arena);
    instances.reserve(bindEntry.instances.size());
    for(const MaterialBindInstance& instance : bindEntry.instances)
        instances.push_back(MakeNotNull(&instance));
    Sort(instances.begin(), instances.end(), [](const NotNull<const MaterialBindInstance*> lhs, const NotNull<const MaterialBindInstance*> rhs)noexcept{
        return lhs->name < rhs->name;
    });
    if(instances.size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' exceeds supported block count for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(contextName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    return instances;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 BuildMaterialBindTypedLayoutBlockLookup(const AStringView contextLabel, MaterialBindTypedLayout& inOutLayout){
    return BuildMaterialBindTypedLayoutBlockLookup(contextLabel, inOutLayout.typedLayoutBlocks, inOutLayout.blockLookup);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 BuildMaterialBindTypedLayoutParameterLookup(const AStringView contextLabel, MaterialBindTypedLayout& inOutLayout){
    inOutLayout.parameterLookup.clear();
    inOutLayout.parameterLookup.reserve(inOutLayout.typedLayoutFields.size());

    const MaterialBindEntry& bindEntry = *inOutLayout.bindEntry;
    for(const MaterialBindInstance& instance : bindEntry.instances){
        const MaterialBindStruct& bindStruct = *MakeNotNull(bindEntry.findStruct(AStringView(instance.type)));

        const auto instanceName = Name::FromText(AStringView(instance.name));
        NWB_ASSERT(instanceName);
        const auto blockIt = inOutLayout.blockLookup.find(*instanceName);
        NWB_ASSERT(blockIt != inOutLayout.blockLookup.end());

        const MaterialBindTypedLayoutBlockLookupEntry& blockEntry = blockIt.value();
        NWB_ASSERT(blockEntry.blockIndex < inOutLayout.typedLayoutBlocks.size());

        const MaterialTypedLayoutBlock& block = inOutLayout.typedLayoutBlocks[blockEntry.blockIndex];
        NWB_ASSERT(block.fieldCount == bindStruct.fields.size());

        for(u32 fieldOffset = 0u; fieldOffset < block.fieldCount; ++fieldOffset){
            const usize fieldIndex = static_cast<usize>(block.fieldBegin) + fieldOffset;
            NWB_ASSERT(fieldIndex < inOutLayout.typedLayoutFields.size());
            const MaterialBindField& bindField = bindStruct.fields[fieldOffset];
            const MaterialTypedLayoutField& field = inOutLayout.typedLayoutFields[fieldIndex];
            NWB_ASSERT(Name::FromText(AStringView(bindField.name)) == field.fieldName);
            if(!TryInsertMaterialBindTypedLayoutParameterLookupEntry(
                AStringView(bindEntry.virtualPath),
                contextLabel,
                AStringView(instance.name),
                AStringView(bindStruct.name),
                AStringView(bindField.name),
                blockEntry,
                block,
                fieldIndex,
                field,
                inOutLayout.parameterLookup
            ))
                return false;
        }
    }

    return true;
}


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
    return ApplyMaterialBindTypedLayoutParameterValueToStorage(
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


Expected<MaterialBindTypedLayout> BuildMaterialBindTypedLayout(const MaterialBindEntry& bindEntry, const Name& contextName, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace MaterialBindDetail;
    MaterialBindTypedLayout layout(arena);
    layout.bindEntry = &bindEntry;

    const auto sortedInstances = BuildSortedMaterialBindInstances(bindEntry, contextName, scratchArena);
    if(!sortedInstances)
        return MakeUnexpected(Failure{});
    if(!ReserveMaterialBindTypedLayoutVectors(bindEntry, contextName, *sortedInstances, layout))
        return MakeUnexpected(Failure{});

    u32 constantTypedByteSize = 0u;
    for(const NotNull<const MaterialBindInstance*> instance : *sortedInstances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance->type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' instance '{}' references unknown " "struct type '{}' for '{}'")
                , ConvertString(bindEntry.virtualPath)
                , ConvertString(instance->name)
                , ConvertString(instance->type)
                , ConvertString(contextName.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }

        const auto blockClassResult = ParseMaterialBindBlockClass(*bindStruct);
        if(!blockClassResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' struct '{}' is missing " "a material block class for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(bindStruct->name), ConvertString(contextName.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        const MaterialBlockClass::Enum blockClass = *blockClassResult;
        if(layout.typedLayoutFields.size() > Limit<u32>::s_Max || bindStruct->fields.size() > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds " "supported field count for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(instance->name), ConvertString(contextName.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        const usize blockByteBegin = layout.typedBlockBytes.size();

        MaterialTypedLayoutBlock block;
        const auto blockName = Name::FromText(AStringView(instance->name));
        if(!blockName || !*blockName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' has invalid block '{}' for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(instance->name), ConvertString(contextName.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        block.blockName = *blockName;
        block.blockClass = blockClass;
        block.fieldBegin = static_cast<u32>(layout.typedLayoutFields.size());
        block.fieldCount = static_cast<u32>(bindStruct->fields.size());
        block.byteSize = 0u;

        for(const MaterialBindField& bindField : bindStruct->fields){
            if(!TryAppendMaterialBindTypedLayoutField(
                AStringView(bindEntry.virtualPath),
                contextName,
                AStringView(instance->name),
                AStringView(bindField.name),
                AStringView(bindField.type),
                bindField.defaultArgument(),
                blockClass,
                blockByteBegin,
                block.byteSize,
                layout.typedLayoutFields,
                layout.typedBlockBytes
            ))
                return MakeUnexpected(Failure{});
        }

        if(!TryFinalizeMaterialBindTypedLayoutBlock(
            AStringView(bindEntry.virtualPath),
            contextName,
            AStringView(instance->name),
            blockByteBegin,
            block,
            constantTypedByteSize,
            layout.typedLayoutBlocks,
            layout.typedBlockBytes
        ))
            return MakeUnexpected(Failure{});
    }

    layout.layoutHash = ComputeMaterialTypedLayoutHash(layout.typedLayoutBlocks, layout.typedLayoutFields);
    if(layout.layoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' produced an empty layout for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(contextName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    const Expected<usize> expectedBlockByteSize = ComputeMaterialTypedBlockByteSize(layout.typedLayoutBlocks);
    if(!expectedBlockByteSize || *expectedBlockByteSize != layout.typedBlockBytes.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' has inconsistent block storage for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(contextName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    if(!BuildMaterialBindTypedLayoutBlockLookup(contextName.resolvedText(), layout))
        return MakeUnexpected(Failure{});
    if(!BuildMaterialBindTypedLayoutParameterLookup(contextName.resolvedText(), layout))
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
    return ApplyMaterialBindTypedLayoutParametersToStorage(
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
