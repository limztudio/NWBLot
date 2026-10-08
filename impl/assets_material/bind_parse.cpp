// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Metascript::Document> ParseMaterialBindDocument(const Path& bindFilePath, MaterialCookArena& arena){
    Metascript::Document doc(arena);
    CookString bindText{arena};
    if(!Core::Assets::ParseMetadataDocumentText(
        bindFilePath,
        "Material bind",
        bindText,
        doc,
        [&](const AStringView text){ return doc.parseWithImplicitAsset(text, s_AssetTypeMaterialBind, s_AssetVariableMaterialBind); }
    ))
        return MakeUnexpected(Failure{});
    return doc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool IsMaterialBindIdentifier(const AStringView text){
    if(text.empty())
        return false;

    const auto isAlpha = [](const char ch){
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    };
    const auto isDigit = [](const char ch){
        return ch >= '0' && ch <= '9';
    };
    const auto isIdentifierChar = [&](const char ch){
        return isAlpha(ch) || isDigit(ch) || ch == '_';
    };

    if(!isAlpha(text[0]) && text[0] != '_')
        return false;

    for(usize i = 1u; i < text.size(); ++i){
        if(!isIdentifierChar(text[i]))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialParameterType> ParseMaterialParameterTypeText(const AStringView typeText)noexcept{
    static constexpr NamedEnumCase<MaterialParameterValueType::Enum> s_Types[] = {
        { "bool", MaterialParameterValueType::Bool },
        { "char", MaterialParameterValueType::Char },
        { "uchar", MaterialParameterValueType::UChar },
        { "short", MaterialParameterValueType::Short },
        { "ushort", MaterialParameterValueType::UShort },
        { "int", MaterialParameterValueType::Int },
        { "uint", MaterialParameterValueType::UInt },
        { "half", MaterialParameterValueType::Half },
        { "float", MaterialParameterValueType::Float }
    };
    for(const auto& type : s_Types){
        const AStringView baseName = type.text;
        if(typeText == baseName)
            return MaterialParameterType{ type.value, 1u };
        if(typeText.size() == baseName.size() + 1u && typeText.substr(0u, baseName.size()) == baseName){
            const char suffix = typeText[baseName.size()];
            if(suffix >= '2' && suffix <= '4')
                return MaterialParameterType{ type.value, static_cast<u32>(suffix - '0') };
        }
    }
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialLayoutFieldType::Enum> ParseMaterialBindResourceFieldTypeText(const AStringView typeText)noexcept{
    if(typeText == s_BindFieldTypeTexture2D)
        return MaterialLayoutFieldType::SampledImage2D;
    if(typeText == s_BindFieldTypeSampler)
        return MaterialLayoutFieldType::Sampler;
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<AStringView> ParseMaterialBindStringField(
    const Path& bindFilePath,
    const Metascript::Value& map,
    const AStringView fieldName,
    const AStringView contextLabel
){
    const Metascript::Value* value = map.findField(fieldName);
    if(!value || !value->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} field '{}' must be a string")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(contextLabel)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }

    const Metascript::MStringView text = value->asString();
    return AStringView(text.data(), text.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialCookVector<MaterialBindAttribute>> ParseMaterialBindAttributeList(
    const Path& bindFilePath,
    const Metascript::Value* attributesValue,
    const AStringView contextLabel,
    MaterialCookArena& arena
){
    MaterialCookVector<MaterialBindAttribute> attributes(arena);
    if(!attributesValue)
        return attributes;

    if(!attributesValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} attributes must be a list")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(contextLabel)
        );
        return MakeUnexpected(Failure{});
    }

    const auto& attributeList = attributesValue->asList();
    attributes.reserve(attributeList.size());
    for(const Metascript::Value& attributeValue : attributeList){
        if(!attributeValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} attribute entries must be maps")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(contextLabel)
            );
            return MakeUnexpected(Failure{});
        }

        MaterialBindAttribute attribute(arena);
        const auto parsedName = ParseMaterialBindStringField(bindFilePath, attributeValue, "name", contextLabel);
        if(!parsedName)
            return MakeUnexpected(Failure{});
        attribute.name = *parsedName;
        if(!IsMaterialBindIdentifier(attribute.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid attribute name '{}' in {}")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(attribute.name)
                , StringConvert(contextLabel)
            );
            return MakeUnexpected(Failure{});
        }

        const Metascript::Value* argumentsValue = attributeValue.findField("arguments");
        if(argumentsValue){
            if(!argumentsValue->isList()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': attribute '{}' arguments must be a list")
                    , PathToString<tchar>(bindFilePath)
                    , StringConvert(attribute.name)
                );
                return MakeUnexpected(Failure{});
            }

            const auto& argumentList = argumentsValue->asList();
            attribute.arguments.reserve(argumentList.size());
            for(const Metascript::Value& argumentValue : argumentList){
                if(!argumentValue.isString()){
                    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': attribute '{}' arguments must be strings")
                        , PathToString<tchar>(bindFilePath)
                        , StringConvert(attribute.name)
                    );
                    return MakeUnexpected(Failure{});
                }

                const Metascript::MStringView argumentText = argumentValue.asString();
                attribute.arguments.emplace_back(argumentText.data(), argumentText.size(), arena);
            }
        }

        attributes.push_back(Move(attribute));
    }

    return attributes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ValidateMaterialBindStructAttributes(
    const Path& bindFilePath,
    const MaterialBindStruct& bindStruct
){
    bool foundBlockClass = false;

    for(const MaterialBindAttribute& attribute : bindStruct.attributes){
        if(attribute.name != s_MaterialConstantAttribute && attribute.name != s_MaterialMutableAttribute){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' has unsupported attribute '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(attribute.name)
            );
            return false;
        }
        if(!attribute.arguments.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' block class attribute '{}' must not have arguments")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(attribute.name)
            );
            return false;
        }
        if(foundBlockClass){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' declares more than one block class attribute")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
            );
            return false;
        }

        foundBlockClass = true;
    }

    if(foundBlockClass)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must declare a material block class attribute")
        , PathToString<tchar>(bindFilePath)
        , StringConvert(bindStruct.name)
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ValidateMaterialBindFieldAttributes(const Path& bindFilePath, const MaterialBindStruct& bindStruct, const MaterialBindField& field){
    const bool isResourceField = ParseMaterialBindResourceFieldTypeText(AStringView(field.type)).has_value();
    if(isResourceField && field.attributes.empty())
        return true;
    bool foundRequiredAttribute = false;

    for(const MaterialBindAttribute& attribute : field.attributes){
        if(isResourceField || attribute.name != s_DefaultAttribute){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has unsupported attribute '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
                , StringConvert(attribute.name)
            );
            return false;
        }
        if(foundRequiredAttribute){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' declares {} more than once")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
                , StringConvert(s_DefaultAttribute)
            );
            return false;
        }
        if(attribute.arguments.size() != 1u || attribute.arguments[0].empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' attribute '{}' requires one non-empty string argument")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
                , StringConvert(attribute.name)
            );
            return false;
        }
        foundRequiredAttribute = true;
    }

    if(!foundRequiredAttribute){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' must declare a {} attribute")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
            , StringConvert(field.name)
            , StringConvert(s_DefaultAttribute)
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialBindField> ParseMaterialBindField(
    const Path& bindFilePath,
    const Metascript::Value& fieldValue,
    const MaterialBindStruct& bindStruct,
    MaterialCookArena& arena
){
    MaterialBindField field(arena);
    if(!fieldValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' field entries must be maps")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
        );
        return MakeUnexpected(Failure{});
    }

    const auto parsedType = ParseMaterialBindStringField(bindFilePath, fieldValue, "type", bindStruct.name);
    if(!parsedType)
        return MakeUnexpected(Failure{});
    field.type = *parsedType;
    const auto parsedName = ParseMaterialBindStringField(bindFilePath, fieldValue, "name", bindStruct.name);
    if(!parsedName)
        return MakeUnexpected(Failure{});
    field.name = *parsedName;
    const bool isResourceField = ParseMaterialBindResourceFieldTypeText(AStringView(field.type)).has_value();
    if(!IsMaterialBindIdentifier(field.type)
        || (!isResourceField && !ParseMaterialParameterTypeText(AStringView(field.type)))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has unsupported type '{}'")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
            , StringConvert(field.name)
            , StringConvert(field.type)
        );
        return MakeUnexpected(Failure{});
    }
    if(isResourceField && bindStruct.findAttribute(s_MaterialMutableAttribute)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': resource field '{}.{}' must use material_constant storage")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
            , StringConvert(field.name)
        );
        return MakeUnexpected(Failure{});
    }
    if(!IsMaterialBindIdentifier(field.name)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has invalid name")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
            , StringConvert(field.name)
        );
        return MakeUnexpected(Failure{});
    }
    auto attributes = ParseMaterialBindAttributeList(bindFilePath, fieldValue.findField("attributes"), field.name, arena);
    if(!attributes)
        return MakeUnexpected(Failure{});
    field.attributes = Move(*attributes);
    if(!ValidateMaterialBindFieldAttributes(bindFilePath, bindStruct, field))
        return MakeUnexpected(Failure{});

    return field;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialBindStruct> ParseMaterialBindStruct(
    const Path& bindFilePath,
    const Metascript::MStringView structName,
    const Metascript::Value& structValue,
    MaterialCookArena& arena
){
    MaterialBindStruct bindStruct(arena);
    if(!structValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must be a map")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(AStringView(structName.data(), structName.size()))
        );
        return MakeUnexpected(Failure{});
    }

    bindStruct.name.assign(structName.data(), structName.size());
    if(!IsMaterialBindIdentifier(bindStruct.name)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid struct name '{}'")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
        );
        return MakeUnexpected(Failure{});
    }
    auto attributes = ParseMaterialBindAttributeList(bindFilePath, structValue.findField("attributes"), bindStruct.name, arena);
    if(!attributes)
        return MakeUnexpected(Failure{});
    bindStruct.attributes = Move(*attributes);
    if(!ValidateMaterialBindStructAttributes(bindFilePath, bindStruct))
        return MakeUnexpected(Failure{});

    const Metascript::Value* fieldsValue = structValue.findField("fields");
    if(!fieldsValue || !fieldsValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' fields must be a list")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
        );
        return MakeUnexpected(Failure{});
    }
    if(fieldsValue->asList().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must declare at least one field")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
        );
        return MakeUnexpected(Failure{});
    }

    bindStruct.fields.reserve(fieldsValue->asList().size());
    for(const Metascript::Value& fieldValue : fieldsValue->asList()){
        auto field = ParseMaterialBindField(bindFilePath, fieldValue, bindStruct, arena);
        if(!field)
            return MakeUnexpected(Failure{});

        if(bindStruct.findField(AStringView(field->name))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': duplicate field '{}.{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field->name)
            );
            return MakeUnexpected(Failure{});
        }

        bindStruct.fields.push_back(Move(*field));
    }

    return bindStruct;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialCookVector<MaterialBindStruct>> ParseMaterialBindStructs(const Path& bindFilePath, const Metascript::Value& asset, MaterialCookArena& arena){
    MaterialCookVector<MaterialBindStruct> structs(arena);

    const Metascript::Value* structsValue = asset.findField("structs");
    if(!structsValue || !structsValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.structs must be a map"), PathToString<tchar>(bindFilePath));
        return MakeUnexpected(Failure{});
    }

    const auto& structsMap = structsValue->asMap();
    if(structsMap.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.structs must not be empty"), PathToString<tchar>(bindFilePath));
        return MakeUnexpected(Failure{});
    }

    structs.reserve(structsMap.size());
    for(const auto& [structName, structValue] : structsMap){
        auto bindStruct = ParseMaterialBindStruct(bindFilePath, Metascript::MStringView(structName.data(), structName.size()), structValue, arena);
        if(!bindStruct)
            return MakeUnexpected(Failure{});
        structs.push_back(Move(*bindStruct));
    }

    Sort(structs.begin(), structs.end(), [](const MaterialBindStruct& lhs, const MaterialBindStruct& rhs){
        return lhs.name < rhs.name;
    });
    return structs;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialBindInstances(const Path& bindFilePath, const Metascript::Value& asset, MaterialCookArena& arena, MaterialBindEntry& outEntry){
    outEntry.instances.clear();

    const Metascript::Value* instancesValue = asset.findField("instances");
    if(!instancesValue || !instancesValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.instances must be a list"), PathToString<tchar>(bindFilePath));
        return false;
    }
    if(instancesValue->asList().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.instances must not be empty"), PathToString<tchar>(bindFilePath));
        return false;
    }

    outEntry.instances.reserve(instancesValue->asList().size());
    for(const Metascript::Value& instanceValue : instancesValue->asList()){
        if(!instanceValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.instances entries must be maps"), PathToString<tchar>(bindFilePath));
            return false;
        }

        MaterialBindInstance instance(arena);
        const auto parsedType = ParseMaterialBindStringField(bindFilePath, instanceValue, "type", "instance");
        if(!parsedType)
            return false;
        instance.type = *parsedType;
        const auto parsedName = ParseMaterialBindStringField(bindFilePath, instanceValue, "name", "instance");
        if(!parsedName)
            return false;
        instance.name = *parsedName;
        if(!IsMaterialBindIdentifier(instance.type) || !outEntry.findStruct(AStringView(instance.type))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': instance '{}' references unknown struct type '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(instance.name)
                , StringConvert(instance.type)
            );
            return false;
        }
        if(!IsMaterialBindIdentifier(instance.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid instance name '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(instance.name)
            );
            return false;
        }
        if(outEntry.findInstance(AStringView(instance.name))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': duplicate instance name '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(instance.name)
            );
            return false;
        }

        outEntry.instances.push_back(Move(instance));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialBindEntry> ParseMaterialBindSource(
    const Path& bindFilePath,
    const Metascript::Document& doc,
    MaterialCookArena& arena,
    ScratchArena& scratchArena
){
    MaterialBindEntry entry(arena);

    entry.source = PathToString(arena, bindFilePath);
    if(!Core::Assets::CheckPairedSourceExtension(
        bindFilePath,
        entry.source,
        MaterialBindNames::SourceExtensionText(),
        "Material bind",
        scratchArena
    ))
        return MakeUnexpected(Failure{});

    const Metascript::Value* assetValue = Core::Assets::FindMetadataAssetMapValue<Metascript::Document, Metascript::Value>(bindFilePath, doc, "Material bind");
    if(!assetValue)
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(bindFilePath, *assetValue, "Material bind", { "structs", "instances" }))
        return MakeUnexpected(Failure{});

    auto structs = ParseMaterialBindStructs(bindFilePath, *assetValue, arena);
    if(!structs)
        return MakeUnexpected(Failure{});
    entry.structs = Move(*structs);
    if(!ParseMaterialBindInstances(bindFilePath, *assetValue, arena, entry))
        return MakeUnexpected(Failure{});

    return entry;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

