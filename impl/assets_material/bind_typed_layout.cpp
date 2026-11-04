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


static Expected<UInt4U> BuildMaterialTypedLayoutDefaultValue(const Name& materialName, const MaterialBindInstance& instance, const MaterialBindField& bindField, const MaterialLayoutFieldType::Enum fieldType){

    // Keep slots zero until the renderer resolves MaterialResourceReference against its live descriptor heap.
    if(IsMaterialLayoutResourceFieldType(fieldType))
        return UInt4U{};

    const Expected<ACompactString> key = BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(bindField.name));
    if(!key){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: field '{}.{}' for '{}' exceeds ACompactString capacity"), ConvertString(instance.name), ConvertString(bindField.name), ConvertString(materialName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    ACompactString defaultText;
    const AStringView defaultArgument = bindField.defaultArgument();
    if(!defaultText.assign(defaultArgument)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default for '{}.{}' in '{}' exceeds ACompactString capacity"), ConvertString(instance.name), ConvertString(bindField.name), ConvertString(materialName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    const auto defaultParameter = ParseMaterialTypedValue(*key, defaultText);
    if(!defaultParameter){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default '{}' for '{}.{}' in '{}' is invalid"), ConvertString(defaultArgument), ConvertString(instance.name), ConvertString(bindField.name), ConvertString(materialName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    const MaterialLayoutFieldType::Enum defaultFieldType = MaterialLayoutFieldTypeFromParameterType(static_cast<MaterialParameterValueType::Enum>(defaultParameter->meta.z), defaultParameter->meta.w);
    if(defaultFieldType != fieldType){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default '{}' for '{}.{}' in '{}' " "does not match field type '{}'")
            , ConvertString(defaultArgument)
            , ConvertString(instance.name)
            , ConvertString(bindField.name)
            , ConvertString(materialName.resolvedText())
            , ConvertString(bindField.type)
        );
        return MakeUnexpected(Failure{});
    }

    return MaterialTypedValueToLayoutValue(*defaultParameter);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The builder admits only field types with a nonzero byte size.
static void AppendMaterialTypedLayoutFieldBytes(const MaterialLayoutFieldType::Enum fieldType, const UInt4U& value, Material::TypedBlockByteVector& inOutBlockBytes){
    const u32 byteSize = MaterialLayoutFieldByteSize(fieldType);
    NWB_ASSERT(byteSize != 0u && byteSize <= sizeof(value));

    const usize oldSize = inOutBlockBytes.size();
    inOutBlockBytes.resize(oldSize + byteSize, 0u);
    NWB_MEMCPY(inOutBlockBytes.data() + oldSize, byteSize, &value, byteSize);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Field offsets only grow inside a block, so padding never shrinks the bytes.
static void PadMaterialTypedLayoutBytesTo(const usize targetByteSize, Material::TypedBlockByteVector& inOutBlockBytes){
    NWB_ASSERT(targetByteSize >= inOutBlockBytes.size());
    if(targetByteSize > inOutBlockBytes.capacity())
        inOutBlockBytes.reserve(targetByteSize);
    inOutBlockBytes.resize(targetByteSize, 0u);
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
    inOutLayout.blockLookup.clear();
    inOutLayout.blockLookup.reserve(inOutLayout.typedLayoutBlocks.size());

    usize blockByteBegin = 0u;
    u32 constantByteBegin = 0u;
    for(usize blockIndex = 0u; blockIndex < inOutLayout.typedLayoutBlocks.size(); ++blockIndex){
        const MaterialTypedLayoutBlock& block = inOutLayout.typedLayoutBlocks[blockIndex];
        NWB_ASSERT(blockIndex < Limit<u32>::s_Max);
        if(blockByteBegin > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: block byte offset exceeds u32 for '{}'"), ConvertString(contextLabel));
            return false;
        }

        const MaterialBindTypedLayoutBlockLookupEntry entry{ static_cast<u32>(blockIndex), static_cast<u32>(blockByteBegin), constantByteBegin };
        if(!inOutLayout.blockLookup.emplace(block.blockName, entry).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate block in '{}'"), ConvertString(contextLabel));
            return false;
        }
        NWB_ASSERT(static_cast<usize>(block.byteSize) <= Limit<usize>::s_Max - blockByteBegin);

        blockByteBegin += block.byteSize;
        if(block.blockClass == MaterialBlockClass::MaterialConstant){
            NWB_ASSERT(block.byteSize <= Limit<u32>::s_Max - constantByteBegin);
            constantByteBegin += block.byteSize;
        }
    }

    return true;
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
            if(fieldIndex > Limit<u32>::s_Max){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' instance '{}' " "field range exceeds layout for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(instance.name), ConvertString(contextLabel));
                return false;
            }

            const MaterialBindField& bindField = bindStruct.fields[fieldOffset];
            const MaterialTypedLayoutField& field = inOutLayout.typedLayoutFields[fieldIndex];
            NWB_ASSERT(Name::FromText(AStringView(bindField.name)) == field.fieldName);
            if(field.offset > Limit<u32>::s_Max - blockEntry.byteBegin){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' " "byte offset exceeds u32 for '{}'")
                    , ConvertString(bindEntry.virtualPath)
                    , ConvertString(bindStruct.name)
                    , ConvertString(bindField.name)
                    , ConvertString(contextLabel)
                );
                return false;
            }
            // Resource fields live in constant blocks, whose total size the builder already bounded to u32.
            NWB_ASSERT(!IsMaterialLayoutResourceFieldType(field.fieldType) || field.offset <= Limit<u32>::s_Max - blockEntry.constantByteBegin);

            const Expected<ACompactString> parameterName = BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(bindField.name));
            if(!parameterName){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: field '{}.{}' for '{}' exceeds ACompactString capacity"), ConvertString(instance.name), ConvertString(bindField.name), ConvertString(contextLabel));
                return false;
            }

            MaterialBindTypedLayoutParameterLookupEntry entry;
            entry.fieldIndex = static_cast<u32>(fieldIndex);
            entry.byteOffset = blockEntry.byteBegin + field.offset;
            entry.constantByteOffset = blockEntry.constantByteBegin + field.offset;
            entry.blockName = block.blockName;
            if(!inOutLayout.parameterLookup.emplace(*parameterName, entry).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate parameter '{}' in '{}'"), ConvertString(parameterName->view()), ConvertString(contextLabel));
                return false;
            }
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 WriteMaterialTypedLayoutFieldBytes(const usize byteOffset, const MaterialLayoutFieldType::Enum fieldType, const UInt4U& value, Material::TypedBlockByteVector& inOutBlockBytes)noexcept{
    const u32 byteSize = MaterialLayoutFieldByteSize(fieldType);
    if(byteSize == 0u || byteSize > sizeof(value))
        return false;
    if(byteOffset > inOutBlockBytes.size() || byteSize > inOutBlockBytes.size() - byteOffset)
        return false;

    NWB_MEMCPY(inOutBlockBytes.data() + byteOffset, byteSize, &value, byteSize);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<UInt4U> ParseMaterialTypedLayoutParameterValue(const Name& materialName, const AStringView parameterName, const AStringView parameterValue, const MaterialTypedLayoutField& field){

    const auto parameter = ParseMaterialTypedValue(parameterName, parameterValue);
    if(!parameter){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' for '{}' has invalid value '{}'"), ConvertString(parameterName), ConvertString(materialName.resolvedText()), ConvertString(parameterValue));
        return MakeUnexpected(Failure{});
    }

    const MaterialLayoutFieldType::Enum parameterFieldType = MaterialLayoutFieldTypeFromParameterType(static_cast<MaterialParameterValueType::Enum>(parameter->meta.z), parameter->meta.w);
    if(parameterFieldType != field.fieldType){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' for '{}' does not match interface field type"), ConvertString(parameterName), ConvertString(materialName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    return MaterialTypedValueToLayoutValue(*parameter);
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
    const auto parameterIt = layout.parameterLookup.find(parameterName);
    if(parameterIt == layout.parameterLookup.end()){
        AStringView interfacePath = "<unknown>";
        if(layout.bindEntry)
            interfacePath = AStringView(layout.bindEntry->virtualPath);

        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: typed parameter '{}' is not declared " "by interface '{}' for '{}'"), ConvertString(parameterName.view()), ConvertString(interfacePath), ConvertString(materialName.resolvedText()));
        return false;
    }
    const MaterialBindTypedLayoutParameterLookupEntry& parameterEntry = parameterIt.value();
    NWB_ASSERT(parameterEntry.fieldIndex < layout.typedLayoutFields.size());

    const MaterialTypedLayoutField& field = layout.typedLayoutFields[parameterEntry.fieldIndex];
    if(IsMaterialLayoutResourceFieldType(field.fieldType)){
        const MaterialResourceKind::Enum resourceKind = MaterialLayoutFieldResourceKind(field.fieldType);
        const AStringView resourcePath = TrimView(parameterValue);
        const auto resourceName = Name::FromText(resourcePath);
        NWB_ASSERT(parameterEntry.blockName && field.fieldName);
        if(!resourceName || !*resourceName || !IsSupportedMaterialResourceReference(resourceKind, resourcePath)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: resource parameter '{}' for '{}' must be an engine or project asset path"), ConvertString(parameterName.view()), ConvertString(materialName.resolvedText()));
            return false;
        }

        MaterialResourceReference resourceReference;
        resourceReference.blockName = parameterEntry.blockName;
        resourceReference.fieldName = field.fieldName;
        resourceReference.resourceKind = resourceKind;
        resourceReference.constantByteOffset = parameterEntry.constantByteOffset;
        if(!TryAssignMaterialResourceReferenceAsset(resourceKind, *resourceName, resourceReference)){
            NWB_ASSERT(false);
            return false;
        }
        outResourceReferences.push_back(resourceReference);
        return true;
    }

    const auto typedValue = ParseMaterialTypedLayoutParameterValue(materialName, parameterName, parameterValue, field);
    if(!typedValue)
        return false;

    if(!WriteMaterialTypedLayoutFieldBytes(parameterEntry.byteOffset, field.fieldType, *typedValue, inOutBlockBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' write exceeds packed layout bytes for '{}'"), ConvertString(parameterName.view()), ConvertString(materialName.resolvedText()));
        return false;
    }

    return true;
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
            const Expected<MaterialLayoutFieldType::Enum> fieldTypeResult = TryParseMaterialLayoutFieldTypeText(AStringView(bindField.type));
            if(!fieldTypeResult){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' has " "unsupported type '{}' for '{}'")
                    , ConvertString(bindEntry.virtualPath)
                    , ConvertString(instance->name)
                    , ConvertString(bindField.name)
                    , ConvertString(bindField.type)
                    , ConvertString(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }
            const MaterialLayoutFieldType::Enum fieldType = *fieldTypeResult;
            if(IsMaterialLayoutResourceFieldType(fieldType) && blockClass != MaterialBlockClass::MaterialConstant){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: resource field '{}.{}' must use material-constant storage for '{}'"), ConvertString(instance->name), ConvertString(bindField.name), ConvertString(contextName.resolvedText()));
                return MakeUnexpected(Failure{});
            }

            const u32 fieldByteSize = MaterialLayoutFieldByteSize(fieldType);
            const Expected<u32> fieldOffset = TryAlignMaterialLayoutFieldOffset(block.byteSize, fieldType);
            if(fieldByteSize == 0u || !fieldOffset || *fieldOffset > Limit<u32>::s_Max - fieldByteSize){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds " "u32 byte size for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(instance->name), ConvertString(contextName.resolvedText()));
                return MakeUnexpected(Failure{});
            }

            MaterialTypedLayoutField field;
            const auto fieldName = Name::FromText(AStringView(bindField.name));
            if(!fieldName || !*fieldName){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' has invalid field '{}.{}' for '{}'")
                    , ConvertString(bindEntry.virtualPath)
                    , ConvertString(instance->name)
                    , ConvertString(bindField.name)
                    , ConvertString(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }
            field.fieldName = *fieldName;
            field.fieldType = fieldType;
            field.offset = *fieldOffset;
            const auto defaultValue = BuildMaterialTypedLayoutDefaultValue(contextName, *instance, bindField, fieldType);
            if(!defaultValue)
                return MakeUnexpected(Failure{});
            field.defaultValue = *defaultValue;
            if(static_cast<usize>(field.offset) > Limit<usize>::s_Max - blockByteBegin){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' could " "not append alignment padding for '{}'")
                    , ConvertString(bindEntry.virtualPath)
                    , ConvertString(instance->name)
                    , ConvertString(bindField.name)
                    , ConvertString(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }
            PadMaterialTypedLayoutBytesTo(blockByteBegin + field.offset, layout.typedBlockBytes);
            AppendMaterialTypedLayoutFieldBytes(fieldType, field.defaultValue, layout.typedBlockBytes);

            layout.typedLayoutFields.push_back(field);
            block.byteSize = *fieldOffset + fieldByteSize;
        }

        const Expected<u32> alignedBlockByteSize = TryAlignMaterialLayoutBlockByteSize(block.byteSize);
        if(!alignedBlockByteSize){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds " "u32 byte size for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(instance->name), ConvertString(contextName.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        block.byteSize = *alignedBlockByteSize;
        if(static_cast<usize>(block.byteSize) > Limit<usize>::s_Max - blockByteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' could " "not append alignment padding for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(instance->name), ConvertString(contextName.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        PadMaterialTypedLayoutBytesTo(blockByteBegin + block.byteSize, layout.typedBlockBytes);

        layout.typedLayoutBlocks.push_back(block);
        if(blockClass == MaterialBlockClass::MaterialConstant){
            if(block.byteSize > Limit<u32>::s_Max - constantTypedByteSize){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' constant storage exceeds u32 byte size for '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(contextName.resolvedText()));
                return MakeUnexpected(Failure{});
            }
            constantTypedByteSize += block.byteSize;
        }
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
    outResourceReferences.clear();
    for(const auto& [parameterName, parameterValue] : parameters){
        if(!MaterialBindDetail::ApplyMaterialBindTypedLayoutParameterValue(layout, materialName, parameterName, parameterValue, outResourceReferences, inOutBlockBytes))
            return false;
    }

    Sort(outResourceReferences.begin(), outResourceReferences.end(), [](const MaterialResourceReference& lhs, const MaterialResourceReference& rhs)noexcept{
        return lhs.constantByteOffset < rhs.constantByteOffset;
    });

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
