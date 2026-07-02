// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "metadata.h"
#include "binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/paths.h>
#include <core/filesystem/module.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_stage_names.h>
#include <core/metascript/parser.h>
#include <global/hash_utils.h>
#include <global/text_utils.h>
#include <global/math/convert.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookArena = MaterialCookArena;
using CookString = MaterialCookString;
using ScratchArena = Core::Alloc::ScratchArena;
using ScratchString = AString<ScratchArena>;
template<typename T>
using CookVector = MaterialCookVector<T>;
template<typename T>
using ScratchHashSet = HashSet<T, Hasher<T>, EqualTo<T>, ScratchArena>;
template<typename T>
using CookHashSet = MaterialCookHashSet<T>;

// Cook-private NAME PREFIXES for the generated per-material AVBOIT accumulate/occupancy/extinction pixel shaders
// (kept here, not in the graphics avboit/names.h, so the material cook does not depend on the graphics-asset header).
// The cook builds "<prefix><material virtual path>" + stores the resolved Name on the cooked material; the renderer
// binds via that stored Name (materialInfo.avboit{Accumulate,Occupancy,Extinction}PixelShader), never re-deriving here.
static constexpr AStringView s_AvboitAccumulatePixelShaderGeneratedPrefix("generated/avboit_accumulate_ps/");
static constexpr AStringView s_AvboitOccupancyPixelShaderGeneratedPrefix("generated/avboit_occupancy_ps/");
static constexpr AStringView s_AvboitExtinctionPixelShaderGeneratedPrefix("generated/avboit_extinction_ps/");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Path BuildMaterialBindIncludeRoot(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    ScratchArena& scratchArena
){
    ScratchString configurationName(configurationSafeName, scratchArena);
    ScratchString includeDirectoryName(MaterialBindNames::GeneratedIncludeCacheDirectoryText(), scratchArena);
    return cacheDirectory / configurationName.c_str() / includeDirectoryName.c_str();
}

static bool BuildMaterialBindIncludeVirtualPathImpl(
    CookArena& arena,
    const MaterialBindEntry& entry,
    CookString& outIncludePath
){
    outIncludePath.clear();
    if(entry.virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation failed: virtual path is empty for '{}'")
            , StringConvert(entry.source)
        );
        return false;
    }

    CookString includePath(entry.virtualPath, arena);
    includePath += MaterialBindNames::SourceExtensionText();
    outIncludePath = Move(includePath);
    return true;
}

static bool HasProjectAssetVirtualRoot(const AStringView virtualPath, ScratchArena& scratchArena){
    ACompactString virtualRoot;
    if(!Core::Assets::AssetPathsDetail::ExtractAssetVirtualRoot(virtualPath, virtualRoot, scratchArena))
        return false;

    return virtualRoot.view() == Core::Assets::s_ProjectVirtualRoot;
}

static bool TryResolveMaterialBindDependencyInterface(
    const Path& normalizedMaterialBindIncludeRoot,
    const Path& dependency,
    CookString& outInterfacePath,
    ScratchArena& scratchArena
){
    outInterfacePath.clear();
    if(normalizedMaterialBindIncludeRoot.empty())
        return true;

    ErrorCode errorCode;
    Path normalizedDependency = AbsolutePath(dependency, errorCode).lexically_normal();
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize shader dependency '{}': {}")
            , PathToString<tchar>(dependency)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    if(!PathHasDirectoryAncestor(normalizedDependency, normalizedMaterialBindIncludeRoot))
        return true;

    ScratchString extension = PathToString(scratchArena, normalizedDependency.extension());
    CanonicalizeTextInPlace(extension);
    if(extension != MaterialBindNames::SourceExtensionText())
        return true;

    Path relativePath = normalizedDependency.lexically_relative(normalizedMaterialBindIncludeRoot);
    relativePath.replace_extension();
    if(!Core::Assets::AssetPathsDetail::BuildRelativeAssetPathText(relativePath, outInterfacePath)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to derive interface from generated include '{}'")
            , PathToString<tchar>(normalizedDependency)
        );
        return false;
    }

    return true;
}

static bool ResolveMaterialBindDependencyInterface(
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const CookVector<Path>& dependencies,
    CookString& outInterfacePath,
    Name& outInterfaceName,
    bool& outDependsOnMaterialBind,
    ScratchArena& scratchArena
){
    outInterfacePath.clear();
    outInterfaceName = NAME_NONE;
    outDependsOnMaterialBind = false;

    Path normalizedMaterialBindIncludeRoot(materialBindIncludeRoot.arena());
    if(!materialBindIncludeRoot.empty()){
        ErrorCode errorCode;
        normalizedMaterialBindIncludeRoot = AbsolutePath(materialBindIncludeRoot, errorCode).lexically_normal();
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize generated include root '{}': {}")
                , PathToString<tchar>(materialBindIncludeRoot)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    CookArena& arena = outInterfacePath.get_allocator().arena();
    CookString dependencyInterfacePath{arena};
    bool dependsOnMultipleInterfaces = false;
    for(const Path& dependency : dependencies){
        if(!TryResolveMaterialBindDependencyInterface(
            normalizedMaterialBindIncludeRoot,
            dependency,
            dependencyInterfacePath,
            scratchArena
        ))
            return false;
        if(dependencyInterfacePath.empty())
            continue;

        const Name dependencyInterfaceName{ AStringView(dependencyInterfacePath) };
        if(!dependencyInterfaceName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: shader '{}' includes invalid generated "
                "material bind interface '{}'")
                , StringConvert(shaderName)
                , StringConvert(dependencyInterfacePath)
            );
            return false;
        }

        // The shader depends on at least one material's generated `.bind` interface, so it reads the typed
        // material constants and must receive the typed binding.
        outDependsOnMaterialBind = true;
        if(dependsOnMultipleInterfaces)
            continue;

        if(!outInterfaceName){
            outInterfacePath = dependencyInterfacePath;
            outInterfaceName = dependencyInterfaceName;
            continue;
        }

        if(outInterfaceName != dependencyInterfaceName){
            // More than one DISTINCT interface: this is a generic consumer of a cook-generated dispatch module,
            // not a per-material shader. The shadow-transmittance dispatch (included by shadow_ahit and the
            // software traversal CS) #includes every surface material's `.bind` -- namespace-isolated by
            // EmitShadowTransmittanceDispatchModule, so there is no symbol collision -- to evaluate each occluder's
            // transmittance hook by shading-model id. Such a shader still reads the typed binding (above) but has
            // NO single owning interface; only per-material pixel shaders (exactly one interface) carry one for
            // material_validation to match against the material's declaration.
            outInterfacePath.clear();
            outInterfaceName = NAME_NONE;
            dependsOnMultipleInterfaces = true;
        }
    }

    return true;
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
        // Optional: a material with no explicit variant uses the default variant (valid only when its shaders
        // declare no defines). Cook-generated shaders declare none; an explicit-`shaders` material may opt into a
        // specific variant.
        outVariant = Core::ShaderArchive::s_DefaultVariant;
        return true;
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

static bool ParseMaterialBoolProperty(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    bool& outValue
){
    outValue = false;

    const auto* propertyValue = asset.findField(fieldName);
    if(!propertyValue)
        return true;
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

using MaterialBindInterfaceLookup = HashMap<
    Name,
    const MaterialBindEntry*,
    Hasher<Name>,
    EqualTo<Name>,
    Core::Alloc::ScratchArena
>;

static void BuildMaterialBindInterfaceLookup(
    const CookVector<MaterialBindEntry>& materialBindEntries,
    MaterialBindInterfaceLookup& outLookup
){
    outLookup.reserve(materialBindEntries.size());
    for(const MaterialBindEntry& bindEntry : materialBindEntries)
        outLookup.emplace(Name(bindEntry.virtualPath.c_str()), &bindEntry);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// material bind generated Slang include helpers


static void AppendMaterialBindGeneratedSeparator(CookString& inOutSource, const u32 newlineCount){
    static constexpr AStringView s_SeparatorChunk = "////////////////";
    for(u32 i = 0u; i < 8u; ++i)
        inOutSource += s_SeparatorChunk;
    for(u32 i = 0u; i < newlineCount; ++i)
        inOutSource += '\n';
}

static void AppendGeneratedUpperIdentifier(const AStringView text, CookString& inOutText){
    const usize beginSize = inOutText.size();
    for(const char ch : text)
        inOutText += IsAsciiAlphaNumeric(ch) ? ToAsciiUpper(ch) : '_';
    if(inOutText.size() == beginSize)
        inOutText += "VALUE";
}

static void AppendGeneratedPascalIdentifier(const AStringView text, CookString& inOutText){
    const usize beginSize = inOutText.size();
    bool upperNext = true;
    for(const char ch : text){
        if(ch == '_'){
            upperNext = true;
            continue;
        }

        if(upperNext)
            inOutText += ToAsciiUpper(ch);
        else
            inOutText += ch;
        upperNext = false;
    }
    if(inOutText.size() == beginSize)
        inOutText += "Value";
}

static void AppendU32Slang(const u32 value, CookString& inOutText){
    char digits[16u];
    inOutText += FormatDecimal(static_cast<usize>(value), digits);
    inOutText += 'u';
}

static void AppendU64AsUint2Slang(const u64 value, CookString& inOutText){
    inOutText += "uint2(";
    AppendHexU32UnsignedLiteral(static_cast<u32>(value & 0xffffffffull), inOutText);
    inOutText += ", ";
    AppendHexU32UnsignedLiteral(static_cast<u32>(value >> 32u), inOutText);
    inOutText += ")";
}

static CookString BuildMaterialBindIncludeGuard(CookArena& arena, const AStringView includePath){
    CookString guard("NWB_GENERATED_MATERIAL_BIND_", arena);
    AppendGeneratedUpperIdentifier(AStringView(includePath), guard);
    return guard;
}

static CookString BuildMaterialBindGeneratedSymbol(
    CookArena& arena,
    const InitializerList<AStringView> nameSegments,
    const AStringView suffix
){
    CookString symbol("NWB_MATERIAL_BIND_", arena);
    bool firstSegment = true;
    for(const AStringView nameSegment : nameSegments){
        if(!firstSegment)
            symbol += '_';
        AppendGeneratedUpperIdentifier(nameSegment, symbol);
        firstSegment = false;
    }
    symbol += suffix;
    return symbol;
}

static CookString BuildMaterialBindAccessorName(
    CookArena& arena,
    const InitializerList<AStringView> nameSegments
){
    CookString functionName("nwbMaterialBindLoad", arena);
    for(const AStringView nameSegment : nameSegments)
        AppendGeneratedPascalIdentifier(nameSegment, functionName);
    return functionName;
}

static bool RegisterGeneratedMaterialBindSymbol(
    const AStringView includePath,
    const AStringView symbol,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena
){
    ScratchString scratchSymbol(symbol, scratchArena);
    if(inOutSymbols.insert(Move(scratchSymbol)).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': generated symbol '{}' is ambiguous")
        , StringConvert(includePath)
        , StringConvert(symbol)
    );
    return false;
}

static AStringView MaterialBindFieldLookupFunctionStorageName(const MaterialBlockClass::Enum blockClass){
    switch(blockClass){
    case MaterialBlockClass::MaterialConstant: return "Constant";
    case MaterialBlockClass::MaterialMutable: return "Mutable";
    default: return AStringView();
    }
}

static AStringView MaterialBindFieldLookupFunctionTypeName(const MaterialLayoutFieldType::Enum fieldType){
    static constexpr AStringView s_TypeNames[] = {
        "Bool",
        "Bool2",
        "Bool3",
        "Bool4",
        "Char",
        "Char2",
        "Char3",
        "Char4",
        "UChar",
        "UChar2",
        "UChar3",
        "UChar4",
        "Short",
        "Short2",
        "Short3",
        "Short4",
        "UShort",
        "UShort2",
        "UShort3",
        "UShort4",
        "Int",
        "Int2",
        "Int3",
        "Int4",
        "UInt",
        "UInt2",
        "UInt3",
        "UInt4",
        "Half",
        "Half2",
        "Half3",
        "Half4",
        "Float",
        "Float2",
        "Float3",
        "Float4"
    };
    static_assert(
        (sizeof(s_TypeNames) / sizeof(s_TypeNames[0]))
        == static_cast<usize>(
            static_cast<u32>(MaterialLayoutFieldType::Float4) - static_cast<u32>(MaterialLayoutFieldType::Bool) + 1u
        )
    );

    if(!IsValidMaterialLayoutFieldType(fieldType))
        return AStringView();

    return s_TypeNames[static_cast<u32>(fieldType) - static_cast<u32>(MaterialLayoutFieldType::Bool)];
}

static CookString BuildMaterialBindFieldLookupFunctionName(
    CookArena& arena,
    const MaterialLayoutFieldType::Enum fieldType,
    const MaterialBlockClass::Enum blockClass
){
    CookString functionName(arena);
    const AStringView storageName = MaterialBindFieldLookupFunctionStorageName(blockClass);
    const AStringView typeName = MaterialBindFieldLookupFunctionTypeName(fieldType);
    if(storageName.empty() || typeName.empty())
        return functionName;

    functionName += "nwbMaterialLoad";
    functionName += storageName;
    functionName += typeName;
    return functionName;
}

static bool AppendMaterialBindConstantPrefix(
    const AStringView includePath,
    const CookString& symbol,
    const AStringView type,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(symbol), inOutSymbols, scratchArena))
        return false;

    inOutSource += "static const ";
    inOutSource += type;
    inOutSource += ' ';
    inOutSource += symbol;
    inOutSource += " = ";
    return true;
}

static void AppendMaterialBindConstantSuffix(CookString& inOutSource){
    inOutSource += ";\n";
}

static bool AppendMaterialBindU32Constant(
    const AStringView includePath,
    const CookString& symbol,
    const u32 value,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    if(!AppendMaterialBindConstantPrefix(includePath, symbol, "uint", inOutSymbols, scratchArena, inOutSource))
        return false;

    AppendU32Slang(value, inOutSource);
    AppendMaterialBindConstantSuffix(inOutSource);
    return true;
}

static bool AppendMaterialBindU64Constant(
    const AStringView includePath,
    const CookString& symbol,
    const u64 value,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    if(!AppendMaterialBindConstantPrefix(includePath, symbol, "uint2", inOutSymbols, scratchArena, inOutSource))
        return false;

    AppendU64AsUint2Slang(value, inOutSource);
    AppendMaterialBindConstantSuffix(inOutSource);
    return true;
}

static bool ResolveMaterialBindGeneratedLayoutBlock(
    const AStringView includePath,
    const MaterialBindInstance& instance,
    const MaterialBindTypedLayout& layout,
    MaterialBindTypedLayoutBlockLookupEntry& outBlockEntry,
    const MaterialTypedLayoutBlock*& outBlock
){
    outBlockEntry = {};
    outBlock = nullptr;

    const auto blockIt = layout.blockLookup.find(Name(AStringView(instance.name)));
    if(blockIt == layout.blockLookup.end()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' has no typed layout block")
            , StringConvert(includePath)
            , StringConvert(instance.name)
        );
        return false;
    }

    outBlockEntry = blockIt.value();
    if(outBlockEntry.blockIndex >= layout.typedLayoutBlocks.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' typed layout block index is out of range")
            , StringConvert(includePath)
            , StringConvert(instance.name)
        );
        return false;
    }

    outBlock = &layout.typedLayoutBlocks[outBlockEntry.blockIndex];
    return true;
}

static u32* MaterialBindStorageByteSizePointer(
    const MaterialBlockClass::Enum blockClass,
    u32& inOutConstantByteSize,
    u32& inOutMutableByteSize
){
    switch(blockClass){
    case MaterialBlockClass::MaterialConstant: return &inOutConstantByteSize;
    case MaterialBlockClass::MaterialMutable: return &inOutMutableByteSize;
    default: return nullptr;
    }
}

static bool ComputeMaterialBindStorageByteSizes(
    const AStringView includePath,
    const MaterialBindTypedLayout& layout,
    u32& outConstantByteSize,
    u32& outMutableByteSize
){
    outConstantByteSize = 0u;
    outMutableByteSize = 0u;

    for(const MaterialTypedLayoutBlock& block : layout.typedLayoutBlocks){
        u32* storageByteSize = MaterialBindStorageByteSizePointer(block.blockClass, outConstantByteSize, outMutableByteSize);
        if(!storageByteSize){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout block has invalid storage class")
                , StringConvert(includePath)
            );
            return false;
        }
        if(block.byteSize > Limit<u32>::s_Max - *storageByteSize){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout storage byte size exceeds u32")
                , StringConvert(includePath)
            );
            return false;
        }

        *storageByteSize += block.byteSize;
    }

    return true;
}

static bool ComputeMaterialBindBlockStorageByteBegin(
    const AStringView includePath,
    const MaterialBindTypedLayout& layout,
    const u32 blockIndex,
    u32& outByteBegin
){
    outByteBegin = 0u;
    if(blockIndex >= layout.typedLayoutBlocks.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout block index is out of range")
            , StringConvert(includePath)
        );
        return false;
    }

    const MaterialBlockClass::Enum blockClass = layout.typedLayoutBlocks[blockIndex].blockClass;
    for(u32 currentBlockIndex = 0u; currentBlockIndex < blockIndex; ++currentBlockIndex){
        const MaterialTypedLayoutBlock& block = layout.typedLayoutBlocks[currentBlockIndex];
        if(block.blockClass != blockClass)
            continue;
        if(block.byteSize > Limit<u32>::s_Max - outByteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout block byte offset exceeds u32")
                , StringConvert(includePath)
            );
            return false;
        }

        outByteBegin += block.byteSize;
    }

    return true;
}

static bool AppendMaterialBindLayoutConstants(
    CookArena& arena,
    const AStringView includePath,
    const MaterialBindEntry& entry,
    const MaterialBindTypedLayout& layout,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    u32 constantByteSize = 0u;
    u32 mutableByteSize = 0u;
    if(!ComputeMaterialBindStorageByteSizes(includePath, layout, constantByteSize, mutableByteSize))
        return false;

    const Name interfaceName(AStringView(entry.virtualPath));
    const NameHash& interfaceHash = interfaceName.hash();
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane){
        CookString laneSuffix("INTERFACE_HASH_", arena);
        char laneDigits[16u];
        laneSuffix += FormatDecimal(static_cast<usize>(lane), laneDigits);
        const CookString symbol = BuildMaterialBindGeneratedSymbol(arena, {}, AStringView(laneSuffix));
        if(!AppendMaterialBindU64Constant(
            includePath,
            symbol,
            interfaceHash.qwords[lane],
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
    }

    const CookString layoutHashSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "LAYOUT_HASH");
    const CookString blockCountSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "BLOCK_COUNT");
    const CookString fieldCountSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "FIELD_COUNT");
    const CookString storageConstantSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "STORAGE_CONSTANT");
    const CookString storageMutableSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "STORAGE_MUTABLE");
    const CookString constantByteSizeSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "CONSTANT_BYTE_SIZE");
    const CookString mutableByteSizeSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "MUTABLE_BYTE_SIZE");

    if(!AppendMaterialBindU64Constant(includePath, layoutHashSymbol, layout.layoutHash, inOutSymbols, scratchArena, inOutSource))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        blockCountSymbol,
        static_cast<u32>(layout.typedLayoutBlocks.size()),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        fieldCountSymbol,
        static_cast<u32>(layout.typedLayoutFields.size()),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        storageConstantSymbol,
        static_cast<u32>(MaterialBlockClass::MaterialConstant),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        storageMutableSymbol,
        static_cast<u32>(MaterialBlockClass::MaterialMutable),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        constantByteSizeSymbol,
        constantByteSize,
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        mutableByteSizeSymbol,
        mutableByteSize,
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;

    if(entry.instances.size() != layout.typedLayoutBlocks.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout block count mismatch"), StringConvert(includePath));
        return false;
    }

    for(const MaterialBindInstance& instance : entry.instances){
        MaterialBindTypedLayoutBlockLookupEntry blockEntry;
        const MaterialTypedLayoutBlock* block = nullptr;
        if(!ResolveMaterialBindGeneratedLayoutBlock(includePath, instance, layout, blockEntry, block))
            return false;

        u32 blockStorageByteBegin = 0u;
        if(!ComputeMaterialBindBlockStorageByteBegin(includePath, layout, blockEntry.blockIndex, blockStorageByteBegin))
            return false;

        const CookString storageSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name) }, "_STORAGE");
        const CookString offsetSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name) }, "_BYTE_OFFSET");
        const CookString sizeSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name) }, "_BYTE_SIZE");
        if(!AppendMaterialBindU32Constant(
            includePath,
            storageSymbol,
            static_cast<u32>(block->blockClass),
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        if(!AppendMaterialBindU32Constant(
            includePath,
            offsetSymbol,
            blockStorageByteBegin,
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        if(!AppendMaterialBindU32Constant(includePath, sizeSymbol, block->byteSize, inOutSymbols, scratchArena, inOutSource))
            return false;
    }

    return true;
}

static bool AppendMaterialBindFieldConstants(
    const AStringView includePath,
    const MaterialBindStruct& bindStruct,
    const MaterialBindInstance& instance,
    const MaterialBindField& field,
    const CookString& keySymbol,
    const CookString& defaultSymbol,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    const AStringView defaultAttribute = field.defaultArgument();
    if(defaultAttribute.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' must declare a default attribute")
            , StringConvert(includePath)
            , StringConvert(bindStruct.name)
            , StringConvert(field.name)
        );
        return false;
    }

    ACompactString keyText;
    if(!BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(field.name), keyText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' exceeds ACompactString capacity")
            , StringConvert(includePath)
            , StringConvert(bindStruct.name)
            , StringConvert(field.name)
        );
        return false;
    }
    const u64 keyHash = ComputeMaterialBindParameterKeyHash(keyText.view());

    if(!AppendMaterialBindU64Constant(
        includePath,
        keySymbol,
        keyHash,
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;

    if(!AppendMaterialBindConstantPrefix(
        includePath,
        defaultSymbol,
        AStringView(field.type),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;

    inOutSource += defaultAttribute;
    AppendMaterialBindConstantSuffix(inOutSource);
    return true;
}

static void AppendMaterialBindFieldAccessor(
    const MaterialBindField& field,
    const CookString& byteOffsetSymbol,
    const CookString& functionName,
    const AStringView loadFunctionName,
    CookString& inOutSource
){
    inOutSource += field.type;
    inOutSource += ' ';
    inOutSource += functionName;
    inOutSource += "(const NwbMeshInstanceData instance){\n";
    inOutSource += "    return ";
    inOutSource += loadFunctionName;
    inOutSource += "(instance, ";
    inOutSource += byteOffsetSymbol;
    inOutSource += ");\n";
    inOutSource += "}\n\n";
}

static bool AppendMaterialBindGeneratedInstance(
    CookArena& arena,
    const AStringView includePath,
    const MaterialBindInstance& instance,
    const MaterialBindStruct& bindStruct,
    const MaterialBindTypedLayout& layout,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    inOutSource += "\n";
    AppendMaterialBindGeneratedSeparator(inOutSource, 3u);

    MaterialBindTypedLayoutBlockLookupEntry layoutBlockEntry;
    const MaterialTypedLayoutBlock* layoutBlock = nullptr;
    if(!ResolveMaterialBindGeneratedLayoutBlock(
        includePath,
        instance,
        layout,
        layoutBlockEntry,
        layoutBlock
    ))
        return false;

    if(layoutBlock->fieldCount != bindStruct.fields.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' typed layout field count mismatch")
            , StringConvert(includePath)
            , StringConvert(instance.name)
        );
        return false;
    }

    u32 layoutBlockStorageByteBegin = 0u;
    if(!ComputeMaterialBindBlockStorageByteBegin(
        includePath,
        layout,
        layoutBlockEntry.blockIndex,
        layoutBlockStorageByteBegin
    ))
        return false;

    for(u32 fieldOffset = 0u; fieldOffset < layoutBlock->fieldCount; ++fieldOffset){
        const usize layoutFieldIndex = static_cast<usize>(layoutBlock->fieldBegin) + fieldOffset;
        if(layoutFieldIndex >= layout.typedLayoutFields.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' typed layout field range exceeds layout")
                , StringConvert(includePath)
                , StringConvert(instance.name)
            );
            return false;
        }

        const MaterialBindField& field = bindStruct.fields[fieldOffset];
        const MaterialTypedLayoutField& layoutField = layout.typedLayoutFields[layoutFieldIndex];
        if(layoutField.fieldName != Name(AStringView(field.name))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' typed layout metadata mismatch")
                , StringConvert(includePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }
        if(layoutField.offset > Limit<u32>::s_Max - layoutBlockStorageByteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' byte offset exceeds u32")
                , StringConvert(includePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }

        const CookString loadFunctionName = BuildMaterialBindFieldLookupFunctionName(
            arena,
            layoutField.fieldType,
            layoutBlock->blockClass
        );
        if(loadFunctionName.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' has unsupported load type '{}'")
                , StringConvert(includePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
                , StringConvert(field.type)
            );
            return false;
        }

        const CookString keySymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name), AStringView(field.name) }, "_KEY");
        const CookString defaultSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name), AStringView(field.name) }, "_DEFAULT");
        const CookString byteOffsetSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name), AStringView(field.name) }, "_BYTE_OFFSET");
        const CookString functionName =
            BuildMaterialBindAccessorName(arena, { AStringView(instance.name), AStringView(field.name) });
        if(!RegisterGeneratedMaterialBindSymbol(
            includePath,
            AStringView(functionName),
            inOutSymbols,
            scratchArena
        ))
            return false;

        if(!AppendMaterialBindFieldConstants(
            includePath,
            bindStruct,
            instance,
            field,
            keySymbol,
            defaultSymbol,
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        const u32 fieldByteOffset = layoutBlockStorageByteBegin + layoutField.offset;
        if(!AppendMaterialBindU32Constant(
            includePath,
            byteOffsetSymbol,
            fieldByteOffset,
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        inOutSource += '\n';
        AppendMaterialBindFieldAccessor(field, byteOffsetSymbol, functionName, AStringView(loadFunctionName), inOutSource);
        inOutSource += '\n';
    }

    const CookString blockFunctionName = BuildMaterialBindAccessorName(arena, { AStringView(instance.name) });
    if(!RegisterGeneratedMaterialBindSymbol(
        includePath,
        AStringView(blockFunctionName),
        inOutSymbols,
        scratchArena
    ))
        return false;

    inOutSource += bindStruct.name;
    inOutSource += ' ';
    inOutSource += blockFunctionName;
    inOutSource += "(const NwbMeshInstanceData instance){\n";
    inOutSource += "    ";
    inOutSource += bindStruct.name;
    inOutSource += " value;\n";
    for(const MaterialBindField& field : bindStruct.fields){
        const CookString functionName =
            BuildMaterialBindAccessorName(arena, { AStringView(instance.name), AStringView(field.name) });
        inOutSource += "    value.";
        inOutSource += field.name;
        inOutSource += " = ";
        inOutSource += functionName;
        inOutSource += "(instance);\n";
    }
    inOutSource += "    return value;\n";
    inOutSource += "}\n\n";

    return true;
}

static bool BuildMaterialBindIncludeSourceImpl(
    CookArena& arena,
    const MaterialBindEntry& entry,
    CookString& outSource,
    ScratchArena& scratchArena
){
    outSource.clear();

    CookString includePath(arena);
    if(!BuildMaterialBindIncludeVirtualPathImpl(arena, entry, includePath))
        return false;

    const CookString includeGuard = BuildMaterialBindIncludeGuard(arena, AStringView(includePath));

    MaterialBindTypedLayout layout(arena);
    if(!BuildMaterialBindTypedLayout(
        entry,
        Name(AStringView(entry.virtualPath)),
        layout,
        scratchArena
    ))
        return false;

    ScratchHashSet<ScratchString> generatedSymbols{
        0,
        Hasher<ScratchString>(),
        EqualTo<ScratchString>(),
        scratchArena
    };

    outSource += "// generated by NWBLot material bind cook\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);
    outSource += "#ifndef ";
    outSource += includeGuard;
    outSource += "\n#define ";
    outSource += includeGuard;
    outSource += "\n\n\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);
    outSource += "#ifndef NWB_MATERIAL_TYPED_BINDING\n";
    outSource += "#error \"generated material bind includes require mesh/authoring.slangi\"\n";
    outSource += "#endif\n\n";
    outSource += "#ifndef NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE\n";
    outSource += "#error \"generated material bind includes require mesh/authoring.slangi\"\n";
    outSource += "#endif\n\n";
    outSource += "#if NWB_MATERIAL_TYPED_BINDING != NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE\n";
    outSource += "#error \"generated material bind accessors require NWB_MATERIAL_TYPED_BINDING to match NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE\"\n";
    outSource += "#endif\n\n\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);

    if(!AppendMaterialBindLayoutConstants(
        arena,
        AStringView(includePath),
        entry,
        layout,
        generatedSymbols,
        scratchArena,
        outSource
    ))
        return false;

    outSource += "\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);

    for(const MaterialBindStruct& bindStruct : entry.structs){
        if(!RegisterGeneratedMaterialBindSymbol(
            AStringView(includePath),
            AStringView(bindStruct.name),
            generatedSymbols,
            scratchArena
        ))
            return false;

        outSource += "struct ";
        outSource += bindStruct.name;
        outSource += "{\n";
        for(const MaterialBindField& field : bindStruct.fields){
            outSource += "    ";
            outSource += field.type;
            outSource += ' ';
            outSource += field.name;
            outSource += ";\n";
        }
        outSource += "};\n\n";
    }

    for(const MaterialBindInstance& instance : entry.instances){
        const MaterialBindStruct* bindStruct = entry.findStruct(AStringView(instance.type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' references unknown struct type '{}'")
                , StringConvert(includePath)
                , StringConvert(instance.name)
                , StringConvert(instance.type)
            );
            return false;
        }

        if(!AppendMaterialBindGeneratedInstance(
            arena,
            AStringView(includePath),
            instance,
            *bindStruct,
            layout,
            generatedSymbols,
            scratchArena,
            outSource
        ))
            return false;
    }

    outSource += "\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);
    outSource += "#endif\n\n\n";
    AppendMaterialBindGeneratedSeparator(outSource, 1u);
    return true;
}

static bool PrepareMaterialBindIncludeRoot(const Path& includeRoot){
    ErrorCode errorCode;
    if(!RemoveAllIfExists(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to clear generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    if(!EnsureDirectories(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to create generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}

static bool EmitMaterialBindIncludes(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialBindEntry>& materialBindEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
){
    outIncludeRoot.clear();
    outIncludeRoot = BuildMaterialBindIncludeRoot(cacheDirectory, configurationSafeName, scratchArena);
    if(!PrepareMaterialBindIncludeRoot(outIncludeRoot))
        return false;
    if(materialBindEntries.empty())
        return true;

    CookHashSet<CookString> seenIncludePaths{arena};
    seenIncludePaths.reserve(materialBindEntries.size());

    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        CookString includePath(arena);
        if(!BuildMaterialBindIncludeVirtualPathImpl(arena, bindEntry, includePath))
            return false;
        if(!seenIncludePaths.insert(includePath).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: duplicate material bind include path '{}'")
                , StringConvert(includePath)
            );
            return false;
        }

        CookString generatedSource{arena};
        if(!BuildMaterialBindIncludeSourceImpl(arena, bindEntry, generatedSource, scratchArena))
            return false;

        const Path outputPath = outIncludeRoot / includePath.c_str();
        ErrorCode errorCode;
        if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to create generated include parent '{}': {}")
                , PathToString<tchar>(outputPath.parent_path())
                , StringConvert(errorCode.message())
            );
            return false;
        }

        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to write generated include '{}'")
                , PathToString<tchar>(outputPath)
            );
            return false;
        }
    }

    return true;
}


static bool ValidateMaterialCookInterfaces(
    const CookVector<MaterialBindEntry>& materialBindEntries,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){
    MaterialBindInterfaceLookup materialBindLookup(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    BuildMaterialBindInterfaceLookup(materialBindEntries, materialBindLookup);

    const usize cacheReserveCount = Min(materialBindEntries.size(), materialEntries.size());
    MaterialBindTypedLayoutCache layoutCache(materialEntries.get_allocator().arena());
    layoutCache.reserve(cacheReserveCount);

    for(MaterialCookEntry& materialEntry : materialEntries){
        materialEntry.typedLayoutHash = 0u;
        materialEntry.typedLayoutBlocks.clear();
        materialEntry.typedLayoutFields.clear();
        materialEntry.typedBlockBytes.clear();

        if(materialEntry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' is missing required material interface")
                , StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }

        // The interface is stored as text; build the Name hash key the bind lookup + typed-layout machinery need.
        const Name materialInterfaceName(AStringView(materialEntry.materialInterface));
        const auto bindEntryIt = materialBindLookup.find(materialInterfaceName);
        if(bindEntryIt == materialBindLookup.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' references unknown material interface '{}'")
                , StringConvert(materialEntry.virtualPath.c_str())
                , StringConvert(materialEntry.materialInterface.c_str())
            );
            return false;
        }
        const MaterialBindEntry* bindEntry = bindEntryIt.value();

        const MaterialBindTypedLayout* layout = nullptr;
        if(!FindOrBuildMaterialBindTypedLayout(
            materialInterfaceName,
            *bindEntry,
            layoutCache,
            layout,
            scratchArena
        ))
            return false;
        if(!layout){
            NWB_LOGGER_ERROR(NWB_TEXT("Material '{}' failed to resolve typed layout cache for interface '{}'")
                , StringConvert(materialEntry.virtualPath.c_str())
                , StringConvert(materialEntry.materialInterface.c_str())
            );
            return false;
        }

        CopyMaterialBindTypedLayoutDefaults(
            *layout,
            materialEntry.typedLayoutHash,
            materialEntry.typedLayoutBlocks,
            materialEntry.typedLayoutFields,
            materialEntry.typedBlockBytes
        );
        if(!ApplyMaterialBindTypedLayoutParameters(
            *layout,
            Name(AStringView(materialEntry.virtualPath)),
            materialEntry.parameters,
            materialEntry.typedBlockBytes
        ))
            return false;
    }

    return true;
}

static bool ParseMaterialMeta(
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
    if(!ParseMaterialParameters(nwbFilePath, asset, outEntry.parameters))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// deferred lighting BXDF dispatch (per-material shading model id + generated dispatch module)


static constexpr AStringView s_DeferredBxdfFunctionMacro = "NWB_DEFERRED_BXDF_FUNCTION";
static constexpr AStringView s_DeferredBxdfModelPrefix = "nwbDeferredBxdfModel";
static constexpr AStringView s_DeferredBxdfModuleSubPath = "deferred/generated/bxdf_dispatch.slangi";

// Sentinel shadow-transmittance id for a material that contributes NO surface hook (it declares explicit
// `shaders` instead of a `.surface`). The dense surface-authored ids start at 0, so a surface-less material must
// NOT reuse 0 (that aliases the first real surface hook). This reserved id is never emitted as a `case` in the
// generated dispatch switch, so a hit on such an occluder falls through to `default: return half3(1)` -- the
// occluder passes all light untinted, the only correct behavior for a material with no transmittance hook.
static constexpr u32 s_ShadowTransmittanceNoSurfaceModelId = Limit<u32>::s_Max;

static bool AssignMaterialShadingModelIdsImpl(
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.bxdfSource.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' is missing a deferred bxdf"), StringConvert(entry.virtualPath.c_str()));
            return false;
        }
    }

    Vector<AStringView, ScratchArena> uniqueSources(scratchArena);
    uniqueSources.reserve(materialEntries.size());
    for(const MaterialCookEntry& entry : materialEntries)
        uniqueSources.push_back(AStringView(entry.bxdfSource));
    Sort(uniqueSources.begin(), uniqueSources.end(), [](const AStringView lhs, const AStringView rhs){ return lhs < rhs; });

    usize uniqueCount = 0u;
    for(usize i = 0u; i < uniqueSources.size(); ++i){
        if(i == 0u || uniqueSources[i] != uniqueSources[uniqueCount - 1u])
            uniqueSources[uniqueCount++] = uniqueSources[i];
    }
    uniqueSources.resize(uniqueCount);
    if(uniqueCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: too many unique deferred bxdfs"));
        return false;
    }

    for(MaterialCookEntry& entry : materialEntries){
        const AStringView source(entry.bxdfSource);
        bool assigned = false;
        for(usize id = 0u; id < uniqueSources.size(); ++id){
            if(uniqueSources[id] == source){
                entry.shadingModelId = static_cast<u32>(id);
                assigned = true;
                break;
            }
        }
        if(!assigned){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: failed to assign shading model id for '{}'"), StringConvert(entry.virtualPath.c_str()));
            return false;
        }
    }

    // Assign each material a separate shadow-transmittance id deduped over the unique `.surface` sources (the
    // surface hook computes the per-hit transmittance the shadow trace returns). A material that declares
    // explicit `shaders` instead of a `surface` gets the reserved no-surface sentinel id (NOT 0, which is the
    // first real surface hook); the generated dispatch routes that sentinel to its `default` -> float3(1), so a
    // transparent surface-less occluder passes all light untinted instead of evaluating an unrelated material.
    Vector<AStringView, ScratchArena> uniqueSurfaces(scratchArena);
    uniqueSurfaces.reserve(materialEntries.size());
    for(const MaterialCookEntry& entry : materialEntries){
        if(!entry.surfaceSource.empty())
            uniqueSurfaces.push_back(AStringView(entry.surfaceSource));
    }
    Sort(uniqueSurfaces.begin(), uniqueSurfaces.end(), [](const AStringView lhs, const AStringView rhs){ return lhs < rhs; });

    usize uniqueSurfaceCount = 0u;
    for(usize i = 0u; i < uniqueSurfaces.size(); ++i){
        if(i == 0u || uniqueSurfaces[i] != uniqueSurfaces[uniqueSurfaceCount - 1u])
            uniqueSurfaces[uniqueSurfaceCount++] = uniqueSurfaces[i];
    }
    uniqueSurfaces.resize(uniqueSurfaceCount);
    if(uniqueSurfaceCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: too many unique shadow transmittance surfaces"));
        return false;
    }

    for(MaterialCookEntry& entry : materialEntries){
        if(entry.surfaceSource.empty()){
            entry.shadowTransmittanceModelId = s_ShadowTransmittanceNoSurfaceModelId;
            continue;
        }

        const AStringView surface(entry.surfaceSource);
        bool assigned = false;
        for(usize id = 0u; id < uniqueSurfaces.size(); ++id){
            if(uniqueSurfaces[id] == surface){
                entry.shadowTransmittanceModelId = static_cast<u32>(id);
                assigned = true;
                break;
            }
        }
        if(!assigned){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: failed to assign shadow transmittance id for '{}'"), StringConvert(entry.virtualPath.c_str()));
            return false;
        }
    }
    return true;
}

static Path BuildDeferredBxdfIncludeRoot(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    ScratchArena& scratchArena
){
    ScratchString configurationName(configurationSafeName, scratchArena);
    return cacheDirectory / configurationName.c_str() / "deferred_modules";
}

static bool PrepareDeferredBxdfIncludeRoot(const Path& includeRoot){
    ErrorCode errorCode;
    if(!RemoveAllIfExists(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to clear generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    if(!EnsureDirectories(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to create generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}

static bool EmitDeferredBxdfDispatchModuleImpl(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
){
    outIncludeRoot.clear();
    outIncludeRoot = BuildDeferredBxdfIncludeRoot(cacheDirectory, configurationSafeName, scratchArena);
    if(!PrepareDeferredBxdfIncludeRoot(outIncludeRoot))
        return false;

    // Build a dense id -> bxdf-source table from the (already assigned) materials. Each unique bxdf appears at
    // exactly one id; materials sharing a bxdf share the slot.
    u32 maxId = 0u;
    bool anyBxdf = false;
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
            return false;
        }
        slot = source;
    }

    CookArena& arena = materialEntries.get_allocator().arena();
    CookString source(arena);
    source += "// Generated by AssetVolumeCooker from material `bxdf` declarations. Do not edit.\n";
    source += "#ifndef NWB_GRAPHICS_DEFERRED_GENERATED_BXDF_DISPATCH_SLANGI\n";
    source += "#define NWB_GRAPHICS_DEFERRED_GENERATED_BXDF_DISPATCH_SLANGI\n\n";

    for(usize id = 0u; id < sourceById.size(); ++id){
        if(sourceById[id].empty())
            continue;

        char idText[32] = {};
        const AStringView idView = FormatDecimal(static_cast<u32>(id), idText);
        source += "#define ";
        source += s_DeferredBxdfFunctionMacro;
        source += ' ';
        source += s_DeferredBxdfModelPrefix;
        source += idView;
        source += "\n#include \"";
        source += sourceById[id];
        source += "\"\n#undef ";
        source += s_DeferredBxdfFunctionMacro;
        source += "\n\n";
    }

    source += "half3 nwbDeferredDispatchBxdf(uint shadingModel, NwbBxdfSurface surface, int2 pixel){\n";
    source += "    switch(shadingModel){\n";
    for(usize id = 0u; id < sourceById.size(); ++id){
        if(sourceById[id].empty())
            continue;

        char idText[32] = {};
        const AStringView idView = FormatDecimal(static_cast<u32>(id), idText);
        source += "    case ";
        source += idView;
        source += "u: return ";
        source += s_DeferredBxdfModelPrefix;
        source += idView;
        source += "(surface, pixel);\n";
    }
    source += "    default: return half3(1.0, 0.0, 1.0); // no engine default BXDF: an unknown id shows magenta\n";
    source += "    }\n";
    source += "}\n\n#endif\n";

    const Path outputPath = outIncludeRoot / s_DeferredBxdfModuleSubPath.data();
    ErrorCode errorCode;
    if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to create generated include parent '{}': {}")
            , PathToString<tchar>(outputPath.parent_path())
            , StringConvert(errorCode.message())
        );
        return false;
    }
    if(!WriteTextFile(outputPath, AStringView(source))){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to write generated include '{}'")
            , PathToString<tchar>(outputPath)
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// shadow transmittance dispatch (per-material surface id + generated dispatch module)


static constexpr AStringView s_ShadowTransmittanceSurfaceMacro = "nwbMaterialSurface";
static constexpr AStringView s_ShadowTransmittanceModelPrefix = "nwbShadowSurfaceModel";
static constexpr AStringView s_ShadowTransmittanceWrapperPrefix = "nwbShadowTransmittanceModel";
// Per-id Slang namespace that isolates each material's `.bind` file-scope symbols in the dispatch module (the one
// TU that concatenates multiple `.bind` files) so distinct interfaces' fixed-named layout constants/structs/
// accessors do not collide; a `using namespace` then exposes them to the global-scope surface hook.
static constexpr AStringView s_ShadowTransmittanceBindNamespacePrefix = "nwbShadowBindModel";
static constexpr AStringView s_ShadowTransmittanceModuleSubPath = "shadow/generated/transmittance_dispatch.slangi";

static Path BuildShadowTransmittanceIncludeRoot(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    ScratchArena& scratchArena
){
    ScratchString configurationName(configurationSafeName, scratchArena);
    return cacheDirectory / configurationName.c_str() / "shadow_modules";
}

static bool PrepareShadowTransmittanceIncludeRoot(const Path& includeRoot){
    ErrorCode errorCode;
    if(!RemoveAllIfExists(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: failed to clear generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    if(!EnsureDirectories(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: failed to create generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}

static bool EmitShadowTransmittanceDispatchModuleImpl(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
){
    outIncludeRoot.clear();
    outIncludeRoot = BuildShadowTransmittanceIncludeRoot(cacheDirectory, configurationSafeName, scratchArena);
    if(!PrepareShadowTransmittanceIncludeRoot(outIncludeRoot))
        return false;

    // Build a dense shadowTransmittanceModelId -> (surface source, .bind interface) table from the (already
    // assigned) materials. Each unique surface appears at exactly one id; materials sharing a surface share the
    // slot. The interface is carried alongside because the surface hook reads its typed `.bind` accessors by
    // fixed name -- materials sharing a surface therefore share the interface.
    u32 maxId = 0u;
    bool anySurface = false;
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.surfaceSource.empty())
            continue;
        anySurface = true;
        if(entry.shadowTransmittanceModelId > maxId)
            maxId = entry.shadowTransmittanceModelId;
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
        AStringView& surfaceSlot = surfaceById[entry.shadowTransmittanceModelId];
        if(!surfaceSlot.empty() && surfaceSlot != surface){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: model id {} maps to multiple surface sources"), entry.shadowTransmittanceModelId);
            return false;
        }
        surfaceSlot = surface;

        const AStringView interfaceName(entry.materialInterface);
        AStringView& interfaceSlot = interfaceById[entry.shadowTransmittanceModelId];
        if(!interfaceSlot.empty() && interfaceSlot != interfaceName){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: model id {} maps to multiple material interfaces"), entry.shadowTransmittanceModelId);
            return false;
        }
        interfaceSlot = interfaceName;
    }

    CookArena& arena = materialEntries.get_allocator().arena();
    CookString source(arena);
    source += "// Generated by AssetVolumeCooker from material `surface` declarations. Do not edit.\n";
    source += "#ifndef NWB_GRAPHICS_SHADOW_GENERATED_TRANSMITTANCE_DISPATCH_SLANGI\n";
    source += "#define NWB_GRAPHICS_SHADOW_GENERATED_TRANSMITTANCE_DISPATCH_SLANGI\n\n";

    // The trace material-constants context (NwbShadowHit + the per-invocation accessors the surface hooks read
    // -- nwbMeshLoadInstance / nwbMeshMaterialConstantByteOffset / ... -- + the surface contract) is supplied by
    // the includer BEFORE this module, exactly as the deferred BXDF dispatch relies on lighting_ps to bring in the
    // framework first. The includer (each shadow trace shader) #includes shadow/shadow_surface.slangi -- where it
    // also points the material-constants buffers at its own binding set -- then this module; emitting the framework
    // include here instead would force a virtual engine/ path that the shader -I roots do not resolve.

    // Per-id surface hook. This is the ONE translation unit that pulls MULTIPLE materials' `.bind` files together
    // (the per-material G-buffer PS pulls only its own one `.bind`), and a generated `.bind` emits file-scope
    // symbols with FIXED, non-interface-qualified names -- the layout constants (NWB_MATERIAL_BIND_LAYOUT_HASH /
    // _BLOCK_COUNT / _FIELD_COUNT / _INTERFACE_HASH_n / per-field _KEY/_DEFAULT/_BYTE_OFFSET) are byte-identical
    // names across ANY two distinct `.bind` files, so concatenating two distinct interfaces would redefine them.
    // Isolation: wrap each id's `.bind` body in its own Slang namespace (nwbShadowBindModel<id>) so its file-scope
    // symbols + structs + accessors are namespace-qualified and never collide across interfaces, then bring that
    // namespace into scope with `using namespace` so the (global-scope) surface hook -- which references the bind
    // accessors/structs by their fixed unqualified names -- still resolves them. The `.surface` is included at
    // global scope (after the using) so any shared helper headers it pulls in stay global + guarded (one copy
    // across ids); the surface's lookups for global framework symbols (nwbMeshLoadInstance / nwbMakeMeshSurface /
    // inNormal) are unaffected. Two ids that share an interface collapse to one `.bind` body via its per-path
    // include guard (the second namespace is empty); the shared interface's symbols stay reachable through the
    // first id's `using`, so the shared-interface case keeps working. The surface hook itself is still renamed to
    // a unique per-id name (nwbShadowSurfaceModel<id>) so multiple hooks coexist; the wrapper sets the trace
    // material context from the hit + loads the surface-input statics (inlining nwbMaterialSurfaceAt) before
    // invoking the renamed hook, then returns the hook's whole NwbMeshSurface (its optical params -- refractionIor /
    // shadowAbsorptionTint -- plus base color / normal). The hook supplies only the params; the ENGINE integrates the shadow
    // visibility over the true entry->exit volume path (per-crossing Fresnel + signed Beer-Lambert optical depth),
    // since only the trace sees both faces of an occluder. There is no per-hit transmittance for the hook to own.
    for(usize id = 0u; id < surfaceById.size(); ++id){
        if(surfaceById[id].empty())
            continue;

        char idText[32] = {};
        const AStringView idView = FormatDecimal(static_cast<u32>(id), idText);
        source += "namespace ";
        source += s_ShadowTransmittanceBindNamespacePrefix;
        source += idView;
        source += "{\n#include \"";
        source += interfaceById[id];
        source += ".bind\"\n}\n";
        source += "using namespace ";
        source += s_ShadowTransmittanceBindNamespacePrefix;
        source += idView;
        source += ";\n";
        source += "#define ";
        source += s_ShadowTransmittanceSurfaceMacro;
        source += ' ';
        source += s_ShadowTransmittanceModelPrefix;
        source += idView;
        source += "\n#include \"";
        source += surfaceById[id];
        source += "\"\n#undef ";
        source += s_ShadowTransmittanceSurfaceMacro;
        source += "\n";
        source += "NwbMeshSurface ";
        source += s_ShadowTransmittanceWrapperPrefix;
        source += idView;
        source += "(NwbShadowHit hit){\n";
        source += "    nwbShadowSetMaterialContext(hit);\n";
        source += "    NwbMeshSurfaceInputs in = nwbShadowBuildSurfaceInputs(hit);\n";
        source += "    nwbLoadMeshSurfaceInputs(in);\n";
        source += "    return ";
        source += s_ShadowTransmittanceModelPrefix;
        source += idView;
        source += "();\n";
        source += "}\n\n";
    }

    source += "NwbMeshSurface nwbShadowDispatchSurface(uint shadingModel, NwbShadowHit hit){\n";
    source += "    switch(shadingModel){\n";
    for(usize id = 0u; id < surfaceById.size(); ++id){
        if(surfaceById[id].empty())
            continue;

        char idText[32] = {};
        const AStringView idView = FormatDecimal(static_cast<u32>(id), idText);
        source += "    case ";
        source += idView;
        source += "u: return ";
        source += s_ShadowTransmittanceWrapperPrefix;
        source += idView;
        source += "(hit);\n";
    }
    // Unknown id: a neutral surface (ior = 1, transmission = white -> no Fresnel attenuation, no absorption), so a
    // no-match occluder behaves as the prior all-light default.
    source += "    default: return nwbMakeMeshSurface(half3(0.0, 0.0, 0.0), hit.worldNormal, half(0.0), half(0.0));\n";
    source += "    }\n";
    source += "}\n\n#endif\n";

    const Path outputPath = outIncludeRoot / s_ShadowTransmittanceModuleSubPath.data();
    ErrorCode errorCode;
    if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: failed to create generated include parent '{}': {}")
            , PathToString<tchar>(outputPath.parent_path())
            , StringConvert(errorCode.message())
        );
        return false;
    }
    if(!WriteTextFile(outputPath, AStringView(source))){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: failed to write generated include '{}'")
            , PathToString<tchar>(outputPath)
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool EmitMaterialPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView sharedMeshShaderName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
    ScratchArena& scratchArena
){
    outGenerated.clear();

    const Name sharedMeshShaderNameId = ToName(sharedMeshShaderName);
    if(!sharedMeshShaderNameId){
        NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: invalid shared mesh shader name '{}'")
            , StringConvert(sharedMeshShaderName)
        );
        return false;
    }

    ScratchString configurationName(configurationSafeName, scratchArena);
    const Path generatedRoot = cacheDirectory / configurationName.c_str() / "generated" / "material_pixel_shaders";
    ErrorCode errorCode;
    if(!RemoveAllIfExists(generatedRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to clear generated directory '{}': {}")
            , PathToString<tchar>(generatedRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    for(MaterialCookEntry& entry : materialEntries){
        const bool hasExplicitShaders = !entry.stageShaders.empty();
        const bool hasSurface = !entry.surfaceSource.empty();
        if(hasExplicitShaders){
            if(hasSurface){
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' declares both 'surface' and 'shaders'; declare exactly one")
                    , StringConvert(entry.virtualPath.c_str())
                );
                return false;
            }
            continue;
        }
        if(!hasSurface){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' declares neither 'surface' nor 'shaders'")
                , StringConvert(entry.virtualPath.c_str())
            );
            return false;
        }
        if(entry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' needs an interface to generate its pixel shader")
                , StringConvert(entry.virtualPath.c_str())
            );
            return false;
        }

        // The generated pixel shader: engine PS authoring + this material's typed .bind (by interface virtual
        // path) + its resolved surface hook (by absolute path). Mesh stage is the shared engine mesh shader.
        CookString generatedSource(arena);
        generatedSource += "// Generated per-material G-buffer pixel shader: engine pixel-shader authoring + this material's\n";
        generatedSource += "// typed .bind + its surface hook. The material declares only its 'surface' fragment; the cook\n";
        generatedSource += "// assembles this here, in the cook cache. Do not edit -- regenerated every cook.\n";
        generatedSource += "#include \"mesh/material_ps_authoring.slangi\"\n";
        generatedSource += "#include \"";
        generatedSource += entry.materialInterface.c_str();
        generatedSource += ".bind\"\n";
        generatedSource += "#include \"";
        generatedSource += entry.surfaceSource.c_str();
        generatedSource += "\"\n";

        CookString relativeFile(arena);
        relativeFile += entry.virtualPath.c_str();
        relativeFile += ".slang";
        const Path outputPath = generatedRoot / relativeFile.c_str();
        errorCode.clear();
        if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to create generated parent '{}': {}")
                , PathToString<tchar>(outputPath.parent_path())
                , StringConvert(errorCode.message())
            );
            return false;
        }
        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to write generated pixel shader '{}'")
                , PathToString<tchar>(outputPath)
            );
            return false;
        }

        GeneratedMaterialPixelShader generated(arena);
        generated.name += "generated/material_ps/";
        generated.name += entry.virtualPath.c_str();
        {
            ScratchString sourceText = PathToString(scratchArena, outputPath);
            for(char& ch : sourceText){
                if(ch == '\\')
                    ch = '/';
            }
            generated.source += sourceText.c_str();
        }

        const Name pixelShaderName = ToName(AStringView(generated.name));
        if(!pixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: generated pixel shader name is invalid for material '{}'")
                , StringConvert(entry.virtualPath.c_str())
            );
            return false;
        }

        Core::Assets::AssetRef<Shader> pixelShaderRef;
        pixelShaderRef.virtualPath = pixelShaderName;
        Core::Assets::AssetRef<Shader> meshShaderRef;
        meshShaderRef.virtualPath = sharedMeshShaderNameId;
        if(!entry.stageShaders.emplace(Core::ShaderType::PixelStage, pixelShaderRef).second
            || !entry.stageShaders.emplace(Core::ShaderType::MeshStage, meshShaderRef).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: failed to assign generated stage shaders for material '{}'")
                , StringConvert(entry.virtualPath.c_str())
            );
            return false;
        }

        outGenerated.push_back(Move(generated));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared body for the three per-material AVBOIT pass-PS generators (accumulate / occupancy / extinction). All three
// generate ONE PS per TRANSPARENT `surface`-authored material that includes the engine pass-authoring header + the
// material's typed .bind + its resolved surface hook, so all three read this material's SAME shader-decided
// surface.renderCoverage. They differ only in the generated-directory leaf, the included authoring header, the log label,
// the generated-name prefix, and which entry name field records the identity -- threaded through here. Unlike the
// G-buffer PS these are NOT material stage shaders; the renderer derives each PS's identity from the material name
// + the matching prefix to bind it for the transparent draw. Opaque materials + transparent materials with explicit
// `shaders` are skipped (the latter fall back to the fixed pass PS).
static bool EmitMaterialAvboitPassPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView generatedDirectoryLeaf,
    const AStringView authoringHeaderInclude,
    const AStringView passLabel,
    const AStringView generatedNamePrefix,
    MaterialCookString MaterialCookEntry::* entryShaderNameField,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
    ScratchArena& scratchArena
){
    outGenerated.clear();

    ScratchString configurationName(configurationSafeName, scratchArena);
    const Path generatedRoot = cacheDirectory / configurationName.c_str() / "generated" / ScratchString(generatedDirectoryLeaf, scratchArena).c_str();
    ErrorCode errorCode;
    if(!RemoveAllIfExists(generatedRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to clear generated directory '{}': {}")
            , StringConvert(passLabel)
            , PathToString<tchar>(generatedRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    for(MaterialCookEntry& entry : materialEntries){
        if(!entry.transparent)
            continue;
        if(entry.surfaceSource.empty())
            continue;
        if(entry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: transparent material '{}' needs an interface to generate its AVBOIT {} pixel shader")
                , StringConvert(entry.virtualPath.c_str())
                , StringConvert(passLabel)
            );
            return false;
        }

        // The generated pass pixel shader: engine AVBOIT pass authoring + this material's typed .bind (by interface
        // virtual path) + its resolved surface hook (by absolute path) -- the same .bind + .surface pair the
        // G-buffer PS uses, so the transparent pass reads the material's own shader-decided surface.renderCoverage.
        CookString generatedSource(arena);
        generatedSource += "// Generated per-material AVBOIT pass pixel shader: engine AVBOIT pass authoring + this material's\n";
        generatedSource += "// typed .bind + its surface hook. The material declares only its 'surface' fragment; the cook\n";
        generatedSource += "// assembles this here, in the cook cache. Do not edit -- regenerated every cook.\n";
        generatedSource += "#include \"";
        generatedSource += authoringHeaderInclude;
        generatedSource += "\"\n";
        generatedSource += "#include \"";
        generatedSource += entry.materialInterface.c_str();
        generatedSource += ".bind\"\n";
        generatedSource += "#include \"";
        generatedSource += entry.surfaceSource.c_str();
        generatedSource += "\"\n";

        CookString relativeFile(arena);
        relativeFile += entry.virtualPath.c_str();
        relativeFile += ".slang";
        const Path outputPath = generatedRoot / relativeFile.c_str();
        errorCode.clear();
        if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to create generated parent '{}': {}")
                , StringConvert(passLabel)
                , PathToString<tchar>(outputPath.parent_path())
                , StringConvert(errorCode.message())
            );
            return false;
        }
        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to write generated pixel shader '{}'")
                , StringConvert(passLabel)
                , PathToString<tchar>(outputPath)
            );
            return false;
        }

        GeneratedMaterialPixelShader generated(arena);
        generated.name += generatedNamePrefix;
        generated.name += entry.virtualPath.c_str();
        {
            ScratchString sourceText = PathToString(scratchArena, outputPath);
            for(char& ch : sourceText){
                if(ch == '\\')
                    ch = '/';
            }
            generated.source += sourceText.c_str();
        }

        const Name pixelShaderName = ToName(AStringView(generated.name));
        if(!pixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: generated AVBOIT {} pixel shader name is invalid for material '{}'")
                , StringConvert(passLabel)
                , StringConvert(entry.virtualPath.c_str())
            );
            return false;
        }

        // Record the generated PS identity so the CSG clip-variant collector can give it AVBOIT CSG clip variants
        // and the renderer can bind it for the transparent draw, both keyed by the SAME name.
        (entry.*entryShaderNameField).assign(AStringView(generated.name));

        outGenerated.push_back(Move(generated));
    }

    return true;
}

static bool EmitMaterialAvboitAccumulatePixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
    ScratchArena& scratchArena
){
    return EmitMaterialAvboitPassPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        AStringView("material_avboit_accumulate_pixel_shaders"),
        AStringView("avboit/accumulate_ps_authoring.slangi"),
        AStringView("accumulate"),
        s_AvboitAccumulatePixelShaderGeneratedPrefix,
        &MaterialCookEntry::avboitAccumulatePixelShaderName,
        materialEntries,
        outGenerated,
        scratchArena
    );
}

static bool EmitMaterialAvboitOccupancyPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
    ScratchArena& scratchArena
){
    return EmitMaterialAvboitPassPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        AStringView("material_avboit_occupancy_pixel_shaders"),
        AStringView("avboit/occupancy_ps_authoring.slangi"),
        AStringView("occupancy"),
        s_AvboitOccupancyPixelShaderGeneratedPrefix,
        &MaterialCookEntry::avboitOccupancyPixelShaderName,
        materialEntries,
        outGenerated,
        scratchArena
    );
}

static bool EmitMaterialAvboitExtinctionPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
    ScratchArena& scratchArena
){
    return EmitMaterialAvboitPassPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        AStringView("material_avboit_extinction_pixel_shaders"),
        AStringView("avboit/extinction_ps_authoring.slangi"),
        AStringView("extinction"),
        s_AvboitExtinctionPixelShaderGeneratedPrefix,
        &MaterialCookEntry::avboitExtinctionPixelShaderName,
        materialEntries,
        outGenerated,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseMaterialCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MaterialCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::ParseMaterialMeta(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        outEntry,
        scratchArena
    );
}

bool ValidateMaterialCookInterfaces(
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::ValidateMaterialCookInterfaces(materialBindEntries, materialEntries, scratchArena);
}

bool BuildMaterialBindIncludeSource(
    MaterialCookArena& arena,
    const MaterialBindEntry& entry,
    MaterialCookString& outSource,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::BuildMaterialBindIncludeSourceImpl(arena, entry, outSource, scratchArena);
}

bool EmitMaterialBindIncludes(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialCookVector<MaterialBindEntry>& materialBindEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitMaterialBindIncludes(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialBindEntries,
        outIncludeRoot,
        scratchArena
    );
}

bool ResolveMaterialBindDependencyInterface(
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const MaterialCookVector<Path>& dependencies,
    MaterialCookString& outInterfacePath,
    Name& outInterfaceName,
    bool& outDependsOnMaterialBind,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::ResolveMaterialBindDependencyInterface(
        shaderName,
        materialBindIncludeRoot,
        dependencies,
        outInterfacePath,
        outInterfaceName,
        outDependsOnMaterialBind,
        scratchArena
    );
}

bool BuildMaterialAsset(const MaterialCookEntry& materialEntry, Material& outMaterial){
    Core::Assets::AssetArena& arena = materialEntry.shaderVariant.get_allocator().arena();
    if(materialEntry.materialInterface.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' is missing required material interface")
            , StringConvert(materialEntry.virtualPath.c_str())
        );
        return false;
    }
    if(materialEntry.typedLayoutHash == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: interface material '{}' is missing typed layout data")
            , StringConvert(materialEntry.virtualPath.c_str())
        );
        return false;
    }
    if(materialEntry.shaderVariant.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has empty shader variant")
            , StringConvert(materialEntry.virtualPath.c_str())
        );
        return false;
    }

    outMaterial = Material(arena, Name(AStringView(materialEntry.virtualPath)));
    outMaterial.setShaderVariant(materialEntry.shaderVariant);
    outMaterial.setMaterialInterface(Name(AStringView(materialEntry.materialInterface)));
    outMaterial.setShadingModelId(materialEntry.shadingModelId);
    outMaterial.setShadowTransmittanceModelId(materialEntry.shadowTransmittanceModelId);
    if(!materialEntry.avboitAccumulatePixelShaderName.empty()){
        const Name avboitAccumulatePixelShaderName = ToName(AStringView(materialEntry.avboitAccumulatePixelShaderName));
        if(!avboitAccumulatePixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has an invalid AVBOIT accumulate pixel shader name")
                , StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }
        Core::Assets::AssetRef<Shader> avboitAccumulatePixelShaderRef;
        avboitAccumulatePixelShaderRef.virtualPath = avboitAccumulatePixelShaderName;
        outMaterial.setAvboitAccumulatePixelShader(avboitAccumulatePixelShaderRef);
    }
    if(!materialEntry.avboitOccupancyPixelShaderName.empty()){
        const Name avboitOccupancyPixelShaderName = ToName(AStringView(materialEntry.avboitOccupancyPixelShaderName));
        if(!avboitOccupancyPixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has an invalid AVBOIT occupancy pixel shader name")
                , StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }
        Core::Assets::AssetRef<Shader> avboitOccupancyPixelShaderRef;
        avboitOccupancyPixelShaderRef.virtualPath = avboitOccupancyPixelShaderName;
        outMaterial.setAvboitOccupancyPixelShader(avboitOccupancyPixelShaderRef);
    }
    if(!materialEntry.avboitExtinctionPixelShaderName.empty()){
        const Name avboitExtinctionPixelShaderName = ToName(AStringView(materialEntry.avboitExtinctionPixelShaderName));
        if(!avboitExtinctionPixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' has an invalid AVBOIT extinction pixel shader name")
                , StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }
        Core::Assets::AssetRef<Shader> avboitExtinctionPixelShaderRef;
        avboitExtinctionPixelShaderRef.virtualPath = avboitExtinctionPixelShaderName;
        outMaterial.setAvboitExtinctionPixelShader(avboitExtinctionPixelShaderRef);
    }
    outMaterial.setTransparent(materialEntry.transparent);
    outMaterial.setTwoSided(materialEntry.twoSided);
    outMaterial.setRefractive(materialEntry.refractive);
    outMaterial.setTypedLayout(
        materialEntry.typedLayoutHash,
        materialEntry.typedLayoutBlocks,
        materialEntry.typedLayoutFields,
        materialEntry.typedBlockBytes
    );

    for(const auto& [shaderType, shaderAsset] : materialEntry.stageShaders){
        if(!outMaterial.setShaderForStage(shaderType, shaderAsset)){
            const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: invalid shader stage '{}' for '{}'")
                , StringConvert(stageName.c_str())
                , StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }
    }

    return true;
}

bool AssignMaterialShadingModelIds(
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::AssignMaterialShadingModelIdsImpl(materialEntries, scratchArena);
}

bool EmitDeferredBxdfDispatchModule(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitDeferredBxdfDispatchModuleImpl(
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        outIncludeRoot,
        scratchArena
    );
}

bool EmitShadowTransmittanceDispatchModule(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialCookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitShadowTransmittanceDispatchModuleImpl(
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        outIncludeRoot,
        scratchArena
    );
}

bool EmitMaterialPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView sharedMeshShaderName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitMaterialPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        sharedMeshShaderName,
        materialEntries,
        outGenerated,
        scratchArena
    );
}

bool EmitMaterialAvboitAccumulatePixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitMaterialAvboitAccumulatePixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        outGenerated,
        scratchArena
    );
}

bool EmitMaterialAvboitOccupancyPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitMaterialAvboitOccupancyPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        outGenerated,
        scratchArena
    );
}

bool EmitMaterialAvboitExtinctionPixelShaders(
    MaterialCookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookVector<MaterialCookEntry>& materialEntries,
    MaterialCookVector<GeneratedMaterialPixelShader>& outGenerated,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::EmitMaterialAvboitExtinctionPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        materialEntries,
        outGenerated,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MaterialAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Material::s_AssetTypeText)
        );
        return false;
    }

    const Material& material = static_cast<const Material&>(asset);
    if(!material.virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: virtual path is empty"));
        return false;
    }
    if(material.stageShaderCount() == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material has no shader stages"));
        return false;
    }
    if(!material.materialInterface()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: material interface is required"));
        return false;
    }
    if(material.shaderVariant().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader variant is empty"));
        return false;
    }
    if(material.typedLayoutHash() == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: interface material is missing typed layout data"));
        return false;
    }
    if(material.typedLayoutBlocks().size() > Limit<u32>::s_Max || material.typedLayoutFields().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed layout count exceeds u32 range"));
        return false;
    }
    if(material.typedBlockBytes().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed block byte count exceeds u32 range"));
        return false;
    }
    if(MaterialBinaryPayload::ComputeMaterialTypedLayoutHash(
        material.typedLayoutBlocks(),
        material.typedLayoutFields()
    ) != material.typedLayoutHash()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed layout hash mismatch"));
        return false;
    }
    usize expectedTypedBlockByteSize = 0u;
    if(!MaterialBinaryPayload::ComputeMaterialTypedBlockByteSize(
        material.typedLayoutBlocks(),
        expectedTypedBlockByteSize
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed block bytes do not match typed layout"));
        return false;
    }
    if(expectedTypedBlockByteSize != material.typedBlockBytes().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: typed block bytes do not match typed layout"));
        return false;
    }
    usize reserveBytes = sizeof(u32); // magic
    bool canReserve = AddBinaryStringReserveBytes(reserveBytes, AStringView(material.shaderVariant()))
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u64))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(
            reserveBytes,
            material.typedLayoutBlocks().size(),
            MaterialBinaryPayload::s_TypedLayoutBlockBytes
        )
        && AddBinaryRepeatedReserveBytes(
            reserveBytes,
            material.typedLayoutFields().size(),
            MaterialBinaryPayload::s_TypedLayoutFieldBytes
        )
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryReserveBytes(reserveBytes, material.typedBlockBytes().size())
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32))
        && AddBinaryRepeatedReserveBytes(reserveBytes, material.stageShaderCount(), MaterialBinaryPayload::s_ShaderEntryBytes)
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // material flags
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // shading model id
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // shadow transmittance model id
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // AVBOIT accumulate pixel shader presence flag
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash)) // optional AVBOIT accumulate pixel shader name
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // AVBOIT occupancy pixel shader presence flag
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash)) // optional AVBOIT occupancy pixel shader name
        && AddBinaryReserveBytes(reserveBytes, sizeof(u32)) // AVBOIT extinction pixel shader presence flag
        && AddBinaryReserveBytes(reserveBytes, sizeof(NameHash)) // optional AVBOIT extinction pixel shader name
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    AppendPOD(outBinary, MaterialBinaryPayload::s_MaterialMagic);
    if(!AppendString(outBinary, AStringView(material.shaderVariant()))){
        NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader variant is too long"));
        return false;
    }
    AppendPOD(outBinary, material.materialInterface().hash());
    AppendPOD(outBinary, material.typedLayoutHash());
    AppendPOD(outBinary, static_cast<u32>(material.typedLayoutBlocks().size()));
    AppendPOD(outBinary, static_cast<u32>(material.typedLayoutFields().size()));
    for(const MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        MaterialBinaryPayload::MaterialTypedLayoutBlockBinary blockBinary;
        blockBinary.blockNameHash = block.blockName.hash();
        blockBinary.blockClass = static_cast<u32>(block.blockClass);
        blockBinary.fieldBegin = block.fieldBegin;
        blockBinary.fieldCount = block.fieldCount;
        blockBinary.byteSize = block.byteSize;
        AppendPOD(outBinary, blockBinary);
    }
    for(const MaterialTypedLayoutField& field : material.typedLayoutFields()){
        MaterialBinaryPayload::MaterialTypedLayoutFieldBinary fieldBinary;
        fieldBinary.fieldNameHash = field.fieldName.hash();
        fieldBinary.fieldType = static_cast<u32>(field.fieldType);
        fieldBinary.offset = field.offset;
        fieldBinary.defaultValue = field.defaultValue;
        AppendPOD(outBinary, fieldBinary);
    }
    AppendPOD(outBinary, static_cast<u32>(material.typedBlockBytes().size()));
    BinaryDetail::AppendBytesNoReserveUnchecked(
        outBinary,
        material.typedBlockBytes().data(),
        material.typedBlockBytes().size()
    );
    AppendPOD(outBinary, material.stageShaderCount());

    const Material::StageShaderArray& stageShaders = material.stageShaders();
    for(usize shaderIndex = 0; shaderIndex < stageShaders.size(); ++shaderIndex){
        const Core::Assets::AssetRef<Shader>& shaderAsset = stageShaders[shaderIndex];
        if(!shaderAsset.valid())
            continue;

        const Core::ShaderType::Enum shaderType = static_cast<Core::ShaderType::Enum>(shaderIndex);
        if(!Core::ShaderType::IsValid(shaderType)){
            NWB_LOGGER_ERROR(NWB_TEXT("MaterialAssetCodec::serialize failed: shader stage index {} is invalid"), shaderIndex);
            return false;
        }

        AppendPOD(outBinary, shaderType);
        AppendPOD(outBinary, shaderAsset.name().hash());
    }

    u32 materialFlags = 0u;
    if(material.transparent())
        materialFlags |= MaterialBinaryPayload::MaterialFlag::Transparent;
    if(material.twoSided())
        materialFlags |= MaterialBinaryPayload::MaterialFlag::TwoSided;
    if(material.refractive())
        materialFlags |= MaterialBinaryPayload::MaterialFlag::Refractive;
    AppendPOD(outBinary, materialFlags);
    AppendPOD(outBinary, material.shadingModelId());
    AppendPOD(outBinary, material.shadowTransmittanceModelId());

    // Optional per-material AVBOIT pass pixel shaders: accumulate, then occupancy, then extinction -- each a
    // presence flag + the shader name hash, mirroring a stage-shader entry. Present only for a surface-authored
    // transparent material; loadBinary reads them back in this order. All three carry the material's SAME
    // shader-decided surface.renderCoverage, so the renderer can bind a per-material PS for every AVBOIT pass.
    const auto appendOptionalAvboitPixelShader = [&outBinary](const Core::Assets::AssetRef<Shader>& shaderRef){
        if(shaderRef.valid()){
            AppendPOD(outBinary, static_cast<u32>(1u));
            AppendPOD(outBinary, shaderRef.name().hash());
        }
        else{
            AppendPOD(outBinary, static_cast<u32>(0u));
        }
    };
    appendOptionalAvboitPixelShader(material.avboitAccumulatePixelShader());
    appendOptionalAvboitPixelShader(material.avboitOccupancyPixelShader());
    appendOptionalAvboitPixelShader(material.avboitExtinctionPixelShader());

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

