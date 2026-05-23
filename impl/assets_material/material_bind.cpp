// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_bind.h"

#include <core/metascript/parser.h>

#include <core/common/log.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Metascript = Core::Metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_bind{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookString = ShaderCook::CookString;

static constexpr AStringView s_AssetTypeMaterialBind = "material_bind";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static CompactString CanonicalAssetType(const Metascript::Document& doc){
    const auto assetType = doc.assetType();
    return CompactString(AStringView(assetType.data(), assetType.size()));
}

static bool ParseMaterialBindDocument(const Path& bindFilePath, ShaderCook::CookArena& arena, Metascript::Document& outDoc){
    CookString bindText{arena};
    if(!ReadTextFile(bindFilePath, bindText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to read Bind '{}'"), PathToString<tchar>(bindFilePath));
        return false;
    }
    StripUtf8Bom(bindText);

    if(!outDoc.parse(AStringView(bindText))){
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

static bool ValidatePairedSourceExtension(const Path& bindFilePath, const CookString& sourcePath){
    Core::Alloc::ScratchArena<> scratchArena;
    AString<Core::Alloc::ScratchArena<>> extension = PathToString(scratchArena, Path(sourcePath).extension());
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

static bool IsSupportedMaterialBindFieldType(const AStringView typeName){
    const auto matchesScalarOrVector = [typeName](const AStringView scalarName){
        if(typeName == scalarName)
            return true;
        if(typeName.size() != scalarName.size() + 1u)
            return false;
        if(typeName.substr(0u, scalarName.size()) != scalarName)
            return false;

        const char suffix = typeName[scalarName.size()];
        if(suffix < '2' || suffix > '4')
            return false;

        return true;
    };

    return
        matchesScalarOrVector("float")
        || matchesScalarOrVector("int")
        || matchesScalarOrVector("uint")
        || matchesScalarOrVector("bool")
    ;
}

static bool IsMaterialBindBlockClassAttribute(const AStringView attributeName){
    return attributeName == "material_constant" || attributeName == "material_mutable";
}

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

static bool ParseMaterialBindAttributeList(
    const Path& bindFilePath,
    const Metascript::Value* attributesValue,
    const AStringView contextLabel,
    ShaderCook::CookArena& arena,
    ShaderCook::CookVector<MaterialBindAttribute>& outAttributes
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

static bool ValidateMaterialBindStructAttributes(
    const Path& bindFilePath,
    const MaterialBindStruct& bindStruct
){
    bool foundBlockClass = false;

    for(const MaterialBindAttribute& attribute : bindStruct.attributes){
        if(!IsMaterialBindBlockClassAttribute(attribute.name)){
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

static bool ValidateMaterialBindFieldAttributes(const Path& bindFilePath, const MaterialBindStruct& bindStruct, const MaterialBindField& field){
    bool foundDefault = false;

    for(const MaterialBindAttribute& attribute : field.attributes){
        if(attribute.name != "default"){
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

static bool ParseMaterialBindField(
    const Path& bindFilePath,
    const Metascript::Value& fieldValue,
    const MaterialBindStruct& bindStruct,
    ShaderCook::CookArena& arena,
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
    if(!IsMaterialBindIdentifier(outField.type) || !IsSupportedMaterialBindFieldType(outField.type)){
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

static bool ParseMaterialBindStruct(
    const Path& bindFilePath,
    const Metascript::MStringView structName,
    const Metascript::Value& structValue,
    ShaderCook::CookArena& arena,
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

static bool ParseMaterialBindStructs(const Path& bindFilePath, const Metascript::Value& asset, ShaderCook::CookArena& arena, ShaderCook::CookVector<MaterialBindStruct>& outStructs){
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

static bool ParseMaterialBindInstances(const Path& bindFilePath, const Metascript::Value& asset, ShaderCook::CookArena& arena, MaterialBindEntry& outEntry){
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

static bool IsMaterialBindAssetField(const AStringView fieldName){
    return fieldName == "structs" || fieldName == "instances";
}

static bool ValidateMaterialBindAssetFields(const Path& bindFilePath, const Metascript::Value& asset){
    for(const auto& [fieldName, fieldValue] : asset.asMap()){
        static_cast<void>(fieldValue);

        const AStringView fieldNameText(fieldName.data(), fieldName.size());
        if(IsMaterialBindAssetField(fieldNameText))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': unsupported asset field '{}'")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(fieldNameText)
        );
        return false;
    }
    return true;
}

static bool ParseMaterialBindSource(const Path& bindFilePath, const Metascript::Document& doc, ShaderCook::CookArena& arena, MaterialBindEntry& outEntry){
    outEntry.reset();

    if(CanonicalAssetType(doc).view() != s_AssetTypeMaterialBind){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset type must be '{}'")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(s_AssetTypeMaterialBind)
        );
        return false;
    }

    outEntry.source = PathToString(arena, bindFilePath);
    if(!ValidatePairedSourceExtension(bindFilePath, outEntry.source))
        return false;

    const Metascript::Value* assetValue = FindAssetMapValue(bindFilePath, doc);
    if(!assetValue)
        return false;
    if(!ValidateMaterialBindAssetFields(bindFilePath, *assetValue))
        return false;

    if(!ParseMaterialBindStructs(bindFilePath, *assetValue, arena, outEntry.structs))
        return false;
    if(!ParseMaterialBindInstances(bindFilePath, *assetValue, arena, outEntry))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// material bind lookup helpers


static bool SplitMaterialBindParameterName(const AStringView parameterName, AStringView& outInstanceName, AStringView& outFieldName){
    const usize separatorIndex = parameterName.find('.');
    if(separatorIndex == AStringView::npos || separatorIndex == 0u || separatorIndex + 1u >= parameterName.size())
        return false;

    outInstanceName = parameterName.substr(0u, separatorIndex);
    outFieldName = parameterName.substr(separatorIndex + 1u);
    return true;
}



};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const MaterialBindStruct* MaterialBindEntry::findStruct(const AStringView typeName)const{
    for(const MaterialBindStruct& bindStruct : structs){
        if(AStringView(bindStruct.name) == typeName)
            return &bindStruct;
    }
    return nullptr;
}

const MaterialBindAttribute* MaterialBindField::findAttribute(const AStringView attributeName)const{
    for(const MaterialBindAttribute& attribute : attributes){
        if(AStringView(attribute.name) == attributeName)
            return &attribute;
    }
    return nullptr;
}

AStringView MaterialBindField::defaultArgument()const{
    const MaterialBindAttribute* attribute = findAttribute("default");
    return (attribute && attribute->arguments.size() == 1u) ? AStringView(attribute->arguments[0u]) : AStringView();
}

const MaterialBindField* MaterialBindStruct::findField(const AStringView fieldName)const{
    for(const MaterialBindField& field : fields){
        if(AStringView(field.name) == fieldName)
            return &field;
    }
    return nullptr;
}

const MaterialBindAttribute* MaterialBindStruct::findAttribute(const AStringView attributeName)const{
    for(const MaterialBindAttribute& attribute : attributes){
        if(AStringView(attribute.name) == attributeName)
            return &attribute;
    }
    return nullptr;
}

const MaterialBindInstance* MaterialBindEntry::findInstance(const AStringView instanceName)const{
    for(const MaterialBindInstance& instance : instances){
        if(AStringView(instance.name) == instanceName)
            return &instance;
    }
    return nullptr;
}

const MaterialBindField* MaterialBindEntry::findParameter(
    const AStringView parameterName,
    const MaterialBindInstance** outInstance,
    const MaterialBindStruct** outStruct
)const{
    if(outInstance)
        *outInstance = nullptr;
    if(outStruct)
        *outStruct = nullptr;

    AStringView instanceName;
    AStringView fieldName;
    if(!__hidden_material_bind::SplitMaterialBindParameterName(parameterName, instanceName, fieldName))
        return nullptr;

    const MaterialBindInstance* instance = findInstance(instanceName);
    if(!instance)
        return nullptr;

    const MaterialBindStruct* bindStruct = findStruct(AStringView(instance->type));
    const MaterialBindField* field = bindStruct ? bindStruct->findField(fieldName) : nullptr;
    if(!field)
        return nullptr;

    if(outInstance)
        *outInstance = instance;
    if(outStruct)
        *outStruct = bindStruct;
    return field;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseMaterialBindSource(const Path& bindFilePath, MaterialBindEntry& outEntry){
    outEntry.reset();

    ShaderCook::CookArena& arena = outEntry.source.get_allocator().arena();
    Metascript::Document doc(arena);
    if(!__hidden_material_bind::ParseMaterialBindDocument(bindFilePath, arena, doc))
        return false;

    return __hidden_material_bind::ParseMaterialBindSource(bindFilePath, doc, arena, outEntry);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

