// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include "source_dependencies.h"
#include "shader_types.h"

#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
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

static constexpr Array<NamedEnumCase<ShaderOptimizationLevel::Enum>, 4u> s_ShaderOptimizationLevelCases = {{
    { s_ShaderNoneOptText, ShaderOptimizationLevel::None },
    { s_ShaderDefaultVariantText, ShaderOptimizationLevel::Default },
    { s_ShaderHighOptText, ShaderOptimizationLevel::High },
    { s_ShaderMaximalOptText, ShaderOptimizationLevel::Maximal },
}};


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

    const auto& definesMap = definesVal->asMap();
    defines.reserve(definesMap.size());
    for(const auto& [key, val] : definesMap){
        const AStringView authoredName(key.data(), key.size());
        if(IsReservedShaderEngineDefineName(authoredName)){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' is an engine transport feature selected in shader source"), PathToString<tchar>(nwbFilePath), ConvertString(authoredName));
            return MakeUnexpected(Failure{});
        }
        CookString defineName(key.data(), key.size(), arena);
        const AStringView defineNameView(defineName);
        if(!IsAsciiIdentifier(defineNameView)){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' must be an ASCII shader identifier"), PathToString<tchar>(nwbFilePath), ConvertString(defineName));
            return MakeUnexpected(Failure{});
        }

        ShaderCook::CookVector<CookString> defineValues(arena);
        if(!val.copyStringList(defineValues)){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' values must be a list of strings"), PathToString<tchar>(nwbFilePath), ConvertString(defineName));
            return MakeUnexpected(Failure{});
        }
        if(defineValues.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' must provide at least one value"), PathToString<tchar>(nwbFilePath), ConvertString(defineName));
            return MakeUnexpected(Failure{});
        }
        for(const CookString& defineValue : defineValues){
            if(!IsValidShaderDefineValueText(AStringView(defineValue))){
                NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' values must be nonempty variant text without semicolons, line breaks, nulls, or surrounding whitespace"), PathToString<tchar>(nwbFilePath), ConvertString(defineName));
                return MakeUnexpected(Failure{});
            }
        }

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

    if(variantSignature.empty())
        return true;

    if(variantSignature == s_ShaderDefaultVariantText){
        if(defineValues.empty())
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant 'default' is only valid when no defines are specified"), ConvertString(contextLabel));
        return false;
    }

    if(defineValues.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' requires defines to be specified"), ConvertString(contextLabel), ConvertString(variantSignature));
        return false;
    }

    using ScratchStringSet = __hidden_shader_cook::ScratchHashSet<AStringView>;
    ScratchStringSet seenDefines{0, Hasher<AStringView>(), EqualTo<AStringView>(), scratchArena};
    seenDefines.reserve(defineValues.size());
    const b8 parsedAssignments = ParseVariantSignatureAssignments(
        variantSignature,
        [&](const AStringView defineName, const AStringView defineValue) -> b8{
            const auto defineIt = defineValues.find(defineName);
            if(defineIt == defineValues.end()){
                NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' references unknown define '{}'"), ConvertString(contextLabel), ConvertString(variantSignature), ConvertString(defineName));
                return false;
            }

            b8 valueFound = false;
            for(const CookString& allowedValue : defineIt.value().values){
                if(AStringView(allowedValue) == defineValue){
                    valueFound = true;
                    break;
                }
            }
            if(!valueFound){
                NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' has unsupported value '{}' for define '{}'"), ConvertString(contextLabel), ConvertString(variantSignature), ConvertString(defineValue), ConvertString(defineName));
                return false;
            }

            if(!seenDefines.insert(defineName).second){
                NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' assigns define '{}' more than once"), ConvertString(contextLabel), ConvertString(variantSignature), ConvertString(defineName));
                return false;
            }

            return true;
        },
        [&](const VariantSignatureSegmentError::Enum error, const AStringView segment){
            if(error == VariantSignatureSegmentError::EmptySegment)
                NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' has invalid empty segment"), ConvertString(contextLabel), ConvertString(variantSignature));
            else
                NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' has invalid assignment '{}'"), ConvertString(contextLabel), ConvertString(variantSignature), ConvertString(segment));
        }
    );
    if(!parsedAssignments)
        return false;

    if(seenDefines.size() != defineValues.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' must assign all defines"), ConvertString(contextLabel), ConvertString(variantSignature));
        return false;
    }

    return true;
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
        const auto optimizationLevel = ParseNamedEnumText<ShaderOptimizationLevel::Enum>(optimizationLevelText->text, __hidden_shader_cook::s_ShaderOptimizationLevelCases);
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
        for(const CookString& includeRoot : entry.includeRoots){
            if(!includeRoot.empty())
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': include_roots entries must not be empty"), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
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

void ShaderCook::mergeInheritedDefines(const Span<const Path> dependencies, const CookMap<CookString, IncludeEntry>& includeMetadata, Alloc::ScratchArena& scratchArena, ShaderEntry& inOutEntry){
    __hidden_shader_cook::ScratchVector<NotNull<const IncludeEntry*>> inheritedEntries{scratchArena};
    inheritedEntries.reserve(dependencies.size());

    usize defineReserveCount = inOutEntry.defineValues.size();
    b8 canReserveDefineValues = true;
    for(const Path& dep : dependencies){
        __hidden_shader_cook::ScratchString depKey = PathToString(scratchArena, dep);
#if defined(NWB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(depKey);
#endif
        const auto found = includeMetadata.find(AStringView(depKey));
        if(found == includeMetadata.end())
            continue;

        const IncludeEntry& includeEntry = found.value();
        inheritedEntries.push_back(MakeNotNull(&includeEntry));

        if(includeEntry.defineValues.size() > Limit<usize>::s_Max - defineReserveCount)
            canReserveDefineValues = false;
        else
            defineReserveCount += includeEntry.defineValues.size();
    }

    if(canReserveDefineValues && defineReserveCount > inOutEntry.defineValues.size())
        inOutEntry.defineValues.reserve(defineReserveCount);

    for(const NotNull<const IncludeEntry*> includeEntry : inheritedEntries){
        for(const auto& [defineName, defineEntry] : includeEntry->defineValues){
            auto [defineIt, inserted] = inOutEntry.defineValues.try_emplace(defineName, m_memoryArena);
            if(inserted)
                defineIt.value().values.assign(defineEntry.values.begin(), defineEntry.values.end());
        }
    }
}

b8 ShaderCook::expandDefineCombinations(const CookMap<CookString, DefineEntry>& defineValues, Alloc::ScratchArena& scratchArena, CookVector<DefineCombo>& outCombinations){
    outCombinations.clear();
    DefineCombo initialCombo{m_memoryArena};
    initialCombo.reserve(defineValues.size());
    outCombinations.push_back(Move(initialCombo));

    auto cloneComboWithEntry = [&](const DefineCombo& source, const AStringView defineName, const AStringView defineValue) -> Expected<DefineCombo>{
        DefineCombo copy{m_memoryArena};
        copy.reserve(defineValues.size());
        for(const auto& [sourceName, sourceValue] : source){
            if(!copy.try_emplace(sourceName, sourceValue).second){
                NWB_ASSERT(false);
                return MakeUnexpected(Failure{});
            }
        }
        if(!copy.try_emplace(defineName, defineValue).second){
            NWB_ASSERT(false);
            return MakeUnexpected(Failure{});
        }
        return copy;
    };

    for(const auto& entry : SortedDefineEntries(defineValues, scratchArena)){
        const AStringView defineName(*entry.key);
        const CookVector<CookString>& values = entry.value->values;
        if(values.empty()){
            outCombinations.clear();
            return true;
        }

        if(values.size() == 1u){
            const AStringView value(values.front());
            for(DefineCombo& combo : outCombinations){
                combo.reserve(defineValues.size());
                if(!combo.try_emplace(defineName, value).second){
                    NWB_ASSERT(false);
                    outCombinations.clear();
                    return false;
                }
            }
            continue;
        }

        if(outCombinations.size() > Limit<usize>::s_Max / values.size()){
            outCombinations.clear();
            return false;
        }

        CookVector<DefineCombo> expanded{m_memoryArena};
        expanded.reserve(outCombinations.size() * values.size());

        const usize copiedValueCount = values.size() - 1u;
        for(DefineCombo& combo : outCombinations){
            for(usize valueIndex = 0u; valueIndex < copiedValueCount; ++valueIndex){
                auto copy = cloneComboWithEntry(combo, defineName, values[valueIndex]);
                if(!copy){
                    outCombinations.clear();
                    return false;
                }
                expanded.push_back(Move(*copy));
            }

            combo.reserve(defineValues.size());
            if(!combo.try_emplace(defineName, AStringView(values.back())).second){
                NWB_ASSERT(false);
                outCombinations.clear();
                return false;
            }
            expanded.push_back(Move(combo));
        }

        outCombinations = Move(expanded);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShaderAssetSerialization::Serialize(const IShader& shader, Core::Assets::AssetBytes& outBinary){
    const Core::Assets::AssetBytes& bytecode = shader.bytecode();
    switch(ShaderBinaryPayload::EncodeAssetPayload(AStringView(shader.entryPoint()), bytecode, outBinary)){
    case ShaderBinaryPayload::AssetPayloadEncodeFailure::None:
        break;
    case ShaderBinaryPayload::AssetPayloadEncodeFailure::InvalidEntryPoint:
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCodec::serialize failed: shader entry point is empty or too long"));
        return false;
    case ShaderBinaryPayload::AssetPayloadEncodeFailure::InvalidBytecode:
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCodec::serialize failed: invalid SPIR-V bytecode"));
        return false;
    case ShaderBinaryPayload::AssetPayloadEncodeFailure::OutputSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCodec::serialize failed: shader payload size overflow"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

