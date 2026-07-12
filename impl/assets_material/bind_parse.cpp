
#if defined(NWB_COOK)


#include "bind_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_bind{


bool ParseMaterialBindDocument(const Path& bindFilePath, MaterialCookArena& arena, Metascript::Document& outDoc){
    CookString bindText{arena};
    if(!ReadTextFile(bindFilePath, bindText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to read Bind '{}'"), PathToString<tchar>(bindFilePath));
        return false;
    }
    StripUtf8Bom(bindText);

    if(!outDoc.parseWithImplicitAsset(AStringView(bindText), s_AssetTypeMaterialBind, s_AssetVariableMaterialBind)){
        for(const Metascript::ParseError& err : outDoc.errors()){
            NWB_LOGGER_ERROR(NWB_TEXT("Bind '{}' parse error at {}:{}: {}")
                , PathToString<tchar>(bindFilePath)
                , err.line
                , err.column
                , StringConvert(AStringView(err.message.data(), err.message.size()))
            );
        }
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const Metascript::Value* FindAssetMapValue(const Path& bindFilePath, const Metascript::Document& doc){
    const Metascript::MStringView assetVariable = doc.assetVariable();
    const Metascript::Value* asset = doc.findVariable(assetVariable);
    if(!asset){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset variable '{}' has no assignments")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(AStringView(assetVariable.data(), assetVariable.size()))
        );
        return nullptr;
    }

    if(!asset->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset is not a map"), PathToString<tchar>(bindFilePath));
        return nullptr;
    }

    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ValidatePairedSourceExtension(
    const Path& bindFilePath,
    const CookString& sourcePath,
    ScratchArena& scratchArena
){
    const Path sourcePathValue(bindFilePath.arena(), sourcePath);
    ScratchString extension = PathToString(scratchArena, sourcePathValue.extension());
    CanonicalizeTextInPlace(extension);
    if(AStringView(extension) == MaterialBindNames::SourceExtensionText())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': paired source '{}' must use '{}' extension")
        , PathToString<tchar>(bindFilePath)
        , StringConvert(sourcePath)
        , StringConvert(MaterialBindNames::SourceExtensionText())
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// material bind parsing helpers


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


bool ParseMaterialParameterTypeText(
    const AStringView typeText,
    MaterialParameterValueType::Enum& outType,
    u32& outComponentCount
){
    outType = MaterialParameterValueType::None;
    outComponentCount = 0u;

    const auto tryMatch = [&](
        const AStringView baseName,
        const MaterialParameterValueType::Enum type
    ) -> bool{
        if(typeText == baseName){
            outType = type;
            outComponentCount = 1u;
            return true;
        }

        const auto parseSuffix = [&](const AStringView prefix) -> bool{
            if(typeText.size() != prefix.size() + 1u)
                return false;
            if(typeText.substr(0u, prefix.size()) != prefix)
                return false;

            const char suffix = typeText[prefix.size()];
            if(suffix < '2' || suffix > '4')
                return false;

            outType = type;
            outComponentCount = static_cast<u32>(suffix - '0');
            return true;
        };

        return parseSuffix(baseName);
    };

    return
        tryMatch(AStringView("bool"), MaterialParameterValueType::Bool)
        || tryMatch(AStringView("char"), MaterialParameterValueType::Char)
        || tryMatch(AStringView("uchar"), MaterialParameterValueType::UChar)
        || tryMatch(AStringView("short"), MaterialParameterValueType::Short)
        || tryMatch(AStringView("ushort"), MaterialParameterValueType::UShort)
        || tryMatch(AStringView("int"), MaterialParameterValueType::Int)
        || tryMatch(AStringView("uint"), MaterialParameterValueType::UInt)
        || tryMatch(AStringView("half"), MaterialParameterValueType::Half)
        || tryMatch(AStringView("float"), MaterialParameterValueType::Float)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialBindStringField(
    const Path& bindFilePath,
    const Metascript::Value& map,
    const AStringView fieldName,
    const AStringView contextLabel,
    CookString& outValue
){
    outValue.clear();

    const Metascript::Value* value = map.findField(fieldName);
    if(!value || !value->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} field '{}' must be a string")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(contextLabel)
            , StringConvert(fieldName)
        );
        return false;
    }

    const Metascript::MStringView text = value->asString();
    outValue.assign(text.data(), text.size());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialBindAttributeList(
    const Path& bindFilePath,
    const Metascript::Value* attributesValue,
    const AStringView contextLabel,
    MaterialCookArena& arena,
    MaterialCookVector<MaterialBindAttribute>& outAttributes
){
    outAttributes.clear();
    if(!attributesValue)
        return true;

    if(!attributesValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} attributes must be a list")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(contextLabel)
        );
        return false;
    }

    const auto& attributeList = attributesValue->asList();
    outAttributes.reserve(attributeList.size());
    for(const Metascript::Value& attributeValue : attributeList){
        if(!attributeValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} attribute entries must be maps")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(contextLabel)
            );
            return false;
        }

        MaterialBindAttribute attribute(arena);
        if(!ParseMaterialBindStringField(bindFilePath, attributeValue, "name", contextLabel, attribute.name))
            return false;
        if(!IsMaterialBindIdentifier(attribute.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid attribute name '{}' in {}")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(attribute.name)
                , StringConvert(contextLabel)
            );
            return false;
        }

        const Metascript::Value* argumentsValue = attributeValue.findField("arguments");
        if(argumentsValue){
            if(!argumentsValue->isList()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': attribute '{}' arguments must be a list")
                    , PathToString<tchar>(bindFilePath)
                    , StringConvert(attribute.name)
                );
                return false;
            }

            const auto& argumentList = argumentsValue->asList();
            attribute.arguments.reserve(argumentList.size());
            for(const Metascript::Value& argumentValue : argumentList){
                if(!argumentValue.isString()){
                    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': attribute '{}' arguments must be strings")
                        , PathToString<tchar>(bindFilePath)
                        , StringConvert(attribute.name)
                    );
                    return false;
                }

                const Metascript::MStringView argumentText = argumentValue.asString();
                attribute.arguments.emplace_back(argumentText.data(), argumentText.size(), arena);
            }
        }

        outAttributes.push_back(Move(attribute));
    }

    return true;
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
    bool foundDefault = false;

    for(const MaterialBindAttribute& attribute : field.attributes){
        if(attribute.name != s_DefaultAttribute){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has unsupported attribute '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
                , StringConvert(attribute.name)
            );
            return false;
        }
        if(foundDefault){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' declares default more than once")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }
        if(attribute.arguments.size() != 1u || attribute.arguments[0].empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' default attribute requires one non-empty string argument")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }

        foundDefault = true;
    }

    if(foundDefault)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' must declare a default attribute")
        , PathToString<tchar>(bindFilePath)
        , StringConvert(bindStruct.name)
        , StringConvert(field.name)
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialBindField(
    const Path& bindFilePath,
    const Metascript::Value& fieldValue,
    const MaterialBindStruct& bindStruct,
    MaterialCookArena& arena,
    MaterialBindField& outField
){
    if(!fieldValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' field entries must be maps")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
        );
        return false;
    }

    if(!ParseMaterialBindStringField(bindFilePath, fieldValue, "type", bindStruct.name, outField.type))
        return false;
    if(!ParseMaterialBindStringField(bindFilePath, fieldValue, "name", bindStruct.name, outField.name))
        return false;
    MaterialParameterValueType::Enum fieldType = MaterialParameterValueType::None;
    u32 fieldComponentCount = 0u;
    if(!IsMaterialBindIdentifier(outField.type)
        || !ParseMaterialParameterTypeText(AStringView(outField.type), fieldType, fieldComponentCount)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has unsupported type '{}'")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
            , StringConvert(outField.name)
            , StringConvert(outField.type)
        );
        return false;
    }
    if(!IsMaterialBindIdentifier(outField.name)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has invalid name")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
            , StringConvert(outField.name)
        );
        return false;
    }
    if(!ParseMaterialBindAttributeList(bindFilePath, fieldValue.findField("attributes"), outField.name, arena, outField.attributes))
        return false;
    if(!ValidateMaterialBindFieldAttributes(bindFilePath, bindStruct, outField))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialBindStruct(
    const Path& bindFilePath,
    const Metascript::MStringView structName,
    const Metascript::Value& structValue,
    MaterialCookArena& arena,
    MaterialBindStruct& outStruct
){
    if(!structValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must be a map")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(AStringView(structName.data(), structName.size()))
        );
        return false;
    }

    outStruct.name.assign(structName.data(), structName.size());
    if(!IsMaterialBindIdentifier(outStruct.name)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid struct name '{}'")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(outStruct.name)
        );
        return false;
    }
    if(!ParseMaterialBindAttributeList(bindFilePath, structValue.findField("attributes"), outStruct.name, arena, outStruct.attributes))
        return false;
    if(!ValidateMaterialBindStructAttributes(bindFilePath, outStruct))
        return false;

    const Metascript::Value* fieldsValue = structValue.findField("fields");
    if(!fieldsValue || !fieldsValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' fields must be a list")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(outStruct.name)
        );
        return false;
    }
    if(fieldsValue->asList().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must declare at least one field")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(outStruct.name)
        );
        return false;
    }

    outStruct.fields.reserve(fieldsValue->asList().size());
    for(const Metascript::Value& fieldValue : fieldsValue->asList()){
        MaterialBindField field(arena);
        if(!ParseMaterialBindField(bindFilePath, fieldValue, outStruct, arena, field))
            return false;

        if(outStruct.findField(AStringView(field.name))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': duplicate field '{}.{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(outStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }

        outStruct.fields.push_back(Move(field));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialBindStructs(const Path& bindFilePath, const Metascript::Value& asset, MaterialCookArena& arena, MaterialCookVector<MaterialBindStruct>& outStructs){
    outStructs.clear();

    const Metascript::Value* structsValue = asset.findField("structs");
    if(!structsValue || !structsValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.structs must be a map"), PathToString<tchar>(bindFilePath));
        return false;
    }

    const auto& structsMap = structsValue->asMap();
    if(structsMap.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.structs must not be empty"), PathToString<tchar>(bindFilePath));
        return false;
    }

    outStructs.reserve(structsMap.size());
    for(const auto& [structName, structValue] : structsMap){
        MaterialBindStruct bindStruct(arena);
        if(!ParseMaterialBindStruct(bindFilePath, Metascript::MStringView(structName.data(), structName.size()), structValue, arena, bindStruct))
            return false;
        outStructs.push_back(Move(bindStruct));
    }

    Sort(outStructs.begin(), outStructs.end(), [](const MaterialBindStruct& lhs, const MaterialBindStruct& rhs){
        return lhs.name < rhs.name;
    });
    return true;
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
        if(!ParseMaterialBindStringField(bindFilePath, instanceValue, "type", "instance", instance.type))
            return false;
        if(!ParseMaterialBindStringField(bindFilePath, instanceValue, "name", "instance", instance.name))
            return false;
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


bool ParseMaterialBindSource(
    const Path& bindFilePath,
    const Metascript::Document& doc,
    MaterialCookArena& arena,
    MaterialBindEntry& outEntry,
    ScratchArena& scratchArena
){
    outEntry.reset();

    outEntry.source = PathToString(arena, bindFilePath);
    if(!ValidatePairedSourceExtension(bindFilePath, outEntry.source, scratchArena))
        return false;

    const Metascript::Value* assetValue = FindAssetMapValue(bindFilePath, doc);
    if(!assetValue)
        return false;
    if(!Core::Assets::ValidateMetadataAssetFields(bindFilePath, *assetValue, "Material bind", { "structs", "instances" }))
        return false;

    if(!ParseMaterialBindStructs(bindFilePath, *assetValue, arena, outEntry.structs))
        return false;
    if(!ParseMaterialBindInstances(bindFilePath, *assetValue, arena, outEntry))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

