// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool HasProjectAssetVirtualRoot(const AStringView virtualPath, ScratchArena& scratchArena){
    ACompactString virtualRoot;
    if(!Core::Assets::AssetPathsDetail::ExtractAssetVirtualRoot(virtualPath, virtualRoot, scratchArena))
        return false;

    return virtualRoot.view() == Core::Assets::s_ProjectVirtualRoot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseVariantField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    CookString& outVariant,
    ScratchArena& scratchArena
){
    auto& arena = outVariant.get_allocator().arena();
    outVariant.clear();

    const auto* variantValue = asset.findField(fieldName);
    if(!variantValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' is required")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    CookString rawVariant{arena};
    if(variantValue->isList()){
        const auto& list = variantValue->asList();
        usize rawVariantSize = list.empty() ? 0u : list.size() - 1u;
        for(usize i = 0; i < list.size(); ++i){
            if(!list[i].isString()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' list elements must be strings")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(fieldName)
                );
                return false;
            }
            rawVariantSize += list[i].asString().size();
        }

        rawVariant.reserve(rawVariantSize);
        for(usize i = 0; i < list.size(); ++i){
            if(i > 0)
                rawVariant += ';';
            const Core::Metascript::MStringView variantText = list[i].asString();
            rawVariant.append(variantText.data(), variantText.size());
        }
    }
    else if(variantValue->isString()){
        const Core::Metascript::MStringView variantText = variantValue->asString();
        rawVariant.assign(variantText.data(), variantText.size());
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must be a string or list of strings")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const AStringView rawVariantView = TrimView(rawVariant);
    if(rawVariantView.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    if(rawVariantView == Core::ShaderArchive::s_DefaultVariant){
        outVariant = Core::ShaderArchive::s_DefaultVariant;
        return true;
    }

    using ScratchString = AString<ScratchArena>;
    using ScratchDefineCombo = HashMap<ScratchString, ScratchString, Hasher<ScratchString>, EqualTo<ScratchString>, ScratchArena>;
    ScratchDefineCombo assignments(
        0,
        Hasher<ScratchString>(),
        EqualTo<ScratchString>(),
        scratchArena
    );
    usize assignmentReserve = 1u;
    for(const char ch : rawVariantView){
        if(ch == ';')
            ++assignmentReserve;
    }
    assignments.reserve(assignmentReserve);

    auto failInvalidVariant = [&](){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' has invalid variant signature '{}'")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
            , StringConvert(rawVariantView)
        );
        return false;
    };

    usize begin = 0u;
    while(begin < rawVariantView.size()){
        usize segmentEnd = rawVariantView.find(';', begin);
        if(segmentEnd == AStringView::npos)
            segmentEnd = rawVariantView.size();

        const AStringView segment = TrimView(rawVariantView.substr(begin, segmentEnd - begin));
        if(segment.empty())
            return failInvalidVariant();

        const usize equalPos = segment.find('=');
        if(equalPos == AStringView::npos || equalPos == 0u || equalPos + 1u >= segment.size())
            return failInvalidVariant();

        ScratchString defineName(TrimView(segment.substr(0u, equalPos)), scratchArena);
        ScratchString defineValue(TrimView(segment.substr(equalPos + 1u)), scratchArena);
        if(defineName.empty() || defineValue.empty())
            return failInvalidVariant();
        if(!assignments.emplace(Move(defineName), Move(defineValue)).second)
            return failInvalidVariant();

        begin = segmentEnd + 1u;
    }

    CookString canonicalVariant{arena};
    if(assignments.size() == 1u){
        const auto& [defineName, defineValue] = *assignments.begin();
        canonicalVariant.reserve(defineName.size() + defineValue.size() + 1u);
        canonicalVariant += defineName;
        canonicalVariant += '=';
        canonicalVariant += defineValue;
    }
    else{
        struct AssignmentPtr{
            const ScratchString* key = nullptr;
            const ScratchString* value = nullptr;
        };
        Vector<AssignmentPtr, ScratchArena> sortedAssignments{scratchArena};
        sortedAssignments.reserve(assignments.size());
        for(const auto& [defineName, defineValue] : assignments)
            sortedAssignments.push_back(AssignmentPtr{ &defineName, &defineValue });
        Sort(sortedAssignments.begin(), sortedAssignments.end(), [](const AssignmentPtr& lhs, const AssignmentPtr& rhs){
            return *lhs.key < *rhs.key;
        });

        usize canonicalVariantSize = sortedAssignments.size() - 1u;
        for(const AssignmentPtr& assignment : sortedAssignments)
            canonicalVariantSize += assignment.key->size() + assignment.value->size() + 1u;

        canonicalVariant.reserve(canonicalVariantSize);
        bool first = true;
        for(const AssignmentPtr& assignment : sortedAssignments){
            if(!first)
                canonicalVariant += ';';
            first = false;

            canonicalVariant += *assignment.key;
            canonicalVariant += '=';
            canonicalVariant += *assignment.value;
        }
    }

    outVariant = Move(canonicalVariant);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialStageShaders(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    MaterialCookEntry::StageShaderMap& outStageShaders,
    ScratchArena& scratchArena
){
    outStageShaders.clear();

    const auto* shadersValue = asset.findField(MaterialAssetMetadataSchema::s_ShadersField);
    if(!shadersValue)
        return true;  // optional: when omitted, the cross-asset phase generates the pixel shader from `surface`
                      // and assigns the shared engine mesh shader.
    if(!shadersValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shaders must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    outStageShaders.reserve(shadersValue->asMap().size());

    for(const auto& [stageKey, shaderValue] : shadersValue->asMap()){
        const AStringView stageKeyText(stageKey.data(), stageKey.size());
        if(!shaderValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader '{}' must be a string")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(stageKeyText)
            );
            return false;
        }

        const Core::Metascript::MStringView shaderText = shaderValue.asString();
        const AStringView shaderPath = TrimView(AStringView(shaderText.data(), shaderText.size()));
        if(!HasProjectAssetVirtualRoot(shaderPath, scratchArena)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage '{}' must use the project/ virtual root")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(stageKeyText)
            );
            return false;
        }

        const Core::ShaderType::Enum shaderType =
            Core::ShaderStageNames::ShaderTypeFromArchiveStageName(ToName(stageKeyText));
        const Name shaderName = ToName(shaderPath);
        Core::Assets::AssetRef<Shader> shaderAsset;
        shaderAsset.virtualPath = shaderName;
        if(!Core::ShaderType::IsValid(shaderType) || !shaderAsset.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage entries must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }
        if(shaderType != Core::ShaderType::PixelStage && shaderType != Core::ShaderType::MeshStage){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage '{}' is not supported by the ECS renderer material contract; only 'mesh' and 'ps' are allowed")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(stageKeyText)
            );
            return false;
        }

        if(!outStageShaders.emplace(shaderType, shaderAsset).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': duplicate shader stage '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(stageKeyText)
            );
            return false;
        }
    }

    if(outStageShaders.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shaders must not be empty"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ValidateMaterialOpticalStageContract(
    const Path& nwbFilePath,
    const MaterialCookEntry& entry
){
    if(entry.stageShaders.empty() || !entry.surfaceSource.empty() || (!entry.transparent && !entry.refractive))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': explicit 'shaders' cannot be used with transparent/refractive materials; author a project 'surface' hook so AVBOIT and shadow optical passes use the material contract")
        , PathToString<tchar>(nwbFilePath)
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialParameters(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    MaterialCookEntry::ParameterMap& outParameters
){
    outParameters.clear();

    const auto* parametersValue = asset.findField(MaterialAssetMetadataSchema::s_ParametersField);
    if(!parametersValue)
        return true;
    if(!parametersValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameters must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    outParameters.reserve(parametersValue->asMap().size());

    auto appendParameter = [&](
        const AStringView paramKeyText,
        const Core::Metascript::Value& paramValue
    ) -> bool{
        if(!paramValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}' must be a string")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(paramKeyText)
            );
            return false;
        }

        ACompactString key;
        ACompactString value;
        const AStringView paramValueText(paramValue.asString().data(), paramValue.asString().size());
        if(!key.assign(paramKeyText) || !value.assign(paramValueText)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}' exceeds ACompactString capacity")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(paramKeyText)
            );
            return false;
        }
        if(!key){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter names must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }

        if(!outParameters.emplace(key, value).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': duplicate parameter '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(key.c_str())
            );
            return false;
        }

        return true;
    };

    for(const auto& [paramKey, paramValue] : parametersValue->asMap()){
        const AStringView paramKeyText(paramKey.data(), paramKey.size());
        if(paramValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface parameter '{}' must be declared inside a block map")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(paramKeyText)
            );
            return false;
        }

        if(!paramValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}' must be a block map")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(paramKeyText)
            );
            return false;
        }
        if(paramKeyText.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter block names must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }

        for(const auto& [blockParamKey, blockParamValue] : paramValue.asMap()){
            const AStringView blockParamKeyText(blockParamKey.data(), blockParamKey.size());
            if(blockParamKeyText.empty()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter names in block '{}' must not be empty")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(paramKeyText)
                );
                return false;
            }

            ACompactString flattenedKey;
            if(!flattenedKey.assign(paramKeyText) || !flattenedKey.pushBack('.') || !flattenedKey.append(blockParamKeyText)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}.{}' exceeds ACompactString capacity")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(paramKeyText)
                    , StringConvert(blockParamKeyText)
                );
                return false;
            }

            if(!appendParameter(flattenedKey.view(), blockParamValue))
                return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialInterface(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    MaterialCookString& outMaterialInterface,
    ScratchArena& scratchArena
){
    outMaterialInterface.clear();

    const auto* interfaceValue = asset.findField(MaterialAssetMetadataSchema::s_InterfaceField);
    if(!interfaceValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface is required"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(!interfaceValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must be a string"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    const Core::Metascript::MStringView interfaceText = interfaceValue->asString();
    const AStringView interfacePath = TrimView(AStringView(interfaceText.data(), interfaceText.size()));
    if(interfacePath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must not be empty"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    if(!HasProjectAssetVirtualRoot(interfacePath, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must use the project/ virtual root "
            "(e.g. 'project/shaders/surface.bind')")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    // The interface names a `.bind` carrying the extension explicitly (like `surface`/`bxdf`). Validate it, then
    // strip the extension: the stored interface name must match the discovered .bind's virtual path, which is
    // derived with the extension removed.
    ::Path<ScratchArena> interfacePathPath(scratchArena, interfacePath);
    ScratchString extension = PathToString(scratchArena, interfacePathPath.extension());
    CanonicalizeTextInPlace(extension);
    if(AStringView(extension) != AStringView(".bind")){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must reference a .bind file"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    interfacePathPath.replace_extension();
    ScratchString strippedInterface = PathToString(scratchArena, interfacePathPath);
    for(char& ch : strippedInterface){
        if(ch == '\\')
            ch = '/';
    }

    // Store the readable interface path text; the Name it hashes to is produced on demand (bind lookup / identity
    // compare / cooked Material). Validate that the text forms a valid Name first, failing early with a clear message.
    if(!Name(AStringView(strippedInterface))){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface '{}' is invalid")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(interfacePath)
        );
        return false;
    }
    outMaterialInterface.assign(AStringView(strippedInterface));

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Parses an optional material field whose value is a `project/`-rooted virtual path carrying a
// dedicated extension (e.g. `bxdf` -> ".bxdf", `surface` -> ".surface"), mirroring how `interface` names a
// `.bind`. Parse only validates the format + stores the virtual path verbatim (forward slashes, original case);
// the cross-asset phase (volume_prepare) resolves it to an absolute source against all asset roots (only known
// there). Existence is enforced there / by the dependency-checksum, so parse stays filesystem-light.
static bool ParseMaterialVirtualAssetField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    const AStringView requiredExtension,
    CookString& outVirtualPath,
    ScratchArena& scratchArena
){
    outVirtualPath.clear();

    const auto* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return true;
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must be a string")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const Core::Metascript::MStringView fieldText = fieldValue->asString();
    const AStringView virtualPath = TrimView(AStringView(fieldText.data(), fieldText.size()));
    if(virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    if(!HasProjectAssetVirtualRoot(virtualPath, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must use the project/ virtual root "
            "(e.g. 'project/shaders/name{}')")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
            , StringConvert(requiredExtension)
        );
        return false;
    }

    const ::Path<ScratchArena> virtualPathPath(scratchArena, virtualPath);
    ScratchString extension = PathToString(scratchArena, virtualPathPath.extension());
    CanonicalizeTextInPlace(extension);
    if(AStringView(extension) != requiredExtension){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must reference a {} file")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
            , StringConvert(requiredExtension)
        );
        return false;
    }

    ScratchString normalized(virtualPath, scratchArena);
    for(char& ch : normalized){
        if(ch == '\\')
            ch = '/';
    }
    outVirtualPath.assign(AStringView(normalized));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Optional at parse; required at cook (AssignMaterialShadingModelIds rejects any material lacking a bxdf -- the
// engine ships no default).
static bool ParseMaterialBxdf(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    CookString& outBxdfSource,
    ScratchArena& scratchArena
){
    return ParseMaterialVirtualAssetField(
        nwbFilePath, asset, MaterialAssetMetadataSchema::s_BxdfField, ".bxdf", outBxdfSource, scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Optional. When present (and `shaders` is omitted), the cross-asset phase generates this material's G-buffer
// pixel shader from this surface hook fragment.
static bool ParseMaterialSurface(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    CookString& outSurfaceSource,
    ScratchArena& scratchArena
){
    return ParseMaterialVirtualAssetField(
        nwbFilePath, asset, MaterialAssetMetadataSchema::s_SurfaceField, ".surface", outSurfaceSource, scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialBoolProperty(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    bool& outValue
){
    outValue = false;

    const auto* propertyValue = asset.findField(fieldName);
    if(!propertyValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': '{}' is required and must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    if(!propertyValue->isInteger()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': '{}' must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const i64 propertyInt = propertyValue->asInteger();
    if(propertyInt != 0 && propertyInt != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': '{}' must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    outValue = propertyInt != 0;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialRenderProperties(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    MaterialCookEntry& outEntry
){
    if(!ParseMaterialBoolProperty(nwbFilePath, asset, MaterialAssetMetadataSchema::s_TransparentField, outEntry.transparent))
        return false;
    if(!ParseMaterialBoolProperty(nwbFilePath, asset, MaterialAssetMetadataSchema::s_TwoSidedField, outEntry.twoSided))
        return false;
    if(!ParseMaterialBoolProperty(nwbFilePath, asset, MaterialAssetMetadataSchema::s_RefractiveField, outEntry.refractive))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseMaterialMeta(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MaterialCookEntry& outEntry,
    ScratchArena& scratchArena
){
    outEntry.reset();

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': asset is not a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    // Derive the material's virtual path as readable text (stored verbatim; the cook builds generated-shader file
    // paths / identities from it, and the framework dedups by the Name it hashes to via ToCookEntryName). Validate
    // it forms a valid Name before storing.
    ScratchString derivedVirtualPath(scratchArena);
    if(!Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, derivedVirtualPath))
        return false;
    if(!Name(AStringView(derivedVirtualPath))){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': failed to derive a valid virtual path"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    outEntry.virtualPath.assign(AStringView(derivedVirtualPath));
    if(!Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        "Material meta",
        MaterialAssetMetadataSchema::IsAllowedAssetField
    ))
        return false;

    if(!ParseVariantField(
        nwbFilePath,
        asset,
        MaterialAssetMetadataSchema::s_ShaderVariantField,
        outEntry.shaderVariant,
        scratchArena
    ))
        return false;
    if(!ParseMaterialInterface(nwbFilePath, asset, outEntry.materialInterface, scratchArena))
        return false;
    if(!ParseMaterialBxdf(nwbFilePath, asset, outEntry.bxdfSource, scratchArena))
        return false;
    if(!ParseMaterialSurface(nwbFilePath, asset, outEntry.surfaceSource, scratchArena))
        return false;
    if(!ParseMaterialRenderProperties(nwbFilePath, asset, outEntry))
        return false;
    if(!ParseMaterialStageShaders(nwbFilePath, asset, outEntry.stageShaders, scratchArena))
        return false;
    if(!ValidateMaterialOpticalStageContract(nwbFilePath, outEntry))
        return false;
    if(!ParseMaterialParameters(nwbFilePath, asset, outEntry.parameters))
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

