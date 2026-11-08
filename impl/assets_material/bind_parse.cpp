// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_private.h"

#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<Core::Metascript::Document> ParseMaterialBindDocument(const Path& bindFilePath, MaterialCookArena& arena, ScratchArena& scratchArena){
    Core::Metascript::Document doc(arena);
    ScratchString bindText{scratchArena};
    if(!Core::Assets::ParseMetadataDocumentText(bindFilePath, s_MaterialBindDiagnosticPrefix, [&](const AStringView text){ return doc.parse(text); }, bindText, doc))
        return MakeUnexpected(Failure{});
    return doc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<AStringView> ParseMaterialBindStringField(const Path& bindFilePath, const Core::Metascript::Value& map, const AStringView fieldName, const AStringView contextLabel){
    const Core::Metascript::Value* value = map.findField(fieldName);
    if(!value || !value->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} field '{}' must be a string"), PathToString<tchar>(bindFilePath), ConvertString(contextLabel), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }

    const Core::Metascript::MStringView text = value->asString();
    return AStringView(text.data(), text.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialCookVector<MaterialBindAttribute>> ParseMaterialBindAttributeList(const Path& bindFilePath, const Core::Metascript::Value* attributesValue, const AStringView contextLabel, MaterialCookArena& arena){
    MaterialCookVector<MaterialBindAttribute> attributes(arena);
    if(!attributesValue)
        return attributes;

    if(!attributesValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} attributes must be a list"), PathToString<tchar>(bindFilePath), ConvertString(contextLabel));
        return MakeUnexpected(Failure{});
    }

    const auto& attributeList = attributesValue->asList();
    attributes.reserve(attributeList.size());
    for(const Core::Metascript::Value& attributeValue : attributeList){
        if(!attributeValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} attribute entries must be maps"), PathToString<tchar>(bindFilePath), ConvertString(contextLabel));
            return MakeUnexpected(Failure{});
        }

        MaterialBindAttribute attribute;
        const auto parsedName = ParseMaterialBindStringField(bindFilePath, attributeValue, s_MaterialBindNameKey, contextLabel);
        if(!parsedName)
            return MakeUnexpected(Failure{});
        attribute.name.assign(*parsedName);
        if(!IsAsciiIdentifier(attribute.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid attribute name '{}' in {}"), PathToString<tchar>(bindFilePath), ConvertString(attribute.name), ConvertString(contextLabel));
            return MakeUnexpected(Failure{});
        }

        const Core::Metascript::Value* argumentsValue = attributeValue.findField(s_MaterialBindArgumentsKey);
        if(argumentsValue){
            if(!argumentsValue->isList()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': attribute '{}' arguments must be a list"), PathToString<tchar>(bindFilePath), ConvertString(attribute.name));
                return MakeUnexpected(Failure{});
            }

            const auto& argumentList = argumentsValue->asList();
            attribute.arguments.reserve(argumentList.size());
            for(const Core::Metascript::Value& argumentValue : argumentList){
                if(!argumentValue.isString()){
                    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': attribute '{}' arguments must be strings"), PathToString<tchar>(bindFilePath), ConvertString(attribute.name));
                    return MakeUnexpected(Failure{});
                }

                const Core::Metascript::MStringView argumentText = argumentValue.asString();
                attribute.arguments.emplace_back(argumentText.data(), argumentText.size(), arena);
            }
        }

        attributes.push_back(Move(attribute));
    }

    return attributes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialBindField> ParseMaterialBindField(const Path& bindFilePath, const AStringView bindFilePathText, const Core::Metascript::Value& fieldValue, const MaterialBindStruct& bindStruct, MaterialCookArena& arena){
    MaterialBindField field;
    MaterialBindSchemaValidator validator{};
    if(!fieldValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' field entries must be maps"), PathToString<tchar>(bindFilePath), ConvertString(bindStruct.name));
        return MakeUnexpected(Failure{});
    }

    const auto parsedType = ParseMaterialBindStringField(bindFilePath, fieldValue, s_MaterialBindTypeKey, bindStruct.name);
    if(!parsedType)
        return MakeUnexpected(Failure{});
    field.type.assign(*parsedType);
    const auto parsedName = ParseMaterialBindStringField(bindFilePath, fieldValue, s_MaterialBindNameKey, bindStruct.name);
    if(!parsedName)
        return MakeUnexpected(Failure{});
    field.name.assign(*parsedName);
    if(!validator.validateMaterialBindFieldSpec(bindFilePathText, bindStruct, field))
        return MakeUnexpected(Failure{});
    auto attributes = ParseMaterialBindAttributeList(bindFilePath, fieldValue.findField(s_MaterialBindAttributesKey), field.name, arena);
    if(!attributes)
        return MakeUnexpected(Failure{});
    field.attributes = Move(*attributes);
    if(!validator.validateMaterialBindFieldAttributes(bindFilePathText, bindStruct, field))
        return MakeUnexpected(Failure{});

    return field;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialBindStruct> ParseMaterialBindStruct(const Path& bindFilePath, const AStringView bindFilePathText, const Core::Metascript::MStringView structName, const Core::Metascript::Value& structValue, MaterialCookArena& arena){
    MaterialBindStruct bindStruct;
    MaterialBindSchemaValidator validator{};
    if(!structValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must be a map"), PathToString<tchar>(bindFilePath), ConvertString(AStringView(structName.data(), structName.size())));
        return MakeUnexpected(Failure{});
    }

    bindStruct.name.assign(structName.data(), structName.size());
    if(!IsAsciiIdentifier(bindStruct.name)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid struct name '{}'"), PathToString<tchar>(bindFilePath), ConvertString(bindStruct.name));
        return MakeUnexpected(Failure{});
    }
    auto attributes = ParseMaterialBindAttributeList(bindFilePath, structValue.findField(s_MaterialBindAttributesKey), bindStruct.name, arena);
    if(!attributes)
        return MakeUnexpected(Failure{});
    bindStruct.attributes = Move(*attributes);
    if(!validator.validateMaterialBindStructAttributes(bindFilePathText, bindStruct))
        return MakeUnexpected(Failure{});

    const Core::Metascript::Value* fieldsValue = structValue.findField(s_MaterialBindFieldsKey);
    if(!fieldsValue || !fieldsValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' fields must be a list"), PathToString<tchar>(bindFilePath), ConvertString(bindStruct.name));
        return MakeUnexpected(Failure{});
    }
    if(fieldsValue->asList().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must declare at least one field"), PathToString<tchar>(bindFilePath), ConvertString(bindStruct.name));
        return MakeUnexpected(Failure{});
    }

    bindStruct.fields.reserve(fieldsValue->asList().size());
    for(const Core::Metascript::Value& fieldValue : fieldsValue->asList()){
        auto field = ParseMaterialBindField(bindFilePath, bindFilePathText, fieldValue, bindStruct, arena);
        if(!field)
            return MakeUnexpected(Failure{});

        if(bindStruct.hasField(AStringView(field->name))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': duplicate field '{}.{}'"), PathToString<tchar>(bindFilePath), ConvertString(bindStruct.name), ConvertString(field->name));
            return MakeUnexpected(Failure{});
        }

        bindStruct.fields.push_back(Move(*field));
    }

    return bindStruct;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialCookVector<MaterialBindStruct>> ParseMaterialBindStructs(const Path& bindFilePath, const AStringView bindFilePathText, const Core::Metascript::Document& doc, MaterialCookArena& arena){
    MaterialCookVector<MaterialBindStruct> structs(arena);

    const auto& structsMap = doc.structs();
    if(structsMap.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct declarations must not be empty"), PathToString<tchar>(bindFilePath));
        return MakeUnexpected(Failure{});
    }

    structs.reserve(structsMap.size());
    for(const auto& [structName, structValue] : structsMap){
        auto bindStruct = ParseMaterialBindStruct(bindFilePath, bindFilePathText, Core::Metascript::MStringView(structName.data(), structName.size()), structValue, arena);
        if(!bindStruct)
            return MakeUnexpected(Failure{});
        structs.push_back(Move(*bindStruct));
    }

    Sort(structs.begin(), structs.end(), [](const MaterialBindStruct& lhs, const MaterialBindStruct& rhs)noexcept{
        return lhs.name < rhs.name;
    });
    return structs;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 ParseMaterialBindInstances(const Path& bindFilePath, const AStringView bindFilePathText, const Core::Metascript::Document& doc, MaterialBindEntry& outEntry){
    outEntry.instances.clear();

    if(doc.declarations().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': instance declarations must not be empty"), PathToString<tchar>(bindFilePath));
        return false;
    }

    MaterialBindSchemaValidator validator{};
    outEntry.instances.reserve(doc.declarations().size());
    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        const AStringView typeName(declaration.type.data(), declaration.type.size());
        const AStringView instanceName(declaration.variable.data(), declaration.variable.size());
        const Core::Metascript::Value* instanceValue = doc.findVariable(instanceName);
        if(!instanceValue || !instanceValue->isNull()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': instance '{}' must not have an initializer or assignments"), PathToString<tchar>(bindFilePath), ConvertString(instanceName));
            return false;
        }

        if(!validator.validateAndAppendMaterialBindInstance(bindFilePathText, outEntry.structs, typeName, instanceName, outEntry.instances))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialBindEntry> ParseMaterialBindSource(const Path& bindFilePath, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena){
    auto doc = MaterialBindDetail::ParseMaterialBindDocument(bindFilePath, arena, scratchArena);
    if(!doc)
        return MakeUnexpected(Failure{});

    MaterialBindEntry entry(arena);

    entry.source = PathToString(arena, bindFilePath);
    if(!Core::Assets::CheckPairedSourceExtension(bindFilePath, entry.source, s_MaterialBindSourceExtensionText, s_MaterialBindDiagnosticPrefix, scratchArena))
        return MakeUnexpected(Failure{});

    auto structs = MaterialBindDetail::ParseMaterialBindStructs(bindFilePath, AStringView(entry.source), *doc, arena);
    if(!structs)
        return MakeUnexpected(Failure{});
    entry.structs = Move(*structs);
    if(!MaterialBindDetail::ParseMaterialBindInstances(bindFilePath, AStringView(entry.source), *doc, entry))
        return MakeUnexpected(Failure{});

    return entry;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

