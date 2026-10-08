// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool HasProjectAssetVirtualRoot(const AStringView virtualPath, ScratchArena& scratchArena){
    const auto virtualRoot = Core::Assets::AssetPathsDetail::ExtractAssetVirtualRoot(virtualPath, scratchArena);
    if(!virtualRoot)
        return false;

    return virtualRoot->view() == Core::Assets::s_ProjectVirtualRoot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<CookString> ParseVariantField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    CookArena& arena,
    ScratchArena& scratchArena
){
    const auto* variantValue = asset.findField(fieldName);
    if(!variantValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' is required")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }

    CookString rawVariant{arena};
    AStringView rawVariantView;
    if(variantValue->isList()){
        const auto& list = variantValue->asList();
        usize rawVariantSize = list.empty() ? 0u : list.size() - 1u;
        for(usize i = 0u; i < list.size(); ++i){
            if(!list[i].isString()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' list elements must be strings")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(fieldName)
                );
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
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must be a string or list of strings")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }

    rawVariantView = TrimView(rawVariantView);
    if(rawVariantView.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }
    if(rawVariantView == Core::ShaderArchive::s_DefaultVariant){
        return CookString(Core::ShaderArchive::s_DefaultVariant, arena);
    }

    using ScratchDefineCombo = HashMap<AStringView, AStringView, ScratchArena, Hasher<AStringView>, EqualTo<AStringView>>;
    ScratchDefineCombo assignments(
        0,
        Hasher<AStringView>(),
        EqualTo<AStringView>(),
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
        return MakeUnexpected(Failure{});
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

        const AStringView defineName = TrimView(segment.substr(0u, equalPos));
        const AStringView defineValue = TrimView(segment.substr(equalPos + 1u));
        if(defineName.empty() || defineValue.empty())
            return failInvalidVariant();
        if(!assignments.emplace(defineName, defineValue).second)
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
            const AStringView* key = nullptr;
            const AStringView* value = nullptr;
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

    return canonicalVariant;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MaterialCookEntry::StageShaderMap> ParseMaterialStageShaders(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    CookArena& arena,
    ScratchArena& scratchArena
){
    MaterialCookEntry::StageShaderMap stageShaders(0u, Hasher<Core::ShaderType::Enum>(), EqualTo<Core::ShaderType::Enum>(), arena);

    const auto* shadersValue = asset.findField(MaterialAssetMetadataSchema::s_ShadersField);
    // Omitted shaders are generated from the surface hook during cross-asset preparation.
    if(!shadersValue)
        return stageShaders;
    if(!shadersValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shaders must be a map"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    stageShaders.reserve(shadersValue->asMap().size());

    for(const auto& [stageKey, shaderValue] : shadersValue->asMap()){
        const AStringView stageKeyText(stageKey.data(), stageKey.size());
        if(!shaderValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader '{}' must be a string")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(stageKeyText)
            );
            return MakeUnexpected(Failure{});
        }

        const Core::Metascript::MStringView shaderText = shaderValue.asString();
        const AStringView shaderPath = TrimView(AStringView(shaderText.data(), shaderText.size()));
        if(!HasProjectAssetVirtualRoot(shaderPath, scratchArena)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage '{}' must use the project/ virtual root")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(stageKeyText)
            );
            return MakeUnexpected(Failure{});
        }

        const Core::ShaderType::Enum shaderType =
            Core::ShaderStageNames::ShaderTypeFromArchiveStageName(ToName(stageKeyText));
        const Name shaderName = ToName(shaderPath);
        Core::Assets::AssetRef<IShader> shaderAsset;
        shaderAsset.virtualPath = shaderName;
        if(!Core::ShaderType::IsValid(shaderType) || !shaderAsset.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage entries must not be empty"), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
        if(shaderType != Core::ShaderType::PixelStage && shaderType != Core::ShaderType::MeshStage){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage '{}' is not supported by the ECS renderer material contract; only 'mesh' and 'ps' are allowed")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(stageKeyText)
            );
            return MakeUnexpected(Failure{});
        }

        if(!stageShaders.emplace(shaderType, shaderAsset).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': duplicate shader stage '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(stageKeyText)
            );
            return MakeUnexpected(Failure{});
        }
    }

    if(stageShaders.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shaders must not be empty"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    return stageShaders;
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


static Expected<MaterialCookEntry::ParameterMap> ParseMaterialParameters(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    CookArena& arena
){
    MaterialCookEntry::ParameterMap parameters(0u, Hasher<ACompactString>(), EqualTo<ACompactString>(), arena);

    const auto* parametersValue = asset.findField(MaterialAssetMetadataSchema::s_ParametersField);
    if(!parametersValue)
        return parameters;
    if(!parametersValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameters must be a map"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    parameters.reserve(parametersValue->asMap().size());

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

        if(!parameters.emplace(key, value).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': duplicate parameter '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(key.view())
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
            return MakeUnexpected(Failure{});
        }

        if(!paramValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}' must be a block map")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(paramKeyText)
            );
            return MakeUnexpected(Failure{});
        }
        if(paramKeyText.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter block names must not be empty"), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }

        for(const auto& [blockParamKey, blockParamValue] : paramValue.asMap()){
            const AStringView blockParamKeyText(blockParamKey.data(), blockParamKey.size());
            if(blockParamKeyText.empty()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter names in block '{}' must not be empty")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(paramKeyText)
                );
                return MakeUnexpected(Failure{});
            }

            ACompactString flattenedKey;
            if(!flattenedKey.assign(paramKeyText) || !flattenedKey.pushBack('.') || !flattenedKey.append(blockParamKeyText)){
                NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter '{}.{}' exceeds ACompactString capacity")
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(paramKeyText)
                    , StringConvert(blockParamKeyText)
                );
                return MakeUnexpected(Failure{});
            }

            if(!appendParameter(flattenedKey.view(), blockParamValue))
                return MakeUnexpected(Failure{});
        }
    }

    return parameters;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<CookString> ParseMaterialInterface(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    CookArena& arena,
    ScratchArena& scratchArena
){
    const auto* interfaceValue = asset.findField(MaterialAssetMetadataSchema::s_InterfaceField);
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
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must use the project/ virtual root "
            "(e.g. 'project/shaders/surface.bind')")
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }

    // The interface names a `.bind`; strip the extension to match the discovered virtual path.
    ::Path<ScratchArena> interfacePathPath(scratchArena, interfacePath);
    ScratchString extension = PathToString(scratchArena, interfacePathPath.extension());
    CanonicalizeTextInPlace(extension);
    if(AStringView(extension) != AStringView(".bind")){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface must reference a .bind file"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    interfacePathPath.replaceExtension();
    ScratchString strippedInterface = PathToString(scratchArena, interfacePathPath);
    for(char& ch : strippedInterface){
        if(ch == '\\')
            ch = '/';
    }

    // Store path text; the Name hash is produced on demand.
    if(!Name(AStringView(strippedInterface))){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': interface '{}' is invalid")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(interfacePath)
        );
        return MakeUnexpected(Failure{});
    }
    return CookString(AStringView(strippedInterface), arena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Parses a `project/`-rooted virtual path field; the cross-asset phase resolves it.
static Expected<CookString> ParseMaterialVirtualAssetField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    const AStringView requiredExtension,
    CookArena& arena,
    ScratchArena& scratchArena
){
    const auto* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return CookString(arena);
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must be a string")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }

    const Core::Metascript::MStringView fieldText = fieldValue->asString();
    const AStringView virtualPath = TrimView(AStringView(fieldText.data(), fieldText.size()));
    if(virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }

    if(!HasProjectAssetVirtualRoot(virtualPath, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': field '{}' must use the project/ virtual root "
            "(e.g. 'project/shaders/name{}')")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
            , StringConvert(requiredExtension)
        );
        return MakeUnexpected(Failure{});
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
        return MakeUnexpected(Failure{});
    }

    ScratchString normalized(virtualPath, scratchArena);
    for(char& ch : normalized){
        if(ch == '\\')
            ch = '/';
    }
    return CookString(AStringView(normalized), arena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<bool> ParseMaterialBoolProperty(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName
){
    const auto* propertyValue = asset.findField(fieldName);
    if(!propertyValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': '{}' is required and must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }
    if(!propertyValue->isInteger()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': '{}' must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }

    const i64 propertyInt = propertyValue->asInteger();
    if(propertyInt != 0 && propertyInt != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': '{}' must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }

    return propertyInt != 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookEntry> ParseMaterialMeta(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    ScratchArena& scratchArena
){
    MaterialCookEntry entry(arena);

    const Core::Metascript::Value& asset = doc.asset();
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, "Material meta"))
        return MakeUnexpected(Failure{});

    // Keep the material path as readable text for generated shader paths; validate its Name identity before storage.
    auto derivedVirtualPath = Core::Assets::BuildDerivedAssetVirtualPath(arena, assetRoot, virtualRoot, nwbFilePath);
    if(!derivedVirtualPath)
        return MakeUnexpected(Failure{});
    if(!Name(AStringView(*derivedVirtualPath))){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': failed to derive a valid virtual path"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    entry.virtualPath = Move(*derivedVirtualPath);
    if(!Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        "Material meta",
        MaterialAssetMetadataSchema::IsAllowedAssetField
    ))
        return MakeUnexpected(Failure{});

    auto shaderVariant = ParseVariantField(nwbFilePath, asset, MaterialAssetMetadataSchema::s_ShaderVariantField, arena, scratchArena);
    if(!shaderVariant)
        return MakeUnexpected(Failure{});
    entry.shaderVariant = Move(*shaderVariant);
    auto materialInterface = ParseMaterialInterface(nwbFilePath, asset, arena, scratchArena);
    if(!materialInterface)
        return MakeUnexpected(Failure{});
    entry.materialInterface = Move(*materialInterface);
    auto bxdfSource = ParseMaterialVirtualAssetField(nwbFilePath, asset, MaterialAssetMetadataSchema::s_BxdfField, ".bxdf", arena, scratchArena);
    if(!bxdfSource)
        return MakeUnexpected(Failure{});
    entry.bxdfSource = Move(*bxdfSource);
    auto surfaceSource = ParseMaterialVirtualAssetField(nwbFilePath, asset, MaterialAssetMetadataSchema::s_SurfaceField, ".surface", arena, scratchArena);
    if(!surfaceSource)
        return MakeUnexpected(Failure{});
    entry.surfaceSource = Move(*surfaceSource);
    const auto transparent = ParseMaterialBoolProperty(nwbFilePath, asset, MaterialAssetMetadataSchema::s_TransparentField);
    if(!transparent)
        return MakeUnexpected(Failure{});
    entry.transparent = *transparent;
    const auto twoSided = ParseMaterialBoolProperty(nwbFilePath, asset, MaterialAssetMetadataSchema::s_TwoSidedField);
    if(!twoSided)
        return MakeUnexpected(Failure{});
    entry.twoSided = *twoSided;
    const auto refractive = ParseMaterialBoolProperty(nwbFilePath, asset, MaterialAssetMetadataSchema::s_RefractiveField);
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

