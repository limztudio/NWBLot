// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include "source_dependencies.h"
#include "shader_types.h"

#include <core/assets/cook_metadata.h>
#include <core/graphics/shader_stage_names.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;
namespace Alloc = Core::Alloc;
namespace Metascript = Core::Metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace ShaderBinaryPayload;
using CookString = ShaderCook::CookString;
using ScratchString = Assets::ScratchString;
template<typename T>
using ScratchVector = Assets::ScratchVector<T>;
template<typename T>
using ScratchHashSet = HashSet<T, Alloc::ScratchArena, Hasher<T>, EqualTo<T>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<ShaderCook::CookMap<CookString, ShaderCook::DefineEntry>> ParseDefines(const Path& nwbFilePath, const Metascript::Value& asset, ShaderCook::CookArena& arena){
    ShaderCook::CookMap<CookString, ShaderCook::DefineEntry> defines{arena};

    const auto* definesVal = asset.findField(s_ShaderDefinesField);
    if(!definesVal)
        return defines;

    if(!definesVal->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': defines must be a map"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    const CookString nwbFilePathText = PathToString(arena, nwbFilePath);
    const auto& definesMap = definesVal->asMap();
    defines.reserve(definesMap.size());
    for(const auto& [key, val] : definesMap){
        CookString defineName(key.data(), key.size(), arena);
        if(IsReservedShaderEngineDefineName(AStringView(defineName))){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' is an engine transport feature selected in shader source"), PathToString<tchar>(nwbFilePath), ConvertString(defineName));
            return MakeUnexpected(Failure{});
        }
        if(!IsAsciiIdentifier(AStringView(defineName))){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' must be an ASCII shader identifier"), PathToString<tchar>(nwbFilePath), ConvertString(defineName));
            return MakeUnexpected(Failure{});
        }

        ShaderCook::CookVector<CookString> defineValues(arena);
        if(!val.copyStringList(defineValues)){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' values must be a list of strings"), PathToString<tchar>(nwbFilePath), ConvertString(defineName));
            return MakeUnexpected(Failure{});
        }
        if(!ValidateShaderDefineEntrySpec(AStringView(nwbFilePathText), AStringView(defineName), defineValues))
            return MakeUnexpected(Failure{});

        ShaderCook::DefineEntry defineEntry(Move(defineValues));
        defines.insert_or_assign(Move(defineName), Move(defineEntry));
    }
    return defines;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShaderCook::ValidateVariantSignature(const AStringView contextLabel, const AStringView variantSignature, const CookMap<CookString, DefineEntry>& defineValues, Alloc::ScratchArena& scratchArena){
    using namespace ShaderBinaryPayload;

    b8 isComplete = false;
    if(!ValidateVariantSignaturePreamble(contextLabel, variantSignature, defineValues.empty(), isComplete))
        return false;
    if(isComplete)
        return true;

    using ScratchStringSet = __hidden_shader_cook::ScratchHashSet<AStringView>;
    ScratchStringSet seenDefines{0, Hasher<AStringView>(), EqualTo<AStringView>(), scratchArena};
    seenDefines.reserve(defineValues.size());
    const b8 parsedAssignments = ParseVariantSignatureAssignments(
        variantSignature,
        [&](const AStringView defineName, const AStringView defineValue) -> b8{
            const auto defineIt = defineValues.find(defineName);
            const b8 defineKnown = defineIt != defineValues.end();
            b8 valueFound = false;
            if(defineKnown){
                for(const CookString& allowedValue : defineIt.value().values){
                    if(AStringView(allowedValue) == defineValue){
                        valueFound = true;
                        break;
                    }
                }
            }
            const b8 insertedUnique = defineKnown && valueFound && seenDefines.insert(defineName).second;
            return ValidateVariantSignatureAssignmentStatus(contextLabel, variantSignature, defineName, defineValue, defineKnown, valueFound, insertedUnique);
        },
        [&](const VariantSignatureSegmentError::Enum error, const AStringView segment){
            ReportVariantSignatureSegmentError(contextLabel, variantSignature, error, segment);
        }
    );
    if(!parsedAssignments)
        return false;

    return ValidateVariantSignatureSeenCount(contextLabel, variantSignature, seenDefines.size(), defineValues.size());
}

Expected<ShaderCook::ShaderEntry> ShaderCook::parseShaderMeta(const Path& nwbFilePath, const Metascript::Document& doc, Alloc::ScratchArena& scratchArena){
    using namespace ShaderBinaryPayload;

    ShaderEntry entry(m_memoryArena);

    const auto* assetDeclaration = doc.findDeclaration(Assets::s_MetadataAssetVariable);
    if(!assetDeclaration){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': missing asset declaration '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(Assets::s_MetadataAssetVariable));
        return MakeUnexpected(Failure{});
    }
    const AStringView assetTypeText(assetDeclaration->type.data(), assetDeclaration->type.size());
    const Core::ShaderType::Enum shaderType = ShaderAssetTypes::ShaderTypeFromAssetTypeText(assetTypeText);
    if(shaderType == Core::ShaderType::Invalid){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': unsupported shader asset type '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(assetTypeText));
        return MakeUnexpected(Failure{});
    }

    const auto assetValue = Assets::FindMetadataAssetMapValue<Metascript::Document, Metascript::Value>(nwbFilePath, doc, s_ShaderDiagnosticPrefix);
    if(!assetValue)
        return MakeUnexpected(assetValue.error());
    const Metascript::Value& asset = **assetValue;

    auto pairedSource = Assets::ResolvePairedSourcePathFromMetadata(nwbFilePath, m_memoryArena, scratchArena);
    if(!pairedSource)
        return MakeUnexpected(Failure{});
    entry.source = Move(*pairedSource);
    if(!Assets::CheckPairedSourceExtension(nwbFilePath, entry.source, s_SlangSourceExtension, s_ShaderDiagnosticPrefix, scratchArena))
        return MakeUnexpected(Failure{});

    entry.stage = Core::ShaderStageNames::ArchiveStageTextFromShaderType(shaderType);
    if(entry.stage.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': failed to derive shader stage"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    entry.archiveStage = entry.stage;
    if(!Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_ShaderMetaDiagnosticPrefix, [isMesh = shaderType == Core::ShaderType::MeshStage](const AStringView field)noexcept{
        return IsAllowedShaderMetadataField(field, isMesh);
    }))
        return MakeUnexpected(Failure{});
    if(!Assets::ReadOptionalMetadataBoolField(nwbFilePath, asset, "Meta", s_ShaderRayQueryField, entry.rayQuery))
        return MakeUnexpected(Failure{});
    const auto optimizationLevelText = Assets::ReadMetadataStringField(nwbFilePath, asset, s_ShaderMetaDiagnosticPrefix, s_ShaderOptimizationLevelField, false);
    if(!optimizationLevelText)
        return MakeUnexpected(Failure{});
    if(optimizationLevelText->present){
        const auto optimizationLevel = TryParseShaderOptimizationLevelText(optimizationLevelText->text);
        if(!optimizationLevel){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': unsupported optimization_level '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(optimizationLevelText->text));
            return MakeUnexpected(Failure{});
        }
        entry.optimizationLevel = *optimizationLevel;
    }
    AStringView entryPointText;
    auto entryPointTextResult = Assets::ReadMetadataStringField(nwbFilePath, asset, s_ShaderMetaDiagnosticPrefix, s_ShaderEntryPointField, false);
    if(!entryPointTextResult)
        return MakeUnexpected(Failure{});
    entryPointText = entryPointTextResult->text;
    entry.entryPoint.assign(entryPointText.data(), entryPointText.size());
    if(entry.entryPoint.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': entry_point must not be empty"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    if(!Assets::ReadOptionalMetadataBoolField(nwbFilePath, asset, "Meta", s_ShaderEmitMeshComputeShadowField, entry.emitMeshComputeShadow))
        return MakeUnexpected(Failure{});

    if(const auto* includeRootsVal = asset.findField(s_ShaderIncludeRootsField)){
        if(!includeRootsVal->copyStringList(entry.includeRoots)){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': include_roots must be a list of strings"), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
        const Assets::ScratchString nwbFilePathText = PathToString(scratchArena, nwbFilePath);
        if(!ValidateShaderIncludeRoots(AStringView(nwbFilePathText), entry.includeRoots))
            return MakeUnexpected(Failure{});
    }

    auto defines = __hidden_shader_cook::ParseDefines(nwbFilePath, asset, m_memoryArena);
    if(!defines)
        return MakeUnexpected(Failure{});
    entry.defineValues = Move(*defines);

    return entry;
}

Expected<ShaderCook::IncludeEntry> ShaderCook::parseIncludeMeta(const Path& nwbFilePath, const Metascript::Document& doc, Alloc::ScratchArena& scratchArena){
    using namespace ShaderBinaryPayload;

    IncludeEntry entry(m_memoryArena);

    const auto* assetDeclaration = doc.findDeclaration(Assets::s_MetadataAssetVariable);
    if(!assetDeclaration){
        NWB_LOGGER_ERROR(NWB_TEXT("Include meta '{}': missing asset declaration '{}'"), PathToString<tchar>(nwbFilePath), ConvertString(Assets::s_MetadataAssetVariable));
        return MakeUnexpected(Failure{});
    }
    const ACompactString assetType(AStringView(assetDeclaration->type.data(), assetDeclaration->type.size()));
    if(assetType.view() != s_ShaderAssetTypeInclude)
        return entry;

    auto pairedSource = Assets::ResolvePairedSourcePathFromMetadata(nwbFilePath, m_memoryArena, scratchArena);
    if(!pairedSource)
        return MakeUnexpected(Failure{});
    entry.source = Move(*pairedSource);
    if(!Assets::CheckPairedSourceExtension(nwbFilePath, entry.source, s_SlangIncludeExtension, s_IncludeDiagnosticPrefix, scratchArena))
        return MakeUnexpected(Failure{});

    const auto assetValue = Assets::FindMetadataAssetMapValue<Metascript::Document, Metascript::Value>(nwbFilePath, doc, s_IncludeDiagnosticPrefix);
    if(!assetValue)
        return MakeUnexpected(assetValue.error());
    const Metascript::Value& asset = **assetValue;

    if(!Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_IncludeMetaDiagnosticPrefix, &IsAllowedIncludeMetadataField))
        return MakeUnexpected(Failure{});
    auto defines = __hidden_shader_cook::ParseDefines(nwbFilePath, asset, m_memoryArena);
    if(!defines)
        return MakeUnexpected(Failure{});
    entry.defineValues = Move(*defines);

    return entry;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShaderAssetSerialization::Serialize(const IShader& shader, Core::Assets::AssetBytes& outBinary){
    return ShaderBinaryPayload::SerializeShaderAssetPayload(AStringView(shader.entryPoint()), shader.bytecode(), outBinary);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

