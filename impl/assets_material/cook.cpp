// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "cook_private.h"
#include "bind.h"
#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>
#include <global/hash_utils.h>
#include <global/text_utils.h>
#include "material_avboit_shader_generator.h"
#include "material_shadow_dispatch.h"
#include <impl/assets_material/bind_parameter_application.adhd.h>
#include <impl/assets_material/bind_source_emitter.adhd.h>
#include <impl/assets_material/layout_validation.adhd.h>
#include <impl/assets_material/material_binary_writer.adhd.h>
#include <impl/assets_material/shadow_surface_source_builder.adhd.h>
#include <impl/assets_shader/cook.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookString = MaterialCookString;
using Core::Assets::ScratchArena;
using Core::Assets::ScratchString;
using Core::Assets::ScratchVector;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ApplyMaterialBindTypedLayoutParameterValue(
    const MaterialBindTypedLayout& layout,
    const Name& materialName,
    const ACompactString& parameterName,
    AStringView parameterValue,
    Material::ResourceReferenceVector& outResourceReferences,
    Material::TypedBlockByteVector& inOutBlockBytes
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(MaterialTypedValueData) == sizeof(u32) * s_MaterialTypedValueRecordWordCount, "MaterialTypedValueData layout must stay stable for typed value parsing");
static_assert(alignof(MaterialTypedValueData) >= alignof(UInt4), "MaterialTypedValueData must stay SIMD-aligned");
static_assert(IsTriviallyCopyable_V<MaterialTypedValueData>, "MaterialTypedValueData must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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


namespace MaterialCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ShadowSurfaceBindNamespaceLookup = HashMap<Name, u32, ScratchArena, Hasher<Name>, EqualTo<Name>>;
using ShadowSurfaceBindAliasVector = CookVector<CookString>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AssignGeneratedMaterialShaderSource(const ScratchPath& sourcePath, ScratchArena& scratchArena, CookString& inOutSource){
    ScratchString sourceText = PathToString(scratchArena, sourcePath);
    Replace(sourceText.begin(), sourceText.end(), '\\', '/');
    inOutSource += sourceText;
}

static Expected<ScratchString> BuildMaterialBindIncludeVirtualPathImpl(const MaterialBindEntry& entry, ScratchArena& arena){
    if(!ValidateMaterialBindIncludePath(AStringView(entry.virtualPath), AStringView(entry.source)))
        return MakeUnexpected(Failure{});

    ScratchString includePath(AStringView(entry.virtualPath), arena);
    includePath += s_MaterialBindSourceExtensionText;
    return includePath;
}

static b8 TryResolveMaterialBindDependencyInterface(const ScratchPath& normalizedMaterialBindIncludeRoot, const Path& dependency, ScratchArena& scratchArena, ScratchString& outInterfacePath){
    outInterfacePath.clear();
    if(normalizedMaterialBindIncludeRoot.empty())
        return true;

    auto normalizedDependencyResult = AbsolutePath(ScratchPath(scratchArena, dependency));
    if(!normalizedDependencyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize shader dependency '{}': {}"), PathToString<tchar>(dependency), ConvertString(normalizedDependencyResult.error().message()));
        return false;
    }
    ScratchPath normalizedDependency = normalizedDependencyResult->lexicallyNormal();

    if(!PathHasDirectoryAncestor(normalizedDependency, normalizedMaterialBindIncludeRoot))
        return true;

    ScratchString extension = PathToString(scratchArena, normalizedDependency.extensionView());
    CanonicalizeTextInPlace(extension);
    if(extension != s_MaterialBindSourceExtensionText)
        return true;

    ScratchPath relativePath = normalizedDependency.lexicallyRelative(normalizedMaterialBindIncludeRoot);
    relativePath.replaceExtension();
    if(!Core::Assets::AssetPathsDetail::BuildRelativeAssetPathText(relativePath, outInterfacePath)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to derive interface from generated include '{}'"), PathToString<tchar>(normalizedDependency));
        return false;
    }

    return true;
}

static const MaterialBindEntry* FindMaterialBindEntry(const Span<const MaterialBindEntry> materialBindEntries, const Name& interfaceName){
    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        const auto bindInterfaceName = Name::FromText(AStringView(bindEntry.virtualPath));
        if(bindInterfaceName && *bindInterfaceName == interfaceName)
            return &bindEntry;
    }
    return nullptr;
}

Expected<MaterialBindInterfaceLookup> BuildMaterialBindInterfaceLookup(const Span<const MaterialBindEntry> materialBindEntries, ScratchArena& scratchArena){
    MaterialBindInterfaceLookup lookup(0, Hasher<Name>(), EqualTo<Name>(), scratchArena);
    lookup.reserve(materialBindEntries.size());
    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        const auto interfaceName = Name::FromText(AStringView(bindEntry.virtualPath));
        if(!interfaceName)
            return MakeUnexpected(Failure{});
        if(!lookup.emplace(*interfaceName, MakeNotNull(&bindEntry)).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind interface '{}' is duplicated"), ConvertString(AStringView(bindEntry.virtualPath)));
            return MakeUnexpected(Failure{});
        }
    }
    return lookup;
}

static b8 HasProjectAssetVirtualRoot(const AStringView virtualPath, ScratchArena& scratchArena){
    const auto virtualRoot = Core::Assets::AssetPathsDetail::ExtractAssetVirtualRoot(virtualPath, scratchArena);
    if(!virtualRoot)
        return false;

    return virtualRoot->view() == Core::Assets::s_ProjectVirtualRoot;
}

static Expected<CookString> ParseVariantField(const Path& nwbFilePath, const Core::Metascript::Value& asset, const AStringView fieldName, CookArena& arena, ScratchArena& scratchArena){
    const auto* variantValue = asset.findField(fieldName);
    if(!variantValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' is required"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }

    ScratchString rawVariant{scratchArena};
    AStringView rawVariantView;
    if(variantValue->isList()){
        const auto& list = variantValue->asList();
        usize rawVariantSize = list.empty() ? 0u : list.size() - 1u;
        for(usize i = 0u; i < list.size(); ++i){
            if(!list[i].isString()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' list elements must be strings"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
                return MakeUnexpected(Failure{});
            }
            rawVariantSize += list[i].asString().size();
        }

        rawVariant.reserve(rawVariantSize);
        for(usize i = 0u; i < list.size(); ++i){
            if(i > 0)
                rawVariant += ';';
            const Core::Metascript::MStringView variantText = list[i].asString();
            rawVariant.append(variantText.data(), variantText.size());
        }
        rawVariantView = rawVariant;
    }
    else if(variantValue->isString()){
        const Core::Metascript::MStringView variantText = variantValue->asString();
        rawVariantView = variantText;
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must be a string or list of strings"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }

    rawVariantView = TrimView(rawVariantView);
    if(rawVariantView.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must not be empty"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }
    if(rawVariantView == Core::ShaderArchive::s_DefaultVariant)
        return CookString(Core::ShaderArchive::s_DefaultVariant, arena);

    using ScratchDefineCombo = HashMap<AStringView, AStringView, ScratchArena, Hasher<AStringView>, EqualTo<AStringView>>;
    ScratchDefineCombo assignments(0, Hasher<AStringView>(), EqualTo<AStringView>(), scratchArena);
    usize assignmentReserve = 1u;
    for(const char ch : rawVariantView){
        if(ch == ';')
            ++assignmentReserve;
    }
    assignments.reserve(assignmentReserve);

    const auto logInvalidVariant = [&](){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' has invalid variant signature '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName), ConvertString(rawVariantView));
    };

    const b8 parsedAssignments = ShaderCook::ParseVariantSignatureAssignments(
        rawVariantView,
        [&](const AStringView defineName, const AStringView defineValue) -> b8{
            if(!assignments.emplace(defineName, defineValue).second){
                logInvalidVariant();
                return false;
            }
            return true;
        },
        [&](const ShaderBinaryPayload::VariantSignatureSegmentError::Enum, const AStringView){
            logInvalidVariant();
        });
    if(!parsedAssignments)
        return MakeUnexpected(Failure{});

    return ShaderCook::BuildCanonicalVariantName(assignments, arena, scratchArena);
}

static Expected<MaterialCookEntry::StageShaderMap> ParseMaterialStageShaders(const Path& nwbFilePath, const Core::Metascript::Value& asset, CookArena& arena, ScratchArena& scratchArena){
    MaterialCookEntry::StageShaderMap stageShaders(0u, Hasher<Core::ShaderType::Enum>(), EqualTo<Core::ShaderType::Enum>(), arena);

    const auto* shadersValue = asset.findField(s_MaterialMetaShadersField);
    // Omitted shaders are generated from the surface hook during cross-asset preparation.
    if(!shadersValue)
        return stageShaders;
    if(!shadersValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shaders must be a map"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    stageShaders.reserve(shadersValue->asMap().size());

    for(const auto& [stageKey, shaderValue] : shadersValue->asMap()){
        const AStringView stageKeyText(stageKey);
        if(!shaderValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader '{}' must be a string"), PathToString<tchar>(nwbFilePath), ConvertString(stageKeyText));
            return MakeUnexpected(Failure{});
        }

        const AStringView shaderPath = TrimView(shaderValue.asString());
        if(!HasProjectAssetVirtualRoot(shaderPath, scratchArena)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage '{}' must use the project/ virtual root"), PathToString<tchar>(nwbFilePath), ConvertString(stageKeyText));
            return MakeUnexpected(Failure{});
        }

        const auto stageName = ToName(stageKeyText);
        const auto shaderName = ToName(shaderPath);
        if(!stageName || !*stageName || !shaderName || !*shaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage entries must not be empty"), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
        const Core::ShaderType::Enum shaderType = Core::ShaderStageNames::ShaderTypeFromArchiveStageName(**stageName);
        Core::Assets::AssetRef<IShader> shaderAsset;
        shaderAsset.virtualPath = **shaderName;
        if(!Core::ShaderType::IsValid(shaderType) || !shaderAsset.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage entries must not be empty"), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
        if(!IsSupportedMaterialShaderStage(shaderType)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage '{}' is not supported by the ECS renderer material contract; only 'mesh' and 'ps' are allowed"), PathToString<tchar>(nwbFilePath), ConvertString(stageKeyText));
            return MakeUnexpected(Failure{});
        }

        if(!stageShaders.emplace(shaderType, shaderAsset).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': duplicate shader stage '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(stageKeyText));
            return MakeUnexpected(Failure{});
        }
    }

    if(stageShaders.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shaders must not be empty"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    return stageShaders;
}

static b8 ValidateMaterialOpticalStageContract(const Path& nwbFilePath, const MaterialCookEntry& entry){
    if(IsMaterialOpticalStageCombinationValid(!entry.stageShaders.empty(), !entry.surfaceSource.empty(), entry.transparent, entry.refractive))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': explicit 'shaders' cannot be used with transparent/refractive materials; author a project 'surface' hook so AVBOIT and shadow optical passes use the material contract"), PathToString<tchar>(nwbFilePath));
    return false;
}

static Expected<MaterialCookEntry::ParameterMap> ParseMaterialParameters(const Path& nwbFilePath, const Core::Metascript::Value& asset, CookArena& arena){
    MaterialCookEntry::ParameterMap parameters(0u, Hasher<ACompactString>(), EqualTo<ACompactString>(), arena);

    const auto* parametersValue = asset.findField(s_MaterialMetaParametersField);
    if(!parametersValue)
        return parameters;
    if(!parametersValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameters must be a map"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    parameters.reserve(parametersValue->asMap().size());

    auto appendParameter = [&](const AStringView paramKeyText, const Core::Metascript::Value& paramValue) -> b8{
        if(!paramValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}' must be a string"), PathToString<tchar>(nwbFilePath), ConvertString(paramKeyText));
            return false;
        }

        ACompactString key;
        ACompactString value;
        const AStringView paramValueText(paramValue.asString().data(), paramValue.asString().size());
        if(!key.assign(paramKeyText) || !value.assign(paramValueText)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}' exceeds ACompactString capacity"), PathToString<tchar>(nwbFilePath), ConvertString(paramKeyText));
            return false;
        }
        if(!key){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter names must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }

        if(!parameters.emplace(key, value).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': duplicate parameter '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(key.view()));
            return false;
        }

        return true;
    };

    for(const auto& [paramKey, paramValue] : parametersValue->asMap()){
        const AStringView paramKeyText(paramKey.data(), paramKey.size());
        if(paramValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface parameter '{}' must be declared inside a block map"), PathToString<tchar>(nwbFilePath), ConvertString(paramKeyText));
            return MakeUnexpected(Failure{});
        }

        if(!paramValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}' must be a block map"), PathToString<tchar>(nwbFilePath), ConvertString(paramKeyText));
            return MakeUnexpected(Failure{});
        }
        if(paramKeyText.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter block names must not be empty"), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }

        for(const auto& [blockParamKey, blockParamValue] : paramValue.asMap()){
            const AStringView blockParamKeyText(blockParamKey.data(), blockParamKey.size());
            if(blockParamKeyText.empty()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter names in block '{}' must not be empty"), PathToString<tchar>(nwbFilePath), ConvertString(paramKeyText));
                return MakeUnexpected(Failure{});
            }

            const Expected<ACompactString> flattenedKey = BuildMaterialBindParameterKey(paramKeyText, blockParamKeyText);
            if(!flattenedKey){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}.{}' exceeds ACompactString capacity"), PathToString<tchar>(nwbFilePath), ConvertString(paramKeyText), ConvertString(blockParamKeyText));
                return MakeUnexpected(Failure{});
            }

            if(!appendParameter(flattenedKey->view(), blockParamValue))
                return MakeUnexpected(Failure{});
        }
    }

    return parameters;
}

static b8 HasMatchingVirtualAssetExtension(const ::Path<ScratchArena>& virtualPath, const AStringView requiredExtension, ScratchArena& scratchArena){
    ScratchString extension = PathToString(scratchArena, virtualPath.extensionView());
    CanonicalizeTextInPlace(extension);
    return AStringView(extension) == requiredExtension;
}

static Expected<CookString> ParseMaterialInterface(const Path& nwbFilePath, const Core::Metascript::Value& asset, CookArena& arena, ScratchArena& scratchArena){
    const auto* interfaceValue = asset.findField(s_MaterialMetaInterfaceField);
    if(!interfaceValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface is required"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    if(!interfaceValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must be a string"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    const Core::Metascript::MStringView interfaceText = interfaceValue->asString();
    const AStringView interfacePath = TrimView(AStringView(interfaceText.data(), interfaceText.size()));
    if(interfacePath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must not be empty"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    if(!HasProjectAssetVirtualRoot(interfacePath, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must use the project/ virtual root " "(e.g. 'project/shaders/surface.bind')"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    // The interface names a `.bind`; strip the extension to match the discovered virtual path.
    ::Path<ScratchArena> interfacePathPath(scratchArena, interfacePath);
    if(!HasMatchingVirtualAssetExtension(interfacePathPath, s_MaterialBindSourceExtensionText, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must reference a .bind file"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    interfacePathPath.replaceExtension();
    ScratchString strippedInterface = PathToString(scratchArena, interfacePathPath);
    Replace(strippedInterface.begin(), strippedInterface.end(), '\\', '/');

    // Store path text; the Name hash is produced on demand.
    const auto interfaceName = Name::FromText(AStringView(strippedInterface));
    if(!interfaceName || !*interfaceName){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface '{}' is invalid"), PathToString<tchar>(nwbFilePath), ConvertString(interfacePath));
        return MakeUnexpected(Failure{});
    }
    return CookString(AStringView(strippedInterface), arena);
}

// Parses a `project/`-rooted virtual path field; the cross-asset phase resolves it.
static Expected<CookString> ParseMaterialVirtualAssetField(const Path& nwbFilePath, const Core::Metascript::Value& asset, const AStringView fieldName, const AStringView requiredExtension, CookArena& arena, ScratchArena& scratchArena){
    const auto* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return CookString(arena);
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must be a string"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }

    const Core::Metascript::MStringView fieldText = fieldValue->asString();
    const AStringView virtualPath = TrimView(AStringView(fieldText.data(), fieldText.size()));
    if(virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must not be empty"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }

    if(!HasProjectAssetVirtualRoot(virtualPath, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must use the project/ virtual root " "(e.g. 'project/shaders/name{}')"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName), ConvertString(requiredExtension));
        return MakeUnexpected(Failure{});
    }

    const ::Path<ScratchArena> virtualPathPath(scratchArena, virtualPath);
    if(!HasMatchingVirtualAssetExtension(virtualPathPath, requiredExtension, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must reference a {} file"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName), ConvertString(requiredExtension));
        return MakeUnexpected(Failure{});
    }

    CookString normalized(virtualPath, arena);
    Replace(normalized.begin(), normalized.end(), '\\', '/');
    return normalized;
}

static Expected<b8> ParseMaterialBoolProperty(const Path& nwbFilePath, const Core::Metascript::Value& asset, const AStringView fieldName){
    const auto* propertyValue = asset.findField(fieldName);
    if(!propertyValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': '{}' is required and must be 0 or 1"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }
    if(propertyValue->isInteger()){
        const i64 propertyInt = propertyValue->asInteger();
        if(propertyInt == 0 || propertyInt == 1)
            return propertyInt != 0;
    }
    NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': '{}' must be 0 or 1"), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
    return MakeUnexpected(Failure{});
}

static b8 AppendShadowSurfaceBindAliases(
    const MaterialBindEntry& bindEntry,
    const AStringView bindNamespace,
    ShadowSurfaceBindAliasVector& outAliasSymbols,
    CookHashSet<CookString>& inOutSeenSymbols,
    CookString& inOutSource
){
    return ShadowSurfaceSourceBuilder().appendBindAliases(
        AStringView(bindEntry.virtualPath),
        bindEntry.structs,
        bindEntry.instances,
        bindNamespace,
        outAliasSymbols,
        inOutSeenSymbols,
        inOutSource
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialAvboitShaderDetail{


using namespace MaterialCookDetail;


struct MaterialAvboitPassPixelShaderSpec{
    AStringView generatedDirectoryLeaf;
    AStringView authoringHeaderInclude;
    AStringView passLabel;
    AStringView generatedNamePrefix;
    MaterialCookString MaterialCookEntry::* entryShaderNameField = nullptr;
};


static constexpr MaterialAvboitPassPixelShaderSpec s_AvboitAccumulatePassSpec = {
    AStringView("material_avboit_accumulate_pixel_shaders"),
    AStringView("avboit/accumulate_ps_authoring.slangi"),
    s_MaterialAvboitAccumulatePassLabel,
    s_AvboitAccumulatePixelShaderGeneratedPrefix,
    &MaterialCookEntry::avboitAccumulatePixelShaderName,
};

static constexpr MaterialAvboitPassPixelShaderSpec s_AvboitOccupancyPassSpec = {
    AStringView("material_avboit_occupancy_pixel_shaders"),
    AStringView("avboit/occupancy_ps_authoring.slangi"),
    s_MaterialAvboitOccupancyPassLabel,
    s_AvboitOccupancyPixelShaderGeneratedPrefix,
    &MaterialCookEntry::avboitOccupancyPixelShaderName,
};

static constexpr MaterialAvboitPassPixelShaderSpec s_AvboitExtinctionPassSpec = {
    AStringView("material_avboit_extinction_pixel_shaders"),
    AStringView("avboit/extinction_ps_authoring.slangi"),
    s_MaterialAvboitExtinctionPassLabel,
    s_AvboitExtinctionPixelShaderGeneratedPrefix,
    &MaterialCookEntry::avboitExtinctionPixelShaderName,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared body for the three AVBOIT pass-PS generators; opaque materials are skipped.
static Expected<CookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitPassPixelShadersImpl(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialAvboitPassPixelShaderSpec& spec,
    CookArena& arena,
    ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    CookVector<GeneratedMaterialPixelShader> generatedShaders(arena);
    usize generatedShaderCount = 0u;
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.transparent && !entry.surfaceSource.empty())
            ++generatedShaderCount;
    }
    generatedShaders.reserve(generatedShaderCount);

    const ScratchPath generatedRoot = ScratchPath(scratchArena, cacheDirectory) / configurationSafeName / s_GeneratedCacheDirectory / spec.generatedDirectoryLeaf;
    auto removeAllIfExistsResult2 = RemoveAllIfExists(generatedRoot);
    if(!removeAllIfExistsResult2){
        NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to clear generated directory '{}': {}"), ConvertString(spec.passLabel), PathToString<tchar>(generatedRoot), ConvertString(removeAllIfExistsResult2.error().message()));
        return MakeUnexpected(Failure{});
    }

    for(MaterialCookEntry& entry : materialEntries){
        if(!entry.transparent)
            continue;
        if(entry.surfaceSource.empty())
            continue;
        if(!ValidateTransparentMaterialInterface(AStringView(entry.virtualPath), AStringView(entry.materialInterface), spec.passLabel))
            return MakeUnexpected(Failure{});

        // Same .bind + .surface pair as the G-buffer PS.
        CookString generatedSource(arena);
        BuildGeneratedMaterialPixelShaderSource(
            "// Generated per-material AVBOIT pass pixel shader: engine AVBOIT pass authoring + this material's\n",
            spec.authoringHeaderInclude,
            AStringView(entry.materialInterface),
            AStringView(entry.surfaceSource),
            generatedSource
        );

        ScratchString relativeFile(scratchArena);
        relativeFile += entry.virtualPath;
        relativeFile += s_GeneratedShaderSourceExtension;
        const ScratchPath outputPath = generatedRoot / AStringView(relativeFile);
        auto ensureDirectoriesResult2 = EnsureDirectories(outputPath.parentPath());
        if(!ensureDirectoriesResult2){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to create generated parent '{}': {}")
                , ConvertString(spec.passLabel)
                , PathToString<tchar>(outputPath.parentPathView())
                , ConvertString(ensureDirectoriesResult2.error().message())
            );
            return MakeUnexpected(Failure{});
        }
        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to write generated pixel shader '{}'"), ConvertString(spec.passLabel), PathToString<tchar>(outputPath));
            return MakeUnexpected(Failure{});
        }

        GeneratedMaterialPixelShader generated(arena);
        generated.name += spec.generatedNamePrefix;
        generated.name += entry.virtualPath;
        AssignGeneratedMaterialShaderSource(outputPath, scratchArena, generated.source);

        const auto pixelShaderName = ToName(AStringView(generated.name));
        if(!pixelShaderName || !*pixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: generated AVBOIT {} pixel shader name is invalid for material '{}'"), ConvertString(spec.passLabel), ConvertString(AStringView(entry.virtualPath)));
            return MakeUnexpected(Failure{});
        }

        // Record the PS identity so the clip collector and the transparent draw share the SAME key.
        (entry.*(spec.entryShaderNameField)).assign(AStringView(generated.name));

        generatedShaders.push_back(Move(generated));
    }

    return generatedShaders;
}


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


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> MaterialAvboitShaderGenerator::EmitAccumulate(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    using namespace MaterialAvboitShaderDetail;
    return EmitMaterialAvboitPassPixelShadersImpl(cacheDirectory, configurationSafeName, s_AvboitAccumulatePassSpec, arena, scratchArena, materialEntries);
}

Expected<MaterialCookVector<GeneratedMaterialPixelShader>> MaterialAvboitShaderGenerator::EmitOccupancy(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    using namespace MaterialAvboitShaderDetail;
    return EmitMaterialAvboitPassPixelShadersImpl(cacheDirectory, configurationSafeName, s_AvboitOccupancyPassSpec, arena, scratchArena, materialEntries);
}

Expected<MaterialCookVector<GeneratedMaterialPixelShader>> MaterialAvboitShaderGenerator::EmitExtinction(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    using namespace MaterialAvboitShaderDetail;
    return EmitMaterialAvboitPassPixelShadersImpl(cacheDirectory, configurationSafeName, s_AvboitExtinctionPassSpec, arena, scratchArena, materialEntries);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookString> BuildMaterialBindIncludeSource(const MaterialBindEntry& entry, MaterialCookArena& arena, Core::Alloc::ScratchArena&){
    MaterialCookString source(arena);
    if(!MaterialBindSourceEmitter{}.buildMaterialBindIncludeSourceFromData(
        AStringView(entry.source),
        AStringView(entry.virtualPath),
        entry.structs,
        entry.instances,
        source
    ))
        return MakeUnexpected(Failure{});
    return source;
}

Expected<Path> EmitMaterialBindIncludes(const Path& cacheDirectory, const AStringView configurationSafeName, const Span<const MaterialBindEntry> materialBindEntries, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace MaterialCookDetail;
    Path includeRoot = cacheDirectory / configurationSafeName / s_MaterialBindGeneratedIncludeCacheDirectoryText;
    if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, s_MaterialBindIncludeGenerationLabel))
        return MakeUnexpected(Failure{});
    if(materialBindEntries.empty())
        return includeRoot;

    ScratchHashSet<ScratchString> seenIncludePaths{0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena};
    seenIncludePaths.reserve(materialBindEntries.size());

    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        auto includePathResult = BuildMaterialBindIncludeVirtualPathImpl(bindEntry, scratchArena);
        if(!includePathResult)
            return MakeUnexpected(Failure{});
        ScratchString includePath = Move(*includePathResult);
        if(!seenIncludePaths.insert(includePath).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: duplicate material bind include path '{}'"), ConvertString(includePath));
            return MakeUnexpected(Failure{});
        }

        auto generatedSourceResult = BuildMaterialBindIncludeSource(bindEntry, arena, scratchArena);
        if(!generatedSourceResult)
            return MakeUnexpected(Failure{});
        const CookString& generatedSource = *generatedSourceResult;

        const ScratchPath outputPath = ScratchPath(scratchArena, includeRoot) / AStringView(includePath);
        auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
        if(!ensureDirectoriesResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult.error().message()));
            return MakeUnexpected(Failure{});
        }

        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to write generated include '{}'"), PathToString<tchar>(outputPath));
            return MakeUnexpected(Failure{});
        }
    }

    return includeRoot;
}

Expected<MaterialBindDependency> ResolveMaterialBindDependencyInterface(
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const Span<const MaterialBindEntry> materialBindEntries,
    const Span<const Path> dependencies,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace MaterialCookDetail;
    MaterialBindDependency result(arena);

    ScratchPath normalizedMaterialBindIncludeRoot(scratchArena);
    if(!materialBindIncludeRoot.empty()){
        const auto normalizedRoot = AbsolutePath(ScratchPath(scratchArena, materialBindIncludeRoot));
        if(!normalizedRoot){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize generated include root '{}': {}"), PathToString<tchar>(materialBindIncludeRoot), ConvertString(normalizedRoot.error().message()));
            return MakeUnexpected(Failure{});
        }
        normalizedMaterialBindIncludeRoot = normalizedRoot->lexicallyNormal();
    }

    ScratchString dependencyInterfacePath{scratchArena};
    b8 dependsOnMultipleInterfaces = false;
    for(const Path& dependency : dependencies){
        if(!TryResolveMaterialBindDependencyInterface(normalizedMaterialBindIncludeRoot, dependency, scratchArena, dependencyInterfacePath))
            return MakeUnexpected(Failure{});
        if(dependencyInterfacePath.empty())
            continue;

        const auto dependencyInterfaceNameResult = Name::FromText(AStringView(dependencyInterfacePath));
        if(!dependencyInterfaceNameResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: shader '{}' includes invalid generated " "material bind interface '{}'"), ConvertString(shaderName), ConvertString(dependencyInterfacePath));
            return MakeUnexpected(Failure{});
        }
        const Name dependencyInterfaceName = *dependencyInterfaceNameResult;

        // Each generated include comes from one authored file, and Gather already drops repeated paths, so this list has no repeats.
        const MaterialBindEntry* bindEntry = FindMaterialBindEntry(materialBindEntries, dependencyInterfaceName);
        if(!bindEntry){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: shader '{}' includes generated material bind interface '{}' with no authored source"), ConvertString(shaderName), ConvertString(dependencyInterfacePath));
            return MakeUnexpected(Failure{});
        }
        result.interfaceSources.emplace_back(arena, AStringView(bindEntry->source));

        // Reads typed material constants, so it needs the typed binding.
        result.dependsOnMaterialBind = true;
        if(dependsOnMultipleInterfaces)
            continue;

        if(!result.interfaceName){
            result.interfacePath = dependencyInterfacePath;
            result.interfaceName = dependencyInterfaceName;
            continue;
        }

        if(result.interfaceName != dependencyInterfaceName){
            // Generic dispatch consumer: no single owning interface.
            result.interfacePath.clear();
            result.interfaceName = s_NameNone;
            dependsOnMultipleInterfaces = true;
        }
    }

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AssignMaterialShadingModelIds(const Span<MaterialCookEntry> materialEntries, Core::Alloc::ScratchArena& scratchArena){
    using namespace MaterialCookDetail;
    for(const MaterialCookEntry& entry : materialEntries){
        if(!ValidateDeferredBxdfSource(AStringView(entry.virtualPath), AStringView(entry.bxdfSource)))
            return false;
    }

    Vector<AStringView, ScratchArena> uniqueSources(scratchArena);
    uniqueSources.reserve(materialEntries.size());
    for(const MaterialCookEntry& entry : materialEntries)
        uniqueSources.push_back(AStringView(entry.bxdfSource));
    Sort(uniqueSources.begin(), uniqueSources.end());

    uniqueSources.erase(Unique(uniqueSources.begin(), uniqueSources.end()), uniqueSources.end());
    if(!ValidateDeferredBxdfCount(uniqueSources.size()))
        return false;

    for(MaterialCookEntry& entry : materialEntries){
        const AStringView source(entry.bxdfSource);
        const auto sourceIt = LowerBound(uniqueSources.begin(), uniqueSources.end(), source);
        NWB_ASSERT(sourceIt != uniqueSources.end() && *sourceIt == source);
        entry.shadingModelId = static_cast<u32>(static_cast<usize>(sourceIt - uniqueSources.begin()));
    }

    // Opaque with explicit `shaders` takes the no-surface sentinel (never 0), so GI hits neutral, never a hook.
    Vector<AStringView, ScratchArena> uniqueSurfaces(scratchArena);
    uniqueSurfaces.reserve(materialEntries.size());
    for(const MaterialCookEntry& entry : materialEntries){
        if(!entry.surfaceSource.empty())
            uniqueSurfaces.push_back(AStringView(entry.surfaceSource));
    }
    Sort(uniqueSurfaces.begin(), uniqueSurfaces.end());

    uniqueSurfaces.erase(Unique(uniqueSurfaces.begin(), uniqueSurfaces.end()), uniqueSurfaces.end());
    if(!ValidateMaterialSurfaceCount(uniqueSurfaces.size()))
        return false;

    for(MaterialCookEntry& entry : materialEntries){
        if(entry.surfaceSource.empty()){
            entry.surfaceDispatchId = s_NoSurfaceDispatchId;
            continue;
        }

        const AStringView surface(entry.surfaceSource);
        const auto surfaceIt = LowerBound(uniqueSurfaces.begin(), uniqueSurfaces.end(), surface);
        NWB_ASSERT(surfaceIt != uniqueSurfaces.end() && *surfaceIt == surface);
        entry.surfaceDispatchId = static_cast<u32>(static_cast<usize>(surfaceIt - uniqueSurfaces.begin()));
    }
    return true;
}

Expected<Path> EmitDeferredBxdfDispatchModule(const Path& cacheDirectory, const AStringView configurationSafeName, const Span<const MaterialCookEntry> materialEntries, Core::Alloc::ScratchArena& scratchArena){
    using namespace MaterialCookDetail;
    Path includeRoot = cacheDirectory / configurationSafeName / s_DeferredBxdfIncludeSubDir;
    if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, "Deferred bxdf dispatch"))
        return MakeUnexpected(Failure{});

    // Each assigned BXDF id maps to one source; materials sharing that source share the slot.
    u32 maxId = 0u;
    b8 anyBxdf = false;
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.bxdfSource.empty())
            continue;
        anyBxdf = true;
        if(entry.shadingModelId > maxId)
            maxId = entry.shadingModelId;
    }

    Vector<AStringView, ScratchArena> sourceById(scratchArena);
    if(anyBxdf)
        sourceById.resize(static_cast<usize>(maxId) + 1u);
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.bxdfSource.empty())
            continue;

        const AStringView source(entry.bxdfSource);
        AStringView& slot = sourceById[entry.shadingModelId];
        if(!slot.empty() && slot != source){
            NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: shading model id {} maps to multiple bxdf sources"), entry.shadingModelId);
            return MakeUnexpected(Failure{});
        }
        slot = source;
    }

    const CookString source = BuildDeferredBxdfDispatchModuleSource(sourceById);

    const ScratchPath outputPath = ScratchPath(scratchArena, includeRoot) / s_DeferredBxdfModuleSubPath;
    auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult.error().message()));
        return MakeUnexpected(Failure{});
    }
    if(!WriteTextFile(outputPath, AStringView(source))){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to write generated include '{}'"), PathToString<tchar>(outputPath));
        return MakeUnexpected(Failure{});
    }
    return includeRoot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialPixelShaders(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const Name& sharedMeshShaderName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    using namespace MaterialCookDetail;
    CookVector<GeneratedMaterialPixelShader> generatedShaders(arena);
    usize generatedShaderCount = 0u;
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.stageShaders.empty() && !entry.surfaceSource.empty())
            ++generatedShaderCount;
    }
    generatedShaders.reserve(generatedShaderCount);

    if(!ValidateGeneratedMaterialMeshShader(generatedShaderCount, sharedMeshShaderName))
        return MakeUnexpected(Failure{});

    const ScratchPath generatedRoot = ScratchPath(scratchArena, cacheDirectory) / configurationSafeName / s_GeneratedCacheDirectory / "material_pixel_shaders";
    auto removeAllIfExistsResult = RemoveAllIfExists(generatedRoot);
    if(!removeAllIfExistsResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to clear generated directory '{}': {}"), PathToString<tchar>(generatedRoot), ConvertString(removeAllIfExistsResult.error().message()));
        return MakeUnexpected(Failure{});
    }

    for(MaterialCookEntry& entry : materialEntries){
        const b8 hasExplicitShaders = !entry.stageShaders.empty();
        const b8 hasSurface = !entry.surfaceSource.empty();
        if(!ValidateMaterialGeneratedPixelShaderEntry(
            AStringView(entry.virtualPath),
            hasExplicitShaders,
            hasSurface,
            AStringView(entry.materialInterface)
        ))
            return MakeUnexpected(Failure{});
        if(hasExplicitShaders)
            continue;

        // Engine authoring + .bind + surface hook; mesh stage is shared.
        CookString generatedSource(arena);
        BuildGeneratedMaterialPixelShaderSource(
            "// Generated per-material G-buffer pixel shader: engine pixel-shader authoring + this material's\n",
            "mesh/material_ps_authoring.slangi",
            AStringView(entry.materialInterface),
            AStringView(entry.surfaceSource),
            generatedSource
        );

        ScratchString relativeFile(scratchArena);
        relativeFile += entry.virtualPath;
        relativeFile += s_GeneratedShaderSourceExtension;
        const ScratchPath outputPath = generatedRoot / AStringView(relativeFile);
        auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
        if(!ensureDirectoriesResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to create generated parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult.error().message()));
            return MakeUnexpected(Failure{});
        }
        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to write generated pixel shader '{}'"), PathToString<tchar>(outputPath));
            return MakeUnexpected(Failure{});
        }

        GeneratedMaterialPixelShader generated(arena);
        generated.name += "generated/material_ps/";
        generated.name += entry.virtualPath;
        AssignGeneratedMaterialShaderSource(outputPath, scratchArena, generated.source);

        const auto pixelShaderName = ToName(AStringView(generated.name));
        if(!pixelShaderName || !*pixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: generated pixel shader name is invalid for material '{}'"), ConvertString(AStringView(entry.virtualPath)));
            return MakeUnexpected(Failure{});
        }

        Core::Assets::AssetRef<IShader> pixelShaderRef;
        pixelShaderRef.virtualPath = **pixelShaderName;
        Core::Assets::AssetRef<IShader> meshShaderRef;
        meshShaderRef.virtualPath = sharedMeshShaderName;
        if(!entry.stageShaders.emplace(Core::ShaderType::PixelStage, pixelShaderRef).second || !entry.stageShaders.emplace(Core::ShaderType::MeshStage, meshShaderRef).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: failed to assign generated stage shaders for material '{}'"), ConvertString(AStringView(entry.virtualPath)));
            return MakeUnexpected(Failure{});
        }

        generatedShaders.push_back(Move(generated));
    }

    return generatedShaders;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateMaterialCookInterfaces(const Span<const MaterialBindEntry> materialBindEntries, const Span<MaterialCookEntry> materialEntries, Core::Alloc::ScratchArena& scratchArena){
    using namespace MaterialCookDetail;
    auto materialBindLookupResult = BuildMaterialBindInterfaceLookup(materialBindEntries, scratchArena);
    if(!materialBindLookupResult)
        return false;
    const MaterialBindInterfaceLookup& materialBindLookup = *materialBindLookupResult;
    if(materialEntries.empty())
        return true;

    const usize cacheReserveCount = Min(materialBindEntries.size(), materialEntries.size());
    MaterialBindTypedLayoutCache layoutCache(materialEntries[0u].virtualPath.get_allocator().arena());
    layoutCache.reserve(cacheReserveCount);

    for(MaterialCookEntry& materialEntry : materialEntries){
        materialEntry.typedLayoutHash = 0u;
        materialEntry.typedLayoutBlocks.clear();
        materialEntry.typedLayoutFields.clear();
        materialEntry.typedBlockBytes.clear();
        materialEntry.resourceReferences.clear();

        if(materialEntry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' is missing required material interface"), ConvertString(AStringView(materialEntry.virtualPath)));
            return false;
        }

        const auto materialInterfaceNameResult = Name::FromText(AStringView(materialEntry.materialInterface));
        if(!materialInterfaceNameResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' references unknown material interface '{}'"), ConvertString(AStringView(materialEntry.virtualPath)), ConvertString(AStringView(materialEntry.materialInterface)));
            return false;
        }
        const Name materialInterfaceName = *materialInterfaceNameResult;
        const auto bindEntryIt = materialBindLookup.find(materialInterfaceName);
        if(bindEntryIt == materialBindLookup.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' references unknown material interface '{}'"), ConvertString(AStringView(materialEntry.virtualPath)), ConvertString(AStringView(materialEntry.materialInterface)));
            return false;
        }
        const MaterialBindEntry& bindEntry = *bindEntryIt.value();

        const auto layoutResult = FindOrBuildMaterialBindTypedLayout(materialInterfaceName, bindEntry, scratchArena, layoutCache);
        if(!layoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' failed to resolve typed layout cache for interface '{}'"), ConvertString(AStringView(materialEntry.virtualPath)), ConvertString(AStringView(materialEntry.materialInterface)));
            return false;
        }

        const MaterialBindTypedLayout& layout = **layoutResult;
        CopyMaterialBindTypedLayoutDefaults(layout, materialEntry.typedLayoutHash, materialEntry.typedLayoutBlocks, materialEntry.typedLayoutFields, materialEntry.typedBlockBytes);
        const auto materialName = Name::FromText(AStringView(materialEntry.virtualPath));
        if(!materialName)
            return false;
        if(!ApplyMaterialBindTypedLayoutParameters(layout, *materialName, materialEntry.parameters, materialEntry.typedBlockBytes, materialEntry.resourceReferences))
            return false;
        if(!ValidateMaterialResourceReferences(materialEntry.typedLayoutBlocks, materialEntry.typedLayoutFields, materialEntry.resourceReferences)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}': resource fields must be assigned engine or project asset paths in parameters"), ConvertString(AStringView(materialEntry.virtualPath)));
            return false;
        }
    }

    return true;
}

Expected<MaterialCookEntry> ParseMaterialCookMetadata(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace MaterialCookDetail;
    MaterialCookEntry entry(arena);

    const auto assetValue = Core::Assets::FindMetadataAssetMapValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, s_MaterialMetaDiagnosticPrefix);
    if(!assetValue)
        return MakeUnexpected(assetValue.error());
    const Core::Metascript::Value& asset = **assetValue;

    // Keep the material path as readable text for generated shader paths; validate its Name identity before storage.
    auto derivedVirtualPath = Core::Assets::AssetPathsDetail::BuildDerivedAssetVirtualPathText(assetRoot, virtualRoot, nwbFilePath, arena, scratchArena);
    if(!derivedVirtualPath)
        return MakeUnexpected(Failure{});
    const auto virtualPathName = Name::FromText(AStringView(*derivedVirtualPath));
    if(!virtualPathName || !*virtualPathName){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': failed to derive a valid virtual path"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    entry.virtualPath = Move(*derivedVirtualPath);
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_MaterialMetaDiagnosticPrefix, IsAllowedMaterialAssetField))
        return MakeUnexpected(Failure{});

    auto shaderVariant = ParseVariantField(nwbFilePath, asset, s_MaterialMetaShaderVariantField, arena, scratchArena);
    if(!shaderVariant)
        return MakeUnexpected(Failure{});
    entry.shaderVariant = Move(*shaderVariant);
    auto materialInterface = ParseMaterialInterface(nwbFilePath, asset, arena, scratchArena);
    if(!materialInterface)
        return MakeUnexpected(Failure{});
    entry.materialInterface = Move(*materialInterface);
    auto bxdfSource = ParseMaterialVirtualAssetField(nwbFilePath, asset, s_MaterialMetaBxdfField, s_MaterialBxdfExtension, arena, scratchArena);
    if(!bxdfSource)
        return MakeUnexpected(Failure{});
    entry.bxdfSource = Move(*bxdfSource);
    auto surfaceSource = ParseMaterialVirtualAssetField(nwbFilePath, asset, s_MaterialMetaSurfaceField, s_MaterialSurfaceExtension, arena, scratchArena);
    if(!surfaceSource)
        return MakeUnexpected(Failure{});
    entry.surfaceSource = Move(*surfaceSource);
    const auto transparent = ParseMaterialBoolProperty(nwbFilePath, asset, s_MaterialMetaTransparentField);
    if(!transparent)
        return MakeUnexpected(Failure{});
    entry.transparent = *transparent;
    const auto twoSided = ParseMaterialBoolProperty(nwbFilePath, asset, s_MaterialMetaTwoSidedField);
    if(!twoSided)
        return MakeUnexpected(Failure{});
    entry.twoSided = *twoSided;
    const auto refractive = ParseMaterialBoolProperty(nwbFilePath, asset, s_MaterialMetaRefractiveField);
    if(!refractive)
        return MakeUnexpected(Failure{});
    entry.refractive = *refractive;
    auto stageShaders = ParseMaterialStageShaders(nwbFilePath, asset, arena, scratchArena);
    if(!stageShaders)
        return MakeUnexpected(Failure{});
    entry.stageShaders = Move(*stageShaders);
    if(!ValidateMaterialOpticalStageContract(nwbFilePath, entry))
        return MakeUnexpected(Failure{});
    auto parameters = ParseMaterialParameters(nwbFilePath, asset, arena);
    if(!parameters)
        return MakeUnexpected(Failure{});
    entry.parameters = Move(*parameters);

    return entry;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Path> ShadowSurfaceDispatchGenerator::emit(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const Span<const MaterialBindEntry> materialBindEntries,
    const Span<const MaterialCookEntry> materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace MaterialCookDetail;
    Path includeRoot = cacheDirectory / configurationSafeName / s_ShadowSurfaceIncludeSubDir;
    if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, "Shadow surface dispatch"))
        return MakeUnexpected(Failure{});

    // Each surface id maps to one source and .bind interface; shared surfaces require the same interface because their hooks use fixed accessor names.
    u32 maxId = 0u;
    b8 anySurface = false;
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.surfaceSource.empty())
            continue;
        anySurface = true;
        if(entry.surfaceDispatchId > maxId)
            maxId = entry.surfaceDispatchId;
    }

    Vector<AStringView, ScratchArena> surfaceById(scratchArena);
    Vector<AStringView, ScratchArena> interfaceById(scratchArena);
    if(anySurface){
        surfaceById.resize(static_cast<usize>(maxId) + 1u);
        interfaceById.resize(static_cast<usize>(maxId) + 1u);
    }
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.surfaceSource.empty())
            continue;

        const AStringView surface(entry.surfaceSource);
        AStringView& surfaceSlot = surfaceById[entry.surfaceDispatchId];
        if(!surfaceSlot.empty() && surfaceSlot != surface){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: model id {} maps to multiple surface sources"), entry.surfaceDispatchId);
            return MakeUnexpected(Failure{});
        }
        surfaceSlot = surface;

        const AStringView interfaceName(entry.materialInterface);
        AStringView& interfaceSlot = interfaceById[entry.surfaceDispatchId];
        if(!interfaceSlot.empty() && interfaceSlot != interfaceName){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: model id {} maps to multiple material interfaces"), entry.surfaceDispatchId);
            return MakeUnexpected(Failure{});
        }
        interfaceSlot = interfaceName;
    }

    const auto bindEntryLookup = BuildMaterialBindInterfaceLookup(materialBindEntries, scratchArena);
    if(!bindEntryLookup)
        return MakeUnexpected(Failure{});

    Vector<const MaterialBindEntry*, ScratchArena> bindEntryById(scratchArena);
    Vector<u32, ScratchArena> bindNamespaceIdById(scratchArena);
    if(anySurface){
        bindEntryById.resize(surfaceById.size());
        bindNamespaceIdById.resize(surfaceById.size());
    }

    ShadowSurfaceBindNamespaceLookup bindNamespaceLookup(0, Hasher<Name>(), EqualTo<Name>(), scratchArena);
    bindNamespaceLookup.reserve(materialBindEntries.size());
    for(usize id = 0u; id < surfaceById.size(); ++id){
        if(surfaceById[id].empty())
            continue;

        const auto interfaceNameResult = Name::FromText(interfaceById[id]);
        if(!interfaceNameResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: model id {} references unknown material interface '{}'"), id, ConvertString(interfaceById[id]));
            return MakeUnexpected(Failure{});
        }
        const Name interfaceName = *interfaceNameResult;
        const auto bindEntryIt = bindEntryLookup->find(interfaceName);
        if(bindEntryIt == bindEntryLookup->end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: model id {} references unknown material interface '{}'"), id, ConvertString(interfaceById[id]));
            return MakeUnexpected(Failure{});
        }
        bindEntryById[id] = bindEntryIt.value().get();

        const auto namespaceIt = bindNamespaceLookup.find(interfaceName);
        if(namespaceIt == bindNamespaceLookup.end()){
            bindNamespaceIdById[id] = static_cast<u32>(id);
            if(!bindNamespaceLookup.emplace(interfaceName, static_cast<u32>(id)).second){
                NWB_ASSERT(false);
                return MakeUnexpected(Failure{});
            }
        }
        else
            bindNamespaceIdById[id] = namespaceIt.value();
    }

    const ShadowSurfaceSourceBuilder sourceBuilder;
    CookString source = sourceBuilder.buildDispatchHeader();

    // Context comes from the includer's shadow_surface.slangi; emitting here would force an unresolvable virtual include.

    // Emit each unique `.bind` once in a private namespace with scoped aliases so multiple interfaces do not collide.
    CookArena& cookArena = source.get_allocator().arena();
    CookHashSet<CookString> bindAliasSeenSymbols{ 0, Hasher<CookString>(), EqualTo<CookString>(), cookArena };
    ShadowSurfaceBindAliasVector bindAliasSymbols(cookArena);
    for(usize id = 0u; id < surfaceById.size(); ++id){
        if(surfaceById[id].empty())
            continue;

        const u32 bindNamespaceId = bindNamespaceIdById[id];
        const CookString bindNamespace = sourceBuilder.buildBindNamespace(bindNamespaceId);
        if(bindNamespaceId == static_cast<u32>(id))
            sourceBuilder.appendNamespaceInclude(AStringView(bindNamespace), interfaceById[id], source);
        if(!AppendShadowSurfaceBindAliases(*bindEntryById[id], AStringView(bindNamespace), bindAliasSymbols, bindAliasSeenSymbols, source))
            return MakeUnexpected(Failure{});
        sourceBuilder.appendInputDefines(source);
        sourceBuilder.appendHookInclude(static_cast<u32>(id), surfaceById[id], source);
        sourceBuilder.appendInputUndefines(source);
        sourceBuilder.appendBindAliasUndefines(bindAliasSymbols, source);
    }

    // Unknown id: no material surface is available (for example, an explicit opaque-stage material).
    // Shadow uses only neutral optical fields; GI may use baseColor, where fixed mid-grey is the no-surface fallback.
    sourceBuilder.appendDispatchFunction(surfaceById, source);

    const ScratchPath outputPath = ScratchPath(scratchArena, includeRoot) / s_ShadowSurfaceModuleSubPath;
    auto ensureDirectoriesResult2 = EnsureDirectories(outputPath.parentPath());
    if(!ensureDirectoriesResult2){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult2.error().message()));
        return MakeUnexpected(Failure{});
    }
    if(!WriteTextFile(outputPath, AStringView(source))){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: failed to write generated include '{}'"), PathToString<tchar>(outputPath));
        return MakeUnexpected(Failure{});
    }
    return includeRoot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using OptionalAvboitPixelShaderSetter = void(Material::*)(const Core::Assets::AssetRef<PixelShader>&);

template<OptionalAvboitPixelShaderSetter Setter>
static b8 SetOptionalAvboitPixelShader(const MaterialCookEntry& materialEntry, const AStringView shaderNameText, const AStringView passLabel, Material& outMaterial){
    static_assert(Setter != nullptr);
    Core::Assets::AssetRef<PixelShader> shaderRef;
    if(!TryResolveOptionalMaterialAvboitPixelShader(AStringView(materialEntry.virtualPath), shaderNameText, passLabel, shaderRef))
        return false;
    if(shaderRef.valid())
        (outMaterial.*Setter)(shaderRef);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Material> BuildMaterialAsset(const MaterialCookEntry& materialEntry, Core::Assets::AssetArena& arena){
    if(!ValidateMaterialCookEntryRequiredFields(
        AStringView(materialEntry.virtualPath),
        AStringView(materialEntry.materialInterface),
        materialEntry.typedLayoutHash,
        AStringView(materialEntry.shaderVariant)
    ))
        return MakeUnexpected(Failure{});

    const auto virtualPath = Name::FromText(AStringView(materialEntry.virtualPath));
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: virtual path contains an embedded null"));
        return MakeUnexpected(Failure{});
    }
    Material asset(arena, *virtualPath);
    asset.setShaderVariant(materialEntry.shaderVariant);
    const auto materialInterface = Name::FromText(AStringView(materialEntry.materialInterface));
    if(!materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface contains an embedded null"));
        return MakeUnexpected(Failure{});
    }
    asset.setMaterialInterface(*materialInterface);
    asset.setShadingModelId(materialEntry.shadingModelId);
    asset.setSurfaceDispatchId(materialEntry.surfaceDispatchId);
    if(!SetOptionalAvboitPixelShader<&Material::setAvboitAccumulatePixelShader>(materialEntry, materialEntry.avboitAccumulatePixelShaderName, s_MaterialAvboitAccumulatePassLabel, asset))
        return MakeUnexpected(Failure{});
    if(!SetOptionalAvboitPixelShader<&Material::setAvboitOccupancyPixelShader>(materialEntry, materialEntry.avboitOccupancyPixelShaderName, s_MaterialAvboitOccupancyPassLabel, asset))
        return MakeUnexpected(Failure{});
    if(!SetOptionalAvboitPixelShader<&Material::setAvboitExtinctionPixelShader>(materialEntry, materialEntry.avboitExtinctionPixelShaderName, s_MaterialAvboitExtinctionPassLabel, asset))
        return MakeUnexpected(Failure{});
    asset.setTransparent(materialEntry.transparent);
    asset.setTwoSided(materialEntry.twoSided);
    asset.setRefractive(materialEntry.refractive);
    if(!HasValidMaterialAvboitPixelShaderContract(asset.transparent(), asset.avboitAccumulatePixelShader(), asset.avboitOccupancyPixelShader(), asset.avboitExtinctionPixelShader())){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' AVBOIT pixel shaders must be present if and only if it is transparent"), ConvertString(AStringView(materialEntry.virtualPath)));
        return MakeUnexpected(Failure{});
    }
    asset.setTypedLayout(materialEntry.typedLayoutHash, materialEntry.typedLayoutBlocks, materialEntry.typedLayoutFields, materialEntry.typedBlockBytes);
    asset.setResourceReferences(materialEntry.resourceReferences);

    for(const auto& [shaderType, shaderAsset] : materialEntry.stageShaders){
        if(!asset.setShaderForStage(shaderType, shaderAsset)){
            const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: invalid shader stage '{}' for '{}'"), ConvertString(stageName.resolvedText()), ConvertString(AStringView(materialEntry.virtualPath)));
            return MakeUnexpected(Failure{});
        }
    }

    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MaterialAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    static constexpr TStringView s_SerializeFailureContext = NWB_TEXT("MaterialAssetCodec::serialize");
    if(!checkSerializeAssetType(asset, s_SerializeFailureContext))
        return false;

    const Material& material = static_cast<const Material&>(asset);
    if(!material.validatePayload(s_SerializeFailureContext, s_MaterialSerializeContextText))
        return false;
    return MaterialBinaryWriter().serialize(
        AStringView(material.shaderVariant()),
        material.materialInterface(),
        material.typedLayoutHash(),
        material.typedLayoutBlocks(),
        material.typedLayoutFields(),
        material.typedBlockBytes(),
        material.resourceReferences(),
        material.stageShaders(),
        material.stageShaderCount(),
        material.transparent(),
        material.twoSided(),
        material.refractive(),
        material.shadingModelId(),
        material.surfaceDispatchId(),
        material.avboitAccumulatePixelShader(),
        material.avboitOccupancyPixelShader(),
        material.avboitExtinctionPixelShader(),
        outBinary
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
