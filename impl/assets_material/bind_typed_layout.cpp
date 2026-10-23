// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_private.h"
#include "bind_typed_value.h"

#include <global/array.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialLayoutFieldType::Enum> ParseMaterialLayoutFieldType(const AStringView typeText)noexcept{
    const auto resourceType = ParseMaterialBindResourceFieldTypeText(typeText);
    if(resourceType)
        return *resourceType;
    const auto type = ParseMaterialParameterTypeText(typeText);
    if(!type)
        return MakeUnexpected(Failure{});
    const auto fieldType = MaterialLayoutFieldTypeFromParameterType(type->valueType, type->componentCount);
    if(!IsValidMaterialLayoutFieldType(fieldType))
        return MakeUnexpected(Failure{});
    return fieldType;
}


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

    const auto keyResult = BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(bindField.name));
    if(!keyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: field '{}.{}' for '{}' exceeds ACompactString capacity"), StringConvert(instance.name), StringConvert(bindField.name), StringConvert(materialName.resolvedText()));
        return MakeUnexpected(Failure{});
    }
    const ACompactString& key = *keyResult;

    ACompactString defaultText;
    const AStringView defaultArgument = bindField.defaultArgument();
    if(!defaultText.assign(defaultArgument)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default for '{}.{}' in '{}' exceeds ACompactString capacity"), StringConvert(instance.name), StringConvert(bindField.name), StringConvert(materialName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    const auto defaultParameter = MaterialTypedValueParser::Parse(key, defaultText);
    if(!defaultParameter){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default '{}' for '{}.{}' in '{}' is invalid"), StringConvert(defaultArgument), StringConvert(instance.name), StringConvert(bindField.name), StringConvert(materialName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    const MaterialLayoutFieldType::Enum defaultFieldType = MaterialLayoutFieldTypeFromParameterType(static_cast<MaterialParameterValueType::Enum>(defaultParameter->meta.z), defaultParameter->meta.w);
    if(defaultFieldType != fieldType){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: default '{}' for '{}.{}' in '{}' " "does not match field type '{}'")
            , StringConvert(defaultArgument)
            , StringConvert(instance.name)
            , StringConvert(bindField.name)
            , StringConvert(materialName.resolvedText())
            , StringConvert(bindField.type)
        );
        return MakeUnexpected(Failure{});
    }

    return MaterialTypedValueParser::ToLayoutValue(*defaultParameter);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<Span<const u8>> GetMaterialTypedLayoutFieldBytes(const MaterialLayoutFieldType::Enum fieldType, const UInt4U& value)noexcept{
    const u32 byteSize = MaterialLayoutFieldByteSize(fieldType);
    if(byteSize == 0u || byteSize > sizeof(value))
        return MakeUnexpected(Failure{});
    return Span<const u8>(reinterpret_cast<const u8*>(&value), byteSize);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The builder admits only field types with a nonzero byte size.
static void AppendMaterialTypedLayoutFieldBytes(Material::TypedBlockByteVector& outBlockBytes, const MaterialLayoutFieldType::Enum fieldType, const UInt4U& value){
    const auto fieldBytes = GetMaterialTypedLayoutFieldBytes(fieldType, value);
    NWB_ASSERT(fieldBytes);
    outBlockBytes.reserve(outBlockBytes.size() + fieldBytes->size());
    outBlockBytes.insert(outBlockBytes.end(), fieldBytes->begin(), fieldBytes->end());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Field offsets only grow inside a block, so padding never shrinks the bytes.
static void PadMaterialTypedLayoutBytesTo(Material::TypedBlockByteVector& outBlockBytes, const usize targetByteSize){
    NWB_ASSERT(targetByteSize >= outBlockBytes.size());
    if(targetByteSize > outBlockBytes.capacity())
        outBlockBytes.reserve(targetByteSize);
    outBlockBytes.resize(targetByteSize, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ReserveMaterialBindTypedLayoutVectors(const MaterialBindEntry& bindEntry, const Name& contextName, const ScratchVector<NotNull<const MaterialBindInstance*>>& sortedInstances, MaterialBindTypedLayout& outLayout){
    usize fieldReserveCount = 0u;
    usize byteReserveCount = 0u;
    for(const NotNull<const MaterialBindInstance*> instance : sortedInstances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance->type));
        if(!bindStruct)
            continue;

        if(fieldReserveCount > Limit<usize>::s_Max - bindStruct->fields.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' exceeds supported field count for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(contextName.resolvedText()));
            return false;
        }
        fieldReserveCount += bindStruct->fields.size();

        for(const MaterialBindField& bindField : bindStruct->fields){
            usize fieldByteReserve = sizeof(UInt4U);
            const auto fieldType = ParseMaterialLayoutFieldType(AStringView(bindField.type));
            if(fieldType){
                const u32 fieldByteSize = MaterialLayoutFieldByteSize(*fieldType);
                if(fieldByteSize != 0u)
                    fieldByteReserve = fieldByteSize;
            }

            if(byteReserveCount > Limit<usize>::s_Max - fieldByteReserve){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' exceeds supported byte count for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(contextName.resolvedText()));
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
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' exceeds supported block count for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(contextName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    return instances;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BuildMaterialBindTypedLayoutBlockLookup(const AStringView contextLabel, MaterialBindTypedLayout& inOutLayout){
    inOutLayout.blockLookup.clear();
    inOutLayout.blockLookup.reserve(inOutLayout.typedLayoutBlocks.size());

    usize blockByteBegin = 0u;
    u32 constantByteBegin = 0u;
    for(usize blockIndex = 0u; blockIndex < inOutLayout.typedLayoutBlocks.size(); ++blockIndex){
        const MaterialTypedLayoutBlock& block = inOutLayout.typedLayoutBlocks[blockIndex];
        NWB_ASSERT(blockIndex < Limit<u32>::s_Max);
        if(blockByteBegin > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: block byte offset exceeds u32 for '{}'"), StringConvert(contextLabel));
            return false;
        }

        const MaterialBindTypedLayoutBlockLookupEntry entry{ static_cast<u32>(blockIndex), static_cast<u32>(blockByteBegin), constantByteBegin };
        if(!inOutLayout.blockLookup.emplace(block.blockName, entry).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate block in '{}'"), StringConvert(contextLabel));
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


static bool BuildMaterialBindTypedLayoutParameterLookup(const AStringView contextLabel, MaterialBindTypedLayout& inOutLayout){
    inOutLayout.parameterLookup.clear();
    inOutLayout.parameterLookup.reserve(inOutLayout.typedLayoutFields.size());

    const MaterialBindEntry& bindEntry = *inOutLayout.bindEntry;
    for(const MaterialBindInstance& instance : bindEntry.instances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance.type));
        NWB_ASSERT(bindStruct);

        const auto instanceName = Name::FromText(AStringView(instance.name));
        NWB_ASSERT(instanceName);
        const auto blockIt = inOutLayout.blockLookup.find(*instanceName);
        NWB_ASSERT(blockIt != inOutLayout.blockLookup.end());

        const MaterialBindTypedLayoutBlockLookupEntry& blockEntry = blockIt.value();
        NWB_ASSERT(blockEntry.blockIndex < inOutLayout.typedLayoutBlocks.size());

        const MaterialTypedLayoutBlock& block = inOutLayout.typedLayoutBlocks[blockEntry.blockIndex];
        NWB_ASSERT(block.fieldCount == bindStruct->fields.size());

        for(u32 fieldOffset = 0u; fieldOffset < block.fieldCount; ++fieldOffset){
            const usize fieldIndex = static_cast<usize>(block.fieldBegin) + fieldOffset;
            NWB_ASSERT(fieldIndex < inOutLayout.typedLayoutFields.size());
            if(fieldIndex > Limit<u32>::s_Max){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' instance '{}' " "field range exceeds layout for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(instance.name), StringConvert(contextLabel));
                return false;
            }

            const MaterialBindField& bindField = bindStruct->fields[fieldOffset];
            const MaterialTypedLayoutField& field = inOutLayout.typedLayoutFields[fieldIndex];
            NWB_ASSERT(Name::FromText(AStringView(bindField.name)) == field.fieldName);
            if(field.offset > Limit<u32>::s_Max - blockEntry.byteBegin){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' " "byte offset exceeds u32 for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(bindStruct->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextLabel)
                );
                return false;
            }
            // Resource fields live in constant blocks, whose total size the builder already bounded to u32.
            NWB_ASSERT(!IsMaterialLayoutResourceFieldType(field.fieldType) || field.offset <= Limit<u32>::s_Max - blockEntry.constantByteBegin);

            const auto parameterNameResult = BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(bindField.name));
            if(!parameterNameResult){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: field '{}.{}' for '{}' exceeds ACompactString capacity"), StringConvert(instance.name), StringConvert(bindField.name), StringConvert(contextLabel));
                return false;
            }
            const ACompactString& parameterName = *parameterNameResult;

            const MaterialBindTypedLayoutParameterLookupEntry entry{ static_cast<u32>(fieldIndex), blockEntry.byteBegin + field.offset, blockEntry.constantByteBegin + field.offset, block.blockName };
            if(!inOutLayout.parameterLookup.emplace(parameterName, entry).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate parameter '{}' in '{}'"), StringConvert(parameterName.view()), StringConvert(contextLabel));
                return false;
            }
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool WriteMaterialTypedLayoutFieldBytes(Material::TypedBlockByteVector& inOutBlockBytes, const usize byteOffset, const MaterialLayoutFieldType::Enum fieldType, const UInt4U& value)noexcept{
    const auto fieldBytes = GetMaterialTypedLayoutFieldBytes(fieldType, value);
    if(!fieldBytes)
        return false;
    if(byteOffset > inOutBlockBytes.size() || fieldBytes->size() > inOutBlockBytes.size() - byteOffset)
        return false;

    NWB_MEMCPY(inOutBlockBytes.data() + byteOffset, fieldBytes->size(), fieldBytes->data(), fieldBytes->size());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<UInt4U> ParseMaterialTypedLayoutParameterValue(const Name& materialName, const ACompactString& parameterName, const ACompactString& parameterValue, const MaterialTypedLayoutField& field){

    const auto parameter = MaterialTypedValueParser::Parse(parameterName, parameterValue);
    if(!parameter){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' for '{}' has invalid value '{}'"), StringConvert(parameterName.view()), StringConvert(materialName.resolvedText()), StringConvert(parameterValue.view()));
        return MakeUnexpected(Failure{});
    }

    const MaterialLayoutFieldType::Enum parameterFieldType = MaterialLayoutFieldTypeFromParameterType(static_cast<MaterialParameterValueType::Enum>(parameter->meta.z), parameter->meta.w);
    if(parameterFieldType != field.fieldType){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' for '{}' does not match interface field type"), StringConvert(parameterName.view()), StringConvert(materialName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    return MaterialTypedValueParser::ToLayoutValue(*parameter);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ApplyMaterialBindTypedLayoutParameterValue(
    const MaterialBindTypedLayout& layout,
    const Name& materialName,
    const ACompactString& parameterName,
    const ACompactString& parameterValue,
    Material::TypedBlockByteVector& inOutBlockBytes,
    Material::ResourceReferenceVector& outResourceReferences
){
    const auto parameterIt = layout.parameterLookup.find(parameterName);
    if(parameterIt == layout.parameterLookup.end()){
        AStringView interfacePath = "<unknown>";
        if(layout.bindEntry)
            interfacePath = AStringView(layout.bindEntry->virtualPath);

        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: typed parameter '{}' is not declared " "by interface '{}' for '{}'"), StringConvert(parameterName.view()), StringConvert(interfacePath), StringConvert(materialName.resolvedText()));
        return false;
    }
    const MaterialBindTypedLayoutParameterLookupEntry& parameterEntry = parameterIt.value();
    NWB_ASSERT(parameterEntry.fieldIndex < layout.typedLayoutFields.size());

    const MaterialTypedLayoutField& field = layout.typedLayoutFields[parameterEntry.fieldIndex];
    if(IsMaterialLayoutResourceFieldType(field.fieldType)){
        const MaterialResourceKind::Enum resourceKind = MaterialLayoutFieldResourceKind(field.fieldType);
        const AStringView resourcePath = TrimView(AStringView(parameterValue));
        const auto resourceName = Name::FromText(resourcePath);
        NWB_ASSERT(parameterEntry.blockName && field.fieldName);
        if(!resourceName || !*resourceName || !IsSupportedMaterialResourceReference(resourceKind, resourcePath)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: resource parameter '{}' for '{}' must be an engine or project asset path"), StringConvert(parameterName.view()), StringConvert(materialName.resolvedText()));
            return false;
        }

        MaterialResourceReference resourceReference;
        resourceReference.blockName = parameterEntry.blockName;
        resourceReference.fieldName = field.fieldName;
        resourceReference.resourceKind = resourceKind;
        resourceReference.constantByteOffset = parameterEntry.constantByteOffset;
        if(!AssignMaterialResourceReferenceAsset(resourceReference, resourceKind, *resourceName))
            return false;
        outResourceReferences.push_back(resourceReference);
        return true;
    }

    const auto typedValue = ParseMaterialTypedLayoutParameterValue(materialName, parameterName, parameterValue, field);
    if(!typedValue)
        return false;

    if(!WriteMaterialTypedLayoutFieldBytes(inOutBlockBytes, parameterEntry.byteOffset, field.fieldType, *typedValue)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: parameter '{}' write exceeds packed layout bytes for '{}'"), StringConvert(parameterName.view()), StringConvert(materialName.resolvedText()));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialBindTypedLayout> BuildMaterialBindTypedLayoutImpl(const MaterialBindEntry& bindEntry, const Name& contextName, MaterialCookArena& arena, ScratchArena& scratchArena){
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
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance->name)
                , StringConvert(instance->type)
                , StringConvert(contextName.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }

        const auto blockClassResult = ParseMaterialBindBlockClass(*bindStruct);
        if(!blockClassResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' struct '{}' is missing " "a material block class for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(bindStruct->name), StringConvert(contextName.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        const MaterialBlockClass::Enum blockClass = *blockClassResult;
        if(layout.typedLayoutFields.size() > Limit<u32>::s_Max || bindStruct->fields.size() > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds " "supported field count for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(instance->name), StringConvert(contextName.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        const usize blockByteBegin = layout.typedBlockBytes.size();

        MaterialTypedLayoutBlock block;
        const auto blockName = Name::FromText(AStringView(instance->name));
        if(!blockName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' has invalid block '{}' for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(instance->name), StringConvert(contextName.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        block.blockName = *blockName;
        block.blockClass = blockClass;
        block.fieldBegin = static_cast<u32>(layout.typedLayoutFields.size());
        block.fieldCount = static_cast<u32>(bindStruct->fields.size());
        block.byteSize = 0u;

        if(!block.blockName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' has invalid block '{}' for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(instance->name), StringConvert(contextName.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        for(const MaterialBindField& bindField : bindStruct->fields){
            const auto fieldTypeResult = ParseMaterialLayoutFieldType(AStringView(bindField.type));
            if(!fieldTypeResult){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' has " "unsupported type '{}' for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(bindField.type)
                    , StringConvert(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }
            const MaterialLayoutFieldType::Enum fieldType = *fieldTypeResult;
            if(IsMaterialLayoutResourceFieldType(fieldType) && blockClass != MaterialBlockClass::MaterialConstant){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: resource field '{}.{}' must use material-constant storage for '{}'"), StringConvert(instance->name), StringConvert(bindField.name), StringConvert(contextName.resolvedText()));
                return MakeUnexpected(Failure{});
            }

            const u32 fieldByteSize = MaterialLayoutFieldByteSize(fieldType);
            const auto fieldOffset = AlignMaterialLayoutFieldOffset(block.byteSize, fieldType);
            if(fieldByteSize == 0u || !fieldOffset || *fieldOffset > Limit<u32>::s_Max - fieldByteSize){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds " "u32 byte size for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(instance->name), StringConvert(contextName.resolvedText()));
                return MakeUnexpected(Failure{});
            }

            MaterialTypedLayoutField field;
            const auto fieldName = Name::FromText(AStringView(bindField.name));
            if(!fieldName){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' has invalid field '{}.{}' for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }
            field.fieldName = *fieldName;
            field.fieldType = fieldType;
            field.offset = *fieldOffset;
            if(!field.fieldName){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' has invalid field '{}.{}' for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }
            const auto defaultValue = BuildMaterialTypedLayoutDefaultValue(contextName, *instance, bindField, fieldType);
            if(!defaultValue)
                return MakeUnexpected(Failure{});
            field.defaultValue = *defaultValue;
            if(static_cast<usize>(field.offset) > Limit<usize>::s_Max - blockByteBegin){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' field '{}.{}' could " "not append alignment padding for '{}'")
                    , StringConvert(bindEntry.virtualPath)
                    , StringConvert(instance->name)
                    , StringConvert(bindField.name)
                    , StringConvert(contextName.resolvedText())
                );
                return MakeUnexpected(Failure{});
            }
            PadMaterialTypedLayoutBytesTo(layout.typedBlockBytes, blockByteBegin + field.offset);
            AppendMaterialTypedLayoutFieldBytes(layout.typedBlockBytes, fieldType, field.defaultValue);

            layout.typedLayoutFields.push_back(field);
            block.byteSize = *fieldOffset + fieldByteSize;
        }

        const auto alignedBlockByteSize = AlignMaterialLayoutBlockByteSize(block.byteSize);
        if(!alignedBlockByteSize){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' exceeds " "u32 byte size for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(instance->name), StringConvert(contextName.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        block.byteSize = *alignedBlockByteSize;
        if(static_cast<usize>(block.byteSize) > Limit<usize>::s_Max - blockByteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' block '{}' could " "not append alignment padding for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(instance->name), StringConvert(contextName.resolvedText()));
            return MakeUnexpected(Failure{});
        }
        PadMaterialTypedLayoutBytesTo(layout.typedBlockBytes, blockByteBegin + block.byteSize);

        layout.typedLayoutBlocks.push_back(block);
        if(blockClass == MaterialBlockClass::MaterialConstant){
            if(block.byteSize > Limit<u32>::s_Max - constantTypedByteSize){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' constant storage exceeds u32 byte size for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(contextName.resolvedText()));
                return MakeUnexpected(Failure{});
            }
            constantTypedByteSize += block.byteSize;
        }
    }

    layout.layoutHash = MaterialBinaryPayload::ComputeMaterialTypedLayoutHash(layout.typedLayoutBlocks, layout.typedLayoutFields);
    if(layout.layoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: interface '{}' produced an empty layout for '{}'"), StringConvert(bindEntry.virtualPath), StringConvert(contextName.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    NWB_ASSERT(MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(layout.typedLayoutBlocks) == layout.typedBlockBytes.size());

    if(!BuildMaterialBindTypedLayoutBlockLookup(contextName.resolvedText(), layout))
        return MakeUnexpected(Failure{});
    if(!BuildMaterialBindTypedLayoutParameterLookup(contextName.resolvedText(), layout))
        return MakeUnexpected(Failure{});

    return layout;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NotNull<const MaterialBindTypedLayout*>> FindOrBuildMaterialBindTypedLayoutImpl(const Name& materialInterface, const MaterialBindEntry& bindEntry, MaterialBindTypedLayoutCache& inOutCache, ScratchArena& scratchArena){
    const auto cacheIt = inOutCache.lookup.find(materialInterface);
    if(cacheIt != inOutCache.lookup.end()){
        const usize cacheIndex = cacheIt.value();
        NWB_ASSERT(cacheIndex < inOutCache.entries.size());
        return MakeNotNull(&inOutCache.entries[cacheIndex]);
    }

    const usize cacheIndex = inOutCache.entries.size();
    auto layout = BuildMaterialBindTypedLayoutImpl(bindEntry, materialInterface, inOutCache.entries.get_allocator().arena(), scratchArena);
    if(!layout)
        return MakeUnexpected(Failure{});

    inOutCache.entries.reserve(cacheIndex + 1u);
    if(!inOutCache.lookup.emplace(materialInterface, cacheIndex).second){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind typed layout: duplicate cache entry for interface '{}'"), StringConvert(materialInterface.resolvedText()));
        return MakeUnexpected(Failure{});
    }

    inOutCache.entries.push_back(Move(*layout));
    return MakeNotNull(&inOutCache.entries.back());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

