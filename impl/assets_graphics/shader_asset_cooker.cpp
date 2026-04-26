// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_asset_cooker.h"

#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformable_gltf_importer.h"
#include "deformable_geometry_asset.h"
#include "geometry_asset.h"
#include "material_asset.h"
#include "shader_asset.h"
#include <impl/assets_graphics/shader_stage_names.h>

#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_cook.h>

#include <core/filesystem/filesystem.h>
#include <core/metascript/parser.h>
#include <core/alloc/core.h>
#include <core/alloc/scratch.h>
#include <core/assets/asset_paths.h>
#include <core/assets/asset_auto_registration.h>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_VolumeName = "graphics";
static constexpr AStringView s_CookerLogPrefix = "ShaderAssetCooker";
static constexpr u64 s_DefaultSegmentSize = 16ull * 1024ull * 1024ull;
static constexpr u64 s_DefaultMetadataSize = 512ull * 1024ull;


UniquePtr<Core::Assets::IAssetCooker> CreateShaderAssetCooker(Core::Alloc::CustomArena& arena){
    return MakeUnique<ShaderAssetCooker>(arena);
}
Core::Assets::AssetCookerAutoRegistrar s_ShaderAssetCookerAutoRegistrar(&CreateShaderAssetCooker);

static const Name& IncludeAssetTypeName(){
    static const Name s_Name("include");
    return s_Name;
}

static bool IsMeshShaderStage(const AStringView stageName){
    return stageName == "mesh";
}

static bool IsMaterialPixelShaderStage(const Name& stageName){
    static const Name s_StageName("ps");
    return stageName == s_StageName;
}

static bool IsMaterialMeshShaderStage(const Name& stageName){
    static const Name s_StageName("mesh");
    return stageName == s_StageName;
}

static bool IsSupportedRendererMaterialShaderStage(const Name& stageName){
    return IsMaterialPixelShaderStage(stageName) || IsMaterialMeshShaderStage(stageName);
}

static bool BuildMeshComputeShadowEntry(const Core::ShaderCook::ShaderEntry& sourceEntry, Core::ShaderCook::ShaderEntry& outEntry){
    outEntry = sourceEntry;
    if(!outEntry.archiveStage.assign(ShaderStageNames::MeshComputeArchiveStageText()))
        return false;
    if(!outEntry.stage.assign("cs"))
        return false;
    if(!outEntry.targetProfile.assign("cs"))
        return false;

    outEntry.implicitDefines.insert_or_assign(
        AString(ShaderStageNames::MeshComputeImplicitDefineText()),
        AString("1")
    );
    return true;
}


static AString NormalizeVariantName(const Core::ShaderCook::ShaderEntry& entry, const AStringView generatedVariantName){
    const AStringView normalizedGeneratedVariantName = generatedVariantName.empty()
        ? AStringView(Core::ShaderArchive::s_DefaultVariant)
        : generatedVariantName
    ;

    if(entry.defaultVariant.empty())
        return AString(normalizedGeneratedVariantName);

    if(normalizedGeneratedVariantName == AStringView(entry.defaultVariant))
        return AString(Core::ShaderArchive::s_DefaultVariant);

    return AString(normalizedGeneratedVariantName);
}


struct ResolvedCookPaths{
    Path repoRoot;
    Vector<Path> assetRoots;
    Path outputDirectory;
    Path cacheDirectory;
};
struct DiscoveredNwbFile{
    Path assetRoot;
    Path filePath;
    AString normalizedPathText;
    CompactString virtualRoot;
};
struct VariantCachePaths{
    Path bytecodePath;
    Path sourceChecksumPath;
};
struct GeometryEntry{
    Name virtualPath = NAME_NONE;
    u32 vertexStride = 0;
    bool use32BitIndices = false;
    Vector<u8> vertexData;
    Vector<u8> indexData;
};
namespace DeformableGeometrySourceKind{
    enum Enum : u8{
        Inline,
        Gltf,
    };
};
struct DeformableGeometrySource{
    DeformableGeometrySourceKind::Enum kind = DeformableGeometrySourceKind::Inline;
    Path sourcePath;
    u32 meshIndex = 0;
    u32 primitiveIndex = 0;
};
struct DeformableGeometryEntry{
    Name virtualPath = NAME_NONE;
    DeformableGeometrySource source;
    Vector<DeformableVertexRest> restVertices;
    Vector<u32> indices;
    Vector<SkinInfluence4> skin;
    Vector<SourceSample> sourceSamples;
    DeformableDisplacement displacement;
    Vector<DeformableMorph> morphs;
    bool use32BitIndices = true;
};
struct DeformableDisplacementTextureEntry{
    Name virtualPath = NAME_NONE;
    u32 width = 0;
    u32 height = 0;
    Vector<Float4U> texels;
};
struct MaterialEntry{
    Name virtualPath = NAME_NONE;
    AString shaderVariant = Core::ShaderArchive::s_DefaultVariant;
    HashMap<Name, Core::Assets::AssetRef<Shader>> stageShaders;
    HashMap<CompactString, CompactString> parameters;
};
struct PreparedShaderKey{
    Name shaderName = NAME_NONE;
    Name stageName = NAME_NONE;
};
struct PreparedShaderKeyHasher{
    usize operator()(const PreparedShaderKey& key)const{
        usize seed = Hasher<Name>{}(key.shaderName);
        Core::CoreDetail::HashCombine(seed, key.stageName);
        return seed;
    }
};
inline bool operator==(const PreparedShaderKey& lhs, const PreparedShaderKey& rhs){
    return lhs.shaderName == rhs.shaderName && lhs.stageName == rhs.stageName;
}
struct PreparedShaderEntry{
    Core::ShaderCook::ShaderEntry entry;
    Path sourcePath;
    Core::ShaderCook::CookVector<Path> includeDirectories;
    Core::ShaderCook::CookVector<Path> dependencies;
    u64 dependencyChecksum = 0;
    u64 variantCount = 0;

    explicit PreparedShaderEntry(Core::ShaderCook::CookArena& arena)
        : entry(arena)
        , includeDirectories(Core::ShaderCook::CookAllocator<Path>(arena))
        , dependencies(Core::ShaderCook::CookAllocator<Path>(arena))
    {}
};
using IncludeMetadataMap = Core::ShaderCook::CookMap<AString, Core::ShaderCook::IncludeEntry>;
using ShaderEntryVector = Core::ShaderCook::CookVector<Core::ShaderCook::ShaderEntry>;
using PreparedShaderVector = Vector<PreparedShaderEntry, Core::ShaderCook::CookAllocator<PreparedShaderEntry>>;
using VirtualPathHashSet = Core::ShaderCook::CookHashSet<NameHash>;

struct ParsedAssetMetadata{
    IncludeMetadataMap includeMetadata;
    ShaderEntryVector shaderEntries;
    Vector<GeometryEntry> geometryEntries;
    Vector<DeformableGeometryEntry> deformableGeometryEntries;
    Vector<DeformableDisplacementTextureEntry> deformableDisplacementTextureEntries;
    Vector<MaterialEntry> materialEntries;

    explicit ParsedAssetMetadata(Core::ShaderCook::CookArena& arena)
        : includeMetadata(Core::ShaderCook::CookAllocator<Pair<const AString, Core::ShaderCook::IncludeEntry>>(arena))
        , shaderEntries(Core::ShaderCook::CookAllocator<Core::ShaderCook::ShaderEntry>(arena))
    {}
};

struct PreparedShaderPlan{
    PreparedShaderVector preparedEntries;
    u64 plannedFileCount = 1; // shader archive index

    explicit PreparedShaderPlan(Core::ShaderCook::CookArena& arena)
        : preparedEntries(Core::ShaderCook::CookAllocator<PreparedShaderEntry>(arena))
    {}
};

template<typename EntryT, typename PathHashSetT, typename EntryVectorT>
static bool AppendUniquePropertyAssetEntry(EntryT& entry, PathHashSetT& seenPathHashes, EntryVectorT& outEntries){
    if(!entry.virtualPath)
        return true;

    const NameHash pathHash = entry.virtualPath.hash();
    if(!seenPathHashes.insert(pathHash).second){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: duplicate property asset virtual path '{}'"),
            StringConvert(entry.virtualPath.c_str())
        );
        return false;
    }

    outEntries.push_back(Move(entry));
    return true;
}

using StagedVolumePaths = StagedDirectoryPaths;

static StagedVolumePaths BuildStagedVolumePaths(const Path& outputDirectory, const AStringView volumeName, const AStringView configurationSafeName){
    const AString volumeSafeName = BuildSafeCacheName(CanonicalizeText(volumeName));
    AString stageToken;
    stageToken.reserve(volumeSafeName.size() + 1u + configurationSafeName.size() + 1u + 16u);
    stageToken += volumeSafeName;
    stageToken += '_';
    stageToken += configurationSafeName;
    stageToken += '_';
    AppendHexU64(ComputeFnv64Text(PathToString(outputDirectory)), stageToken);
    return BuildStagedDirectoryPaths(outputDirectory, stageToken);
}

static bool ResolveCookPaths(const ShaderCookEnvironment& environment, ResolvedCookPaths& outPaths){
    ErrorCode errorCode;

    outPaths = {};

    if(environment.assetRoots.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: no asset roots specified"));
        return false;
    }
    if(environment.outputDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: output directory is empty"));
        return false;
    }

    outPaths.repoRoot = environment.repoRoot.empty() ? Path(".") : environment.repoRoot;
    outPaths.repoRoot = AbsolutePath(outPaths.repoRoot, errorCode).lexically_normal();
    if(errorCode){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to resolve repo root: {}"),
            StringConvert(errorCode.message())
        );
        return false;
    }

    outPaths.assetRoots.reserve(environment.assetRoots.size());
    for(const Path& assetRoot : environment.assetRoots){
        Path resolvedAssetRoot;
        errorCode.clear();
        if(!ResolveAbsolutePath(outPaths.repoRoot, PathToString(assetRoot), resolvedAssetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: failed to resolve asset root '{}': {}"),
                    PathToString<tchar>(assetRoot),
                    StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: asset root is empty or invalid: '{}'"),
                    PathToString<tchar>(assetRoot)
                );
            }
            return false;
        }
        outPaths.assetRoots.push_back(Move(resolvedAssetRoot));
    }

    errorCode.clear();
    if(!ResolveAbsolutePath(outPaths.repoRoot, PathToString(environment.outputDirectory), outPaths.outputDirectory, errorCode)){
        if(errorCode){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to resolve output directory '{}': {}"),
                PathToString<tchar>(environment.outputDirectory),
                StringConvert(errorCode.message())
            );
        }
        else{
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: output directory is empty or invalid: '{}'"),
                PathToString<tchar>(environment.outputDirectory)
            );
        }
        return false;
    }

    const Path defaultCacheDirectory = outPaths.repoRoot / "__build_obj/shader_cache";
    const Path requestedCacheDirectory = environment.cacheDirectory.empty()
        ? defaultCacheDirectory
        : environment.cacheDirectory
    ;
    errorCode.clear();
    if(!ResolveAbsolutePath(outPaths.repoRoot, PathToString(requestedCacheDirectory), outPaths.cacheDirectory, errorCode)){
        if(errorCode){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to resolve cache directory '{}': {}"),
                PathToString<tchar>(requestedCacheDirectory),
                StringConvert(errorCode.message())
            );
        }
        else{
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: cache directory is empty or invalid: '{}'"),
                PathToString<tchar>(requestedCacheDirectory)
            );
        }
        return false;
    }

    if(!EnsureDirectories(outPaths.cacheDirectory, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to create cache directory '{}': {}"),
            PathToString<tchar>(outPaths.cacheDirectory),
            StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}


static bool DiscoverNwbFiles(const Vector<Path>& assetRoots, Vector<DiscoveredNwbFile>& outNwbFiles){
    Core::Alloc::ScratchArena<> scratchArena;
    ErrorCode errorCode;
    HashSet<AString, Hasher<AString>, EqualTo<AString>, Core::Alloc::ScratchAllocator<AString>> seenNwbPaths(
        0,
        Hasher<AString>(),
        EqualTo<AString>(),
        Core::Alloc::ScratchAllocator<AString>(scratchArena)
    );

    outNwbFiles.clear();

    for(const Path& assetRoot : assetRoots){
        CompactString virtualRoot;
        if(!Core::Assets::BuildAssetRootVirtualRoot(assetRoot, virtualRoot))
            return false;

        errorCode.clear();
        if(!IsDirectory(assetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: failed to query asset root '{}': {}"),
                    PathToString<tchar>(assetRoot),
                    StringConvert(errorCode.message())
                );
                return false;
            }

            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: asset root is not a directory: '{}'"),
                PathToString<tchar>(assetRoot)
            );
            return false;
        }

        for(const auto& dirEntry : RecursiveDirectoryIterator(assetRoot, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: error scanning asset root '{}': {}"),
                    PathToString<tchar>(assetRoot),
                    StringConvert(errorCode.message())
                );
                return false;
            }

            errorCode.clear();
            const bool isRegularFile = dirEntry.is_regular_file(errorCode);
            if(errorCode){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: failed to inspect '{}' while scanning '{}': {}"),
                    PathToString<tchar>(dirEntry.path()),
                    PathToString<tchar>(assetRoot),
                    StringConvert(errorCode.message())
                );
                return false;
            }
            if(!isRegularFile)
                continue;

            const Path& filePath = dirEntry.path();
            const AString extension = CanonicalizeText(PathToString(filePath.extension()));
            if(extension != Core::Assets::s_NwbExtension)
                continue;

            const AString normalizedPath = CanonicalizeText(PathToString(filePath.lexically_normal()));
            if(!seenNwbPaths.insert(normalizedPath).second)
                continue;

            outNwbFiles.push_back(DiscoveredNwbFile{
                assetRoot,
                filePath,
                Move(normalizedPath),
                virtualRoot
            });
        }
    }

    Sort(outNwbFiles.begin(), outNwbFiles.end(),
        [](const DiscoveredNwbFile& lhs, const DiscoveredNwbFile& rhs){
            return lhs.normalizedPathText < rhs.normalizedPathText;
        }
    );

    return true;
}

static bool ParseVariantField(
    Core::ShaderCook& shaderCook,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    const AStringView defaultValue,
    AString& outVariant
){
    outVariant = defaultValue;

    const auto* variantValue = asset.findField(fieldName);
    if(!variantValue)
        return true;

    AString rawVariant;
    if(variantValue->isList()){
        const auto& list = variantValue->asList();
        usize rawVariantSize = list.empty() ? 0u : list.size() - 1u;
        for(usize i = 0; i < list.size(); ++i){
            if(!list[i].isString()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Material meta '{}': field '{}' list elements must be strings"),
                    PathToString<tchar>(nwbFilePath),
                    StringConvert(fieldName)
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
        NWB_LOGGER_ERROR(
            NWB_TEXT("Material meta '{}': field '{}' must be a string or list of strings"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }

    const AStringView rawVariantView = TrimView(rawVariant);
    if(rawVariantView.empty()){
        outVariant = defaultValue;
        return true;
    }
    if(rawVariantView == Core::ShaderArchive::s_DefaultVariant){
        outVariant = Core::ShaderArchive::s_DefaultVariant;
        return true;
    }

    AString canonicalVariant;
    if(!shaderCook.canonicalizeVariantSignature(rawVariantView, canonicalVariant)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Material meta '{}': field '{}' has invalid variant signature '{}'"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName),
            StringConvert(rawVariantView)
        );
        return false;
    }

    outVariant = Move(canonicalVariant);
    return true;
}

static bool ParseMaterialStageShaders(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    HashMap<Name, Core::Assets::AssetRef<Shader>>& outStageShaders
){
    outStageShaders.clear();

    const auto* shadersValue = asset.findField("shaders");
    if(!shadersValue){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shaders is required"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(!shadersValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shaders must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    outStageShaders.reserve(shadersValue->asMap().size());

    for(const auto& [stageKey, shaderValue] : shadersValue->asMap()){
        const AStringView stageKeyText(stageKey.data(), stageKey.size());
        if(!shaderValue.isString()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material meta '{}': shader '{}' must be a string"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(stageKeyText)
            );
            return false;
        }

        const Name stageName = ToName(stageKeyText);
        const Name shaderName = ToName(shaderValue.asString());
        Core::Assets::AssetRef<Shader> shaderAsset;
        shaderAsset.virtualPath = shaderName;
        if(!stageName || !shaderAsset.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage entries must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }
        if(!IsSupportedRendererMaterialShaderStage(stageName)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material meta '{}': shader stage '{}' is not supported by the ECS renderer material contract; only 'mesh' and 'ps' are allowed"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(stageKeyText)
            );
            return false;
        }

        if(!outStageShaders.emplace(stageName, shaderAsset).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material meta '{}': duplicate shader stage '{}'"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(stageKeyText)
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
    HashMap<CompactString, CompactString>& outParameters
){
    outParameters.clear();

    const auto* parametersValue = asset.findField("parameters");
    if(!parametersValue)
        return true;
    if(!parametersValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameters must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    outParameters.reserve(parametersValue->asMap().size());

    for(const auto& [paramKey, paramValue] : parametersValue->asMap()){
        const AStringView paramKeyText(paramKey.data(), paramKey.size());
        if(!paramValue.isString()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material meta '{}': parameter '{}' must be a string"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(paramKeyText)
            );
            return false;
        }

        CompactString key;
        CompactString value;
        const AStringView paramValueText(paramValue.asString().data(), paramValue.asString().size());
        if(!key.assign(paramKeyText) || !value.assign(paramValueText)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material meta '{}': parameter '{}' exceeds CompactString capacity"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(paramKeyText)
            );
            return false;
        }
        if(!key){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter names must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }

        if(!outParameters.emplace(key, value).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material meta '{}': duplicate parameter '{}'"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(key.c_str())
            );
            return false;
        }
    }

    return true;
}

static bool ParseMaterialMeta(
    Core::ShaderCook& shaderCook,
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Document& doc,
    MaterialEntry& outEntry
){
    outEntry = {};

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': asset is not a map"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }

    if(!Core::Assets::RejectVirtualPathOverrideField(discoveredFile.filePath, asset, "Material"))
        return false;
    if(!Core::Assets::BuildDerivedAssetVirtualPath(discoveredFile.assetRoot, discoveredFile.virtualRoot, discoveredFile.filePath, outEntry.virtualPath))
        return false;

    if(!ParseVariantField(shaderCook, discoveredFile.filePath, asset, "shader_variant", Core::ShaderArchive::s_DefaultVariant, outEntry.shaderVariant))
        return false;
    if(!ParseMaterialStageShaders(discoveredFile.filePath, asset, outEntry.stageShaders))
        return false;
    if(!ParseMaterialParameters(discoveredFile.filePath, asset, outEntry.parameters))
        return false;

    return true;
}

static bool AccumulateFlattenedValueLeafCount(const Core::Metascript::Value& value, usize& inOutCount){
    if(value.isList()){
        for(const Core::Metascript::Value& child : value.asList()){
            if(!AccumulateFlattenedValueLeafCount(child, inOutCount))
                return false;
        }
        return true;
    }

    if(inOutCount == Limit<usize>::s_Max)
        return false;

    ++inOutCount;
    return true;
}

static bool CountFlattenedValueLeaves(const Core::Metascript::Value& value, usize& outCount){
    outCount = 0u;
    return AccumulateFlattenedValueLeafCount(value, outCount);
}

static bool ParseGeometryMeta(const DiscoveredNwbFile& discoveredFile, const Core::Metascript::Document& doc, GeometryEntry& outEntry){
    outEntry = {};

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': asset is not a map"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }

    if(!Core::Assets::RejectVirtualPathOverrideField(discoveredFile.filePath, asset, "Geometry"))
        return false;
    if(!Core::Assets::BuildDerivedAssetVirtualPath(discoveredFile.assetRoot, discoveredFile.virtualRoot, discoveredFile.filePath, outEntry.virtualPath))
        return false;

    if(asset.findField("primitive")){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry meta '{}': field 'primitive' is no longer supported; define vertex_stride, vertex_data, index_type, and index_data in metadata"),
            PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    const auto* vertexStrideValue = asset.findField("vertex_stride");
    if(!vertexStrideValue || !vertexStrideValue->isInteger()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex_stride must be an integer"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }
    const i64 vertexStrideValueInt = vertexStrideValue->asInteger();
    if(vertexStrideValueInt <= 0 || static_cast<u64>(vertexStrideValueInt) > static_cast<u64>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex_stride is out of range"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }
    if((vertexStrideValueInt % static_cast<i64>(sizeof(f32))) != 0){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry meta '{}': vertex_stride must be a multiple of {} because vertex_data is encoded as f32 scalars"),
            PathToString<tchar>(discoveredFile.filePath),
            sizeof(f32)
        );
        return false;
    }
    outEntry.vertexStride = static_cast<u32>(vertexStrideValueInt);

    const auto* indexTypeValue = asset.findField("index_type");
    if(!indexTypeValue || !indexTypeValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_type must be a string ('u16' or 'u32')"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }

    CompactString indexType;
    const AStringView indexTypeText(indexTypeValue->asString().data(), indexTypeValue->asString().size());
    if(!indexType.assign(indexTypeText)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry meta '{}': index_type exceeds CompactString capacity"),
            PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }
    if(indexType.view() == "u16")
        outEntry.use32BitIndices = false;
    else if(indexType.view() == "u32")
        outEntry.use32BitIndices = true;
    else{
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry meta '{}': unsupported index_type '{}'"),
            PathToString<tchar>(discoveredFile.filePath),
            StringConvert(indexType.view())
        );
        return false;
    }

    auto appendVertexScalars = [&](const auto& self, const Core::Metascript::Value& value) -> bool{
        if(value.isList()){
            for(const Core::Metascript::Value& child : value.asList()){
                if(!self(self, child))
                    return false;
            }
            return true;
        }

        if(!value.isNumeric()){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex_data must contain only numeric values"), PathToString<tchar>(discoveredFile.filePath));
            return false;
        }

        const f64 numericValue = value.toDouble();
        if(!IsFinite(numericValue)){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex_data must contain only finite numeric values"), PathToString<tchar>(discoveredFile.filePath));
            return false;
        }
        if(numericValue < static_cast<f64>(Limit<f32>::s_Min) || numericValue > static_cast<f64>(Limit<f32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex_data contains a value outside the f32 range"), PathToString<tchar>(discoveredFile.filePath));
            return false;
        }
        if(outEntry.vertexData.size() > Limit<usize>::s_Max - sizeof(f32)){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex_data byte size overflows"), PathToString<tchar>(discoveredFile.filePath));
            return false;
        }

        AppendPOD(outEntry.vertexData, static_cast<f32>(numericValue));
        return true;
    };

    auto appendIndices = [&](const auto& self, const Core::Metascript::Value& value) -> bool{
        if(value.isList()){
            for(const Core::Metascript::Value& child : value.asList()){
                if(!self(self, child))
                    return false;
            }
            return true;
        }

        if(!value.isNumeric()){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data must contain only integer values"), PathToString<tchar>(discoveredFile.filePath));
            return false;
        }

        const f64 numericValue = value.toDouble();
        if(!IsFinite(numericValue)){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data must contain only finite integers"), PathToString<tchar>(discoveredFile.filePath));
            return false;
        }
        if(numericValue < 0.0 || numericValue != Floor(numericValue)){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data values must be non-negative integers"), PathToString<tchar>(discoveredFile.filePath));
            return false;
        }

        if(outEntry.use32BitIndices){
            if(numericValue > static_cast<f64>(Limit<u32>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data contains a value that exceeds u32"), PathToString<tchar>(discoveredFile.filePath));
                return false;
            }
            if(outEntry.indexData.size() > Limit<usize>::s_Max - sizeof(u32)){
                NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data byte size overflows"), PathToString<tchar>(discoveredFile.filePath));
                return false;
            }
            AppendPOD(outEntry.indexData, static_cast<u32>(numericValue));
            return true;
        }

        if(numericValue > static_cast<f64>(Limit<u16>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data contains a value that exceeds u16"), PathToString<tchar>(discoveredFile.filePath));
            return false;
        }
        if(outEntry.indexData.size() > Limit<usize>::s_Max - sizeof(u16)){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data byte size overflows"), PathToString<tchar>(discoveredFile.filePath));
            return false;
        }
        AppendPOD(outEntry.indexData, static_cast<u16>(numericValue));
        return true;
    };

    const auto* vertexDataValue = asset.findField("vertex_data");
    if(!vertexDataValue || !vertexDataValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex_data must be a list"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }
    usize vertexScalarCount = 0u;
    if(!CountFlattenedValueLeaves(*vertexDataValue, vertexScalarCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex_data scalar count overflows"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }
    if(vertexScalarCount > Limit<usize>::s_Max / sizeof(f32)){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex_data byte size overflows"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }
    outEntry.vertexData.reserve(vertexScalarCount * sizeof(f32));
    if(!appendVertexScalars(appendVertexScalars, *vertexDataValue))
        return false;
    if(outEntry.vertexData.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': vertex_data must not be empty"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }
    if((outEntry.vertexData.size() % outEntry.vertexStride) != 0){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry meta '{}': vertex_data byte count {} is not a multiple of vertex_stride {}"),
            PathToString<tchar>(discoveredFile.filePath),
            outEntry.vertexData.size(),
            outEntry.vertexStride
        );
        return false;
    }

    const auto* indexDataValue = asset.findField("index_data");
    if(!indexDataValue || !indexDataValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data must be a list"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }
    usize indexScalarCount = 0u;
    if(!CountFlattenedValueLeaves(*indexDataValue, indexScalarCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data scalar count overflows"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }
    const usize indexElementSize = outEntry.use32BitIndices ? sizeof(u32) : sizeof(u16);
    if(indexScalarCount > Limit<usize>::s_Max / indexElementSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data byte size overflows"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }
    outEntry.indexData.reserve(indexScalarCount * indexElementSize);
    if(!appendIndices(appendIndices, *indexDataValue))
        return false;
    if(outEntry.indexData.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data must not be empty"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }

    return true;
}

static bool BuildGeometryAsset(const GeometryEntry& geometryEntry, Geometry& outGeometry){
    outGeometry = Geometry(geometryEntry.virtualPath);
    outGeometry.setVertexLayout(geometryEntry.vertexStride);
    outGeometry.setVertexData(geometryEntry.vertexData.data(), geometryEntry.vertexData.size());
    outGeometry.setIndexData(geometryEntry.indexData.data(), geometryEntry.indexData.size(), geometryEntry.use32BitIndices);
    return outGeometry.validatePayload();
}

static const Core::Metascript::Value* FindField(const Core::Metascript::Value& map, const AStringView fieldName){
    return map.findField(Core::Metascript::MStringView(fieldName.data(), fieldName.size()));
}

static bool ParseFiniteF32Value(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    f32& outValue
){
    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' must contain only numeric values"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(label)
        );
        return false;
    }

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' must contain only finite numeric values"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(label)
        );
        return false;
    }
    if(numericValue < static_cast<f64>(Limit<f32>::s_Min) || numericValue > static_cast<f64>(Limit<f32>::s_Max)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' contains a value outside the f32 range"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(label)
        );
        return false;
    }

    outValue = static_cast<f32>(numericValue);
    return true;
}

static bool ParseU32Value(const Path& nwbFilePath, const Core::Metascript::Value& value, const AStringView label, u32& outValue){
    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' must contain only integer values"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(label)
        );
        return false;
    }

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < 0.0 || numericValue != Floor(numericValue)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' contains a non-integer or negative value"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(label)
        );
        return false;
    }
    if(numericValue > static_cast<f64>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' contains a value that exceeds u32"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(label)
        );
        return false;
    }

    outValue = static_cast<u32>(numericValue);
    return true;
}

static bool ParseU16Value(const Path& nwbFilePath, const Core::Metascript::Value& value, const AStringView label, u16& outValue){
    u32 parsed = 0;
    if(!ParseU32Value(nwbFilePath, value, label, parsed))
        return false;
    if(parsed > Limit<u16>::s_Max){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' contains a value that exceeds u16"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(label)
        );
        return false;
    }

    outValue = static_cast<u16>(parsed);
    return true;
}

static const Core::Metascript::Value* FindRequiredListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName)
{
    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(field && field->isList())
        return field;

    NWB_LOGGER_ERROR(
        NWB_TEXT("Deformable geometry meta '{}': '{}' must be a list"),
        PathToString<tchar>(nwbFilePath),
        StringConvert(fieldName)
    );
    return nullptr;
}

template<usize ComponentCount>
static bool ParseF32Tuple(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    f32 (&outValues)[ComponentCount]
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' must be a {}-component list"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(label),
            ComponentCount
        );
        return false;
    }

    const auto& list = value.asList();
    for(usize i = 0; i < ComponentCount; ++i){
        const AString componentLabel = StringFormat("{}[{}]", label, i);
        if(!ParseFiniteF32Value(nwbFilePath, list[i], componentLabel, outValues[i]))
            return false;
    }
    return true;
}

template<usize ComponentCount>
static bool ParseU16Tuple(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    u16 (&outValues)[ComponentCount]
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' must be a {}-component integer list"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(label),
            ComponentCount
        );
        return false;
    }

    const auto& list = value.asList();
    for(usize i = 0; i < ComponentCount; ++i){
        const AString componentLabel = StringFormat("{}[{}]", label, i);
        if(!ParseU16Value(nwbFilePath, list[i], componentLabel, outValues[i]))
            return false;
    }
    return true;
}

template<typename ElementT, usize ComponentCount>
static bool ParseFloatListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    Vector<ElementT>& outValues
){
    outValues.clear();

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, asset, fieldName);
    if(!field)
        return false;

    const auto& list = field->asList();
    outValues.reserve(list.size());
    for(usize i = 0; i < list.size(); ++i){
        const AString label = StringFormat("{}[{}]", fieldName, i);
        f32 tuple[ComponentCount] = {};
        if(!ParseF32Tuple(nwbFilePath, list[i], label, tuple))
            return false;

        ElementT element;
        element.x = tuple[0];
        element.y = tuple[1];
        if constexpr(ComponentCount >= 3u)
            element.z = tuple[2];
        if constexpr(ComponentCount >= 4u)
            element.w = tuple[3];
        outValues.push_back(element);
    }

    if(outValues.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' must not be empty"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }

    return true;
}

static bool ParseU32ListField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName,
    Vector<u32>& outValues
){
    outValues.clear();

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, map, fieldName);
    if(!field)
        return false;

    const auto& list = field->asList();
    outValues.reserve(list.size());
    for(usize i = 0; i < list.size(); ++i){
        u32 value = 0;
        const AString label = StringFormat("{}[{}]", fieldName, i);
        if(!ParseU32Value(nwbFilePath, list[i], label, value))
            return false;
        outValues.push_back(value);
    }

    return true;
}

static bool IsExplicitEmptyOptionalField(const Core::Metascript::Value& value){
    return (value.isList() && value.asList().empty()) || (value.isMap() && value.asMap().empty());
}

static bool AppendU32Recursive(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    Vector<u32>& outValues
){
    if(value.isList()){
        const auto& list = value.asList();
        for(usize i = 0; i < list.size(); ++i){
            const AString childLabel = StringFormat("{}[{}]", label, i);
            if(!AppendU32Recursive(nwbFilePath, list[i], childLabel, outValues))
                return false;
        }
        return true;
    }

    u32 index = 0;
    if(!ParseU32Value(nwbFilePath, value, label, index))
        return false;
    outValues.push_back(index);
    return true;
}

static bool ParseDeformableIndexType(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    bool& outUse32BitIndices
){
    outUse32BitIndices = true;

    const Core::Metascript::Value* indexType = FindField(asset, "index_type");
    if(!indexType || !indexType->isString()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': 'index_type' must be 'u16' or 'u32'"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    const AStringView indexTypeText(indexType->asString().data(), indexType->asString().size());
    if(indexTypeText == "u16"){
        outUse32BitIndices = false;
        return true;
    }
    if(indexTypeText == "u32"){
        outUse32BitIndices = true;
        return true;
    }

    NWB_LOGGER_ERROR(
        NWB_TEXT("Deformable geometry meta '{}': unsupported index_type '{}'"),
        PathToString<tchar>(nwbFilePath),
        StringConvert(indexTypeText)
    );
    return false;
}

static bool ParseDeformableIndexField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const bool use32BitIndices,
    Vector<u32>& outIndices
){
    outIndices.clear();

    const Core::Metascript::Value* field = FindRequiredListField(nwbFilePath, asset, "indices");
    if(!field)
        return false;
    usize indexCount = 0u;
    if(!CountFlattenedValueLeaves(*field, indexCount)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': 'indices' scalar count overflows"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    outIndices.reserve(indexCount);
    if(!AppendU32Recursive(nwbFilePath, *field, "indices", outIndices))
        return false;
    if(outIndices.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': 'indices' must not be empty"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(!use32BitIndices){
        for(const u32 index : outIndices){
            if(index > Limit<u16>::s_Max){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Deformable geometry meta '{}': 'indices' contains a value that exceeds u16 index_type"),
                    PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
        }
    }
    return true;
}

static bool BuildDeformableRestVertices(
    const Path& nwbFilePath,
    const Vector<Float3U>& positions,
    const Vector<Float3U>& normals,
    const Vector<Float4U>& tangents,
    const Vector<Float2U>& uv0,
    const Vector<Float4U>& colors,
    Vector<DeformableVertexRest>& outVertices
){
    if(positions.size() != normals.size()
        || positions.size() != tangents.size()
        || positions.size() != uv0.size()
        || positions.size() != colors.size()
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': rest vertex stream counts must match"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outVertices.resize(positions.size());
    for(usize i = 0; i < positions.size(); ++i){
        outVertices[i].position = positions[i];
        outVertices[i].normal = normals[i];
        outVertices[i].tangent = tangents[i];
        outVertices[i].uv0 = uv0[i];
        outVertices[i].color0 = colors[i];
    }
    return true;
}

static void BuildDefaultDeformableColors(const usize vertexCount, Vector<Float4U>& outColors){
    outColors.resize(vertexCount);
    for(Float4U& color : outColors)
        color = Float4U(1.f, 1.f, 1.f, 1.f);
}

static bool ParseSkinInfluences(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const usize vertexCount,
    Vector<SkinInfluence4>& outSkin
){
    outSkin.clear();

    const Core::Metascript::Value* skin = FindField(asset, "skin");
    if(!skin)
        return true;
    if(!skin->isMap()){
        if(IsExplicitEmptyOptionalField(*skin))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': 'skin' must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(skin->asMap().empty())
        return true;

    const Core::Metascript::Value* joints = FindField(*skin, "joints0");
    const Core::Metascript::Value* weights = FindField(*skin, "weights0");
    if(!joints || !joints->isList() || !weights || !weights->isList()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': 'skin' requires 'joints0' and 'weights0' lists"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    const auto& jointList = joints->asList();
    const auto& weightList = weights->asList();
    if(jointList.size() != vertexCount || weightList.size() != vertexCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': skin streams must match vertex count"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outSkin.resize(vertexCount);
    for(usize i = 0; i < vertexCount; ++i){
        const AString jointLabel = StringFormat("skin.joints0[{}]", i);
        const AString weightLabel = StringFormat("skin.weights0[{}]", i);
        if(!ParseU16Tuple(nwbFilePath, jointList[i], jointLabel, outSkin[i].joint))
            return false;
        if(!ParseF32Tuple(nwbFilePath, weightList[i], weightLabel, outSkin[i].weight))
            return false;
    }

    return true;
}

static bool ParseSourceSamples(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const usize vertexCount,
    Vector<SourceSample>& outSourceSamples
){
    outSourceSamples.clear();

    const Core::Metascript::Value* sourceSamples = FindField(asset, "source_samples");
    if(!sourceSamples)
        return true;
    if(!sourceSamples->isMap()){
        if(IsExplicitEmptyOptionalField(*sourceSamples))
            return true;

        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': 'source_samples' must be a map"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(sourceSamples->asMap().empty())
        return true;

    Vector<u32> sourceTri;
    Vector<Float3U> bary;
    if(!ParseU32ListField(nwbFilePath, *sourceSamples, "source_tri", sourceTri))
        return false;
    if(!ParseFloatListField<Float3U, 3u>(nwbFilePath, *sourceSamples, "bary", bary))
        return false;
    if(sourceTri.size() != vertexCount || bary.size() != vertexCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': source samples must match vertex count"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outSourceSamples.resize(vertexCount);
    for(usize i = 0; i < vertexCount; ++i){
        outSourceSamples[i].sourceTri = sourceTri[i];
        outSourceSamples[i].bary[0] = bary[i].x;
        outSourceSamples[i].bary[1] = bary[i].y;
        outSourceSamples[i].bary[2] = bary[i].z;
    }
    return true;
}

static bool ParseRequiredStringField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName,
    AString& outText)
{
    outText.clear();

    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(!field || !field->isString()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' must be a string"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }

    const Core::Metascript::MStringView text = field->asString();
    outText = CanonicalizeText(AStringView(text.data(), text.size()));
    if(outText.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' must not be empty"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

static bool ParseRequiredRawStringField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName,
    AString& outText)
{
    outText.clear();

    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(!field || !field->isString()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' must be a string"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }

    const Core::Metascript::MStringView text = field->asString();
    outText.assign(text.data(), text.size());
    if(outText.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': '{}' must not be empty"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

static bool ParseOptionalFiniteF32Field(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName,
    f32& outValue)
{
    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(!field)
        return true;

    return ParseFiniteF32Value(nwbFilePath, *field, fieldName, outValue);
}

static bool ParseOptionalFloat2Field(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName,
    Float2U& outValue)
{
    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(!field)
        return true;

    f32 tuple[2] = {};
    if(!ParseF32Tuple(nwbFilePath, *field, fieldName, tuple))
        return false;

    outValue = Float2U(tuple[0], tuple[1]);
    return true;
}

static bool ParseOptionalU32Field(
    const Path& nwbFilePath,
    const Core::Metascript::Value& map,
    const AStringView fieldName,
    u32& inOutValue)
{
    const Core::Metascript::Value* field = FindField(map, fieldName);
    if(!field)
        return true;

    return ParseU32Value(nwbFilePath, *field, fieldName, inOutValue);
}

static bool ParseDisplacement(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    DeformableDisplacement& outDisplacement)
{
    outDisplacement = DeformableDisplacement{};

    const Core::Metascript::Value* displacement = FindField(asset, "displacement");
    if(!displacement)
        return true;
    if(!displacement->isMap()){
        if(IsExplicitEmptyOptionalField(*displacement))
            return true;

        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': 'displacement' must be a map"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(displacement->asMap().empty())
        return true;

    AString space;
    AString mode;
    AString field;
    if(!ParseRequiredStringField(nwbFilePath, *displacement, "space", space))
        return false;
    if(!ParseRequiredStringField(nwbFilePath, *displacement, "mode", mode))
        return false;
    if(!ParseRequiredStringField(nwbFilePath, *displacement, "field", field))
        return false;

    const Core::Metascript::Value* amplitude = FindField(*displacement, "amplitude");
    if(!amplitude || !ParseFiniteF32Value(nwbFilePath, *amplitude, "displacement.amplitude", outDisplacement.amplitude))
        return false;

    if(!ParseOptionalFiniteF32Field(nwbFilePath, *displacement, "bias", outDisplacement.bias)
        || !ParseOptionalFloat2Field(nwbFilePath, *displacement, "uv_scale", outDisplacement.uvScale)
        || !ParseOptionalFloat2Field(nwbFilePath, *displacement, "uv_offset", outDisplacement.uvOffset)
    )
        return false;

    if(space == "tangent" && mode == "scalar" && field == "uv_ramp"){
        outDisplacement.mode = DeformableDisplacementMode::ScalarUvRamp;
        return ValidDeformableDisplacementDescriptor(outDisplacement);
    }

    if(field != "texture"){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': displacement field must be 'uv_ramp' or 'texture'"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    AString texturePath;
    if(!ParseRequiredStringField(nwbFilePath, *displacement, "texture", texturePath))
        return false;

    outDisplacement.texture.virtualPath = ToName(texturePath);
    if(space == "tangent" && mode == "scalar")
        outDisplacement.mode = DeformableDisplacementMode::ScalarTexture;
    else if(space == "tangent" && mode == "vector")
        outDisplacement.mode = DeformableDisplacementMode::VectorTangentTexture;
    else if(space == "object" && mode == "vector")
        outDisplacement.mode = DeformableDisplacementMode::VectorObjectTexture;
    else{
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': unsupported displacement texture space='{}' mode='{}'"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(space),
            StringConvert(mode)
        );
        return false;
    }

    return ValidDeformableDisplacementDescriptor(outDisplacement);
}

static bool ParseMorphs(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Vector<DeformableMorph>& outMorphs
){
    outMorphs.clear();

    const Core::Metascript::Value* morphs = FindField(asset, "morphs");
    if(!morphs)
        return true;
    if(!morphs->isMap()){
        if(IsExplicitEmptyOptionalField(*morphs))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Deformable geometry meta '{}': 'morphs' must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    outMorphs.reserve(morphs->asMap().size());
    for(const auto& [morphName, morphValue] : morphs->asMap()){
        const AStringView morphNameView(morphName.data(), morphName.size());
        const Name morphNameId = ToName(morphNameView);
        if(!morphNameId){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable geometry meta '{}': morph names must not be empty"),
                PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
        if(!morphValue.isMap()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable geometry meta '{}': morph '{}' must be a map"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(morphNameView)
            );
            return false;
        }

        Vector<u32> vertexIds;
        Vector<Float3U> deltaPositions;
        Vector<Float3U> deltaNormals;
        Vector<Float4U> deltaTangents;
        if(!ParseU32ListField(nwbFilePath, morphValue, "vertex_ids", vertexIds))
            return false;
        if(!ParseFloatListField<Float3U, 3u>(nwbFilePath, morphValue, "delta_position", deltaPositions))
            return false;
        if(!ParseFloatListField<Float3U, 3u>(nwbFilePath, morphValue, "delta_normal", deltaNormals))
            return false;
        if(!FindField(morphValue, "delta_tangent")){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable geometry meta '{}': morph '{}' requires 'delta_tangent' list"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(morphNameView)
            );
            return false;
        }
        if(!ParseFloatListField<Float4U, 4u>(nwbFilePath, morphValue, "delta_tangent", deltaTangents))
            return false;
        if(vertexIds.empty()
            || vertexIds.size() != deltaPositions.size()
            || vertexIds.size() != deltaNormals.size()
            || vertexIds.size() != deltaTangents.size()
        ){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Deformable geometry meta '{}': morph '{}' stream counts must match and must not be empty"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(morphNameView)
            );
            return false;
        }

        DeformableMorph morph;
        morph.name = morphNameId;
        morph.deltas.resize(vertexIds.size());
        for(usize i = 0; i < vertexIds.size(); ++i){
            morph.deltas[i].vertexId = vertexIds[i];
            morph.deltas[i].deltaPosition = deltaPositions[i];
            morph.deltas[i].deltaNormal = deltaNormals[i];
            morph.deltas[i].deltaTangent = deltaTangents[i];
        }
        outMorphs.push_back(Move(morph));
    }

    return true;
}

static bool ResolveDeformableGeometrySourcePath(
    const DiscoveredNwbFile& discoveredFile,
    const Vector<Path>& assetRoots,
    const AStringView sourcePathText,
    Path& outSourcePath)
{
    outSourcePath.clear();

    if(Core::Assets::HasReservedAssetVirtualRoot(sourcePathText)){
        if(Core::Assets::ResolveVirtualAssetPath(assetRoots, sourcePathText, outSourcePath))
            return true;

        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': failed to resolve source virtual path '{}'"),
            PathToString<tchar>(discoveredFile.filePath),
            StringConvert(sourcePathText)
        );
        return false;
    }

    ErrorCode errorCode;
    if(!ResolveAbsolutePath(discoveredFile.filePath.parent_path(), sourcePathText, outSourcePath, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': failed to resolve source path '{}': {}"),
            PathToString<tchar>(discoveredFile.filePath),
            StringConvert(sourcePathText),
            StringConvert(errorCode ? errorCode.message() : AString("invalid path"))
        );
        return false;
    }
    return true;
}

static bool ParseDeformableGeometrySource(
    const DiscoveredNwbFile& discoveredFile,
    const Vector<Path>& assetRoots,
    const Core::Metascript::Value& asset,
    DeformableGeometrySource& outSource,
    bool& outHasSource)
{
    outSource = DeformableGeometrySource{};
    outHasSource = false;

    const Core::Metascript::Value* source = FindField(asset, "source");
    if(!source)
        return true;
    outHasSource = true;
    if(!source->isMap()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': 'source' must be a map"),
            PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    AString format;
    AString sourcePathText;
    if(!ParseRequiredStringField(discoveredFile.filePath, *source, "format", format)
        || !ParseRequiredRawStringField(discoveredFile.filePath, *source, "path", sourcePathText)
    )
        return false;
    if(format != "gltf" && format != "glb"){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': source format '{}' is not supported"),
            PathToString<tchar>(discoveredFile.filePath),
            StringConvert(format)
        );
        return false;
    }

    outSource.kind = DeformableGeometrySourceKind::Gltf;
    if(!ParseOptionalU32Field(discoveredFile.filePath, *source, "mesh", outSource.meshIndex)
        || !ParseOptionalU32Field(discoveredFile.filePath, *source, "primitive", outSource.primitiveIndex)
        || !ResolveDeformableGeometrySourcePath(discoveredFile, assetRoots, sourcePathText, outSource.sourcePath)
    )
        return false;

    return true;
}

static bool ParseDeformableGeometryMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Vector<Path>& assetRoots,
    const Core::Metascript::Document& doc,
    DeformableGeometryEntry& outEntry
){
    outEntry = {};

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable geometry meta '{}': asset is not a map"),
            PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    if(!Core::Assets::RejectVirtualPathOverrideField(discoveredFile.filePath, asset, "DeformableGeometry"))
        return false;
    if(!Core::Assets::BuildDerivedAssetVirtualPath(
        discoveredFile.assetRoot,
        discoveredFile.virtualRoot,
        discoveredFile.filePath,
        outEntry.virtualPath
    ))
        return false;

    bool hasSource = false;
    if(!ParseDeformableGeometrySource(discoveredFile, assetRoots, asset, outEntry.source, hasSource))
        return false;
    if(hasSource){
        if(!ParseDisplacement(discoveredFile.filePath, asset, outEntry.displacement))
            return false;
        return true;
    }

    Vector<Float3U> positions;
    Vector<Float3U> normals;
    Vector<Float4U> tangents;
    Vector<Float2U> uv0;
    Vector<Float4U> colors;
    if(!ParseDeformableIndexType(discoveredFile.filePath, asset, outEntry.use32BitIndices))
        return false;
    if(!ParseFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, "positions", positions))
        return false;
    if(!ParseFloatListField<Float3U, 3u>(discoveredFile.filePath, asset, "normals", normals))
        return false;
    if(!ParseFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, "tangents", tangents))
        return false;
    if(!ParseFloatListField<Float2U, 2u>(discoveredFile.filePath, asset, "uv0", uv0))
        return false;
    const Core::Metascript::Value* colorsField = FindField(asset, "colors");
    if(colorsField && !IsExplicitEmptyOptionalField(*colorsField)){
        if(!ParseFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, "colors", colors))
            return false;
    }else
        BuildDefaultDeformableColors(positions.size(), colors);
    if(!BuildDeformableRestVertices(discoveredFile.filePath, positions, normals, tangents, uv0, colors, outEntry.restVertices))
        return false;
    if(!ParseDeformableIndexField(discoveredFile.filePath, asset, outEntry.use32BitIndices, outEntry.indices))
        return false;
    if(!ParseSkinInfluences(discoveredFile.filePath, asset, outEntry.restVertices.size(), outEntry.skin))
        return false;
    if(!ParseSourceSamples(discoveredFile.filePath, asset, outEntry.restVertices.size(), outEntry.sourceSamples))
        return false;
    if(!ParseDisplacement(discoveredFile.filePath, asset, outEntry.displacement))
        return false;
    if(!ParseMorphs(discoveredFile.filePath, asset, outEntry.morphs))
        return false;

    return true;
}

static bool ParseDeformableDisplacementTextureMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Document& doc,
    DeformableDisplacementTextureEntry& outEntry
){
    outEntry = {};

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable displacement texture meta '{}': asset is not a map"),
            PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    if(!Core::Assets::RejectVirtualPathOverrideField(discoveredFile.filePath, asset, "DeformableDisplacementTexture"))
        return false;
    if(!Core::Assets::BuildDerivedAssetVirtualPath(
        discoveredFile.assetRoot,
        discoveredFile.virtualRoot,
        discoveredFile.filePath,
        outEntry.virtualPath
    ))
        return false;

    const Core::Metascript::Value* width = FindField(asset, "width");
    const Core::Metascript::Value* height = FindField(asset, "height");
    if(!width || !ParseU32Value(discoveredFile.filePath, *width, "width", outEntry.width))
        return false;
    if(!height || !ParseU32Value(discoveredFile.filePath, *height, "height", outEntry.height))
        return false;
    if(!ParseFloatListField<Float4U, 4u>(discoveredFile.filePath, asset, "texels", outEntry.texels))
        return false;
    if(outEntry.width == 0u || outEntry.height == 0u || outEntry.width > Limit<u32>::s_Max / outEntry.height){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable displacement texture meta '{}': dimensions are invalid"),
            PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }
    if(outEntry.texels.size() != static_cast<usize>(outEntry.width) * static_cast<usize>(outEntry.height)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Deformable displacement texture meta '{}': texel count must match width * height"),
            PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    return true;
}

static bool BuildDeformableGeometryAsset(DeformableGeometryEntry& geometryEntry, DeformableGeometry& outGeometry){
    if(geometryEntry.source.kind == DeformableGeometrySourceKind::Gltf){
        DeformableGltfImportOptions importOptions;
        importOptions.sourcePath = geometryEntry.source.sourcePath;
        importOptions.virtualPath = geometryEntry.virtualPath;
        importOptions.meshIndex = geometryEntry.source.meshIndex;
        importOptions.primitiveIndex = geometryEntry.source.primitiveIndex;
        if(!ImportDeformableGeometryFromGltf(importOptions, outGeometry))
            return false;
        if(geometryEntry.displacement.mode != DeformableDisplacementMode::None)
            outGeometry.setDisplacement(geometryEntry.displacement);
        return outGeometry.validatePayload();
    }

    outGeometry = DeformableGeometry(geometryEntry.virtualPath);

    outGeometry.setRestVertices(Move(geometryEntry.restVertices));
    outGeometry.setIndices(Move(geometryEntry.indices));
    outGeometry.setSkin(Move(geometryEntry.skin));
    outGeometry.setSourceSamples(Move(geometryEntry.sourceSamples));
    outGeometry.setDisplacement(geometryEntry.displacement);
    outGeometry.setMorphs(Move(geometryEntry.morphs));
    return outGeometry.validatePayload();
}

static bool BuildIncludeDirectories(const Path& repoRoot, const Vector<Path>& assetRoots, const Core::ShaderCook::ShaderEntry& entry, Core::ShaderCook::CookVector<Path>& outIncludeDirectories){
    Core::Alloc::ScratchArena<> scratchArena;
    ErrorCode errorCode;
    HashSet<AString, Hasher<AString>, EqualTo<AString>, Core::Alloc::ScratchAllocator<AString>> seenIncludeDirectories(
        0,
        Hasher<AString>(),
        EqualTo<AString>(),
        Core::Alloc::ScratchAllocator<AString>(scratchArena)
    );

    outIncludeDirectories.clear();
    outIncludeDirectories.reserve(entry.includeRoots.size());
    seenIncludeDirectories.reserve(entry.includeRoots.size());

    for(const AString& includeRoot : entry.includeRoots){
        Path includeDirectory;
        if(!Core::Assets::ResolveVirtualAssetPath(assetRoots, includeRoot, includeDirectory)){
            if(Core::Assets::HasReservedAssetVirtualRoot(includeRoot)){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: failed to resolve virtual include root '{}' for entry '{}'"),
                    StringConvert(includeRoot),
                    StringConvert(entry.name)
                );
                return false;
            }

            errorCode.clear();
            if(!ResolveAbsolutePath(repoRoot, includeRoot, includeDirectory, errorCode)){
                if(errorCode){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("ShaderAssetCooker: failed to resolve include root '{}' for entry '{}': {}"),
                        StringConvert(includeRoot),
                        StringConvert(entry.name),
                        StringConvert(errorCode.message())
                    );
                }
                else{
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("ShaderAssetCooker: include root '{}' is empty or invalid for entry '{}'"),
                        StringConvert(includeRoot),
                        StringConvert(entry.name)
                    );
                }
                return false;
            }
        }

        errorCode.clear();
        const bool isDirectory = IsDirectory(includeDirectory, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to query include root '{}' for entry '{}': {}"),
                PathToString<tchar>(includeDirectory),
                StringConvert(entry.name),
                StringConvert(errorCode.message())
            );
            return false;
        }
        if(!isDirectory){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: include root is not a directory for entry '{}': '{}'"),
                StringConvert(entry.name),
                PathToString<tchar>(includeDirectory)
            );
            return false;
        }

        AString normalizedIncludeDirectory = CanonicalizeText(PathToString(includeDirectory.lexically_normal()));
        if(!seenIncludeDirectories.insert(Move(normalizedIncludeDirectory)).second)
            continue;

        outIncludeDirectories.push_back(Move(includeDirectory));
    }

    return true;
}

static bool CountShaderVariants(const Core::ShaderCook::ShaderEntry& entry, u64& outVariantCount){
    outVariantCount = 1;

    for(const auto& [defineName, defineEntry] : entry.defineValues){
        const u64 valueCount = static_cast<u64>(defineEntry.values.size());
        if(valueCount == 0){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: entry '{}' has define '{}' with no values"),
                StringConvert(entry.name),
                StringConvert(defineName)
            );
            return false;
        }
        if(outVariantCount > Limit<u64>::s_Max / valueCount){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: variant count overflow for entry '{}'"),
                StringConvert(entry.name)
            );
            return false;
        }
        outVariantCount *= valueCount;
    }

    return true;
}

static u64 EstimateRequiredMetadataBytes(const u64 fileCount){
    if(fileCount == 0)
        return s_DefaultMetadataSize;

    u64 indexBytes = 0;
    if(fileCount > Limit<u64>::s_Max / static_cast<u64>(sizeof(Core::Filesystem::VolumeIndexEntryDisk)))
        return Limit<u64>::s_Max;
    indexBytes = fileCount * static_cast<u64>(sizeof(Core::Filesystem::VolumeIndexEntryDisk));

    u64 totalBytes = static_cast<u64>(sizeof(Core::Filesystem::VolumeHeaderDisk));
    if(totalBytes > Limit<u64>::s_Max - indexBytes)
        return Limit<u64>::s_Max;
    totalBytes += indexBytes;

    constexpr u64 s_MetadataPaddingBytes = 4ull * 1024ull;
    if(totalBytes <= Limit<u64>::s_Max - s_MetadataPaddingBytes)
        totalBytes += s_MetadataPaddingBytes;

    return Max(totalBytes, s_DefaultMetadataSize);
}

static bool ConfigureVolumeSizing(const u64 plannedFileCount, Core::Filesystem::VolumeBuildConfig& outConfig){
    outConfig.volumeName = s_VolumeName;
    outConfig.metadataSize = EstimateRequiredMetadataBytes(plannedFileCount);
    if(outConfig.metadataSize == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: metadata size overflow while planning volume"));
        return false;
    }

    outConfig.segmentSize = s_DefaultSegmentSize;
    while(outConfig.segmentSize <= outConfig.metadataSize){
        if(outConfig.segmentSize > Limit<u64>::s_Max / 2ull){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: segment size overflow while planning volume"));
            return false;
        }
        outConfig.segmentSize *= 2ull;
    }

    return true;
}

static bool NormalizeMaterialVariant(
    Core::ShaderCook& shaderCook,
    const MaterialEntry& materialEntry,
    const PreparedShaderEntry& preparedShaderEntry,
    const Name& stageName,
    AString& outNormalizedVariant
){
    outNormalizedVariant.clear();

    const AStringView requestedVariant = materialEntry.shaderVariant.empty()
        ? Core::ShaderArchive::s_DefaultVariant
        : AStringView(materialEntry.shaderVariant)
    ;

    if(requestedVariant == Core::ShaderArchive::s_DefaultVariant){
        if(!preparedShaderEntry.entry.defineValues.empty() && preparedShaderEntry.entry.defaultVariant.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material '{}' requests variant '{}' for shader '{}' stage '{}', but that shader has no default variant alias"),
                StringConvert(materialEntry.virtualPath.c_str()),
                StringConvert(requestedVariant),
                StringConvert(preparedShaderEntry.entry.name),
                StringConvert(stageName.c_str())
            );
            return false;
        }

        outNormalizedVariant = AString(Core::ShaderArchive::s_DefaultVariant);
        return true;
    }

    const AString contextLabel = StringFormat("{} [{}]", materialEntry.virtualPath.c_str(), stageName.c_str());
    if(!shaderCook.validateDefaultVariant(contextLabel, requestedVariant, preparedShaderEntry.entry.defineValues))
        return false;

    AString canonicalVariant;
    if(!shaderCook.canonicalizeVariantSignature(requestedVariant, canonicalVariant)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Material '{}' has invalid shader_variant '{}'"),
            StringConvert(materialEntry.virtualPath.c_str()),
            StringConvert(requestedVariant)
        );
        return false;
    }

    outNormalizedVariant = NormalizeVariantName(preparedShaderEntry.entry, canonicalVariant);
    return true;
}

static bool ValidateAndNormalizeMaterials(
    Core::ShaderCook& shaderCook,
    const Vector<PreparedShaderEntry, Core::ShaderCook::CookAllocator<PreparedShaderEntry>>& preparedEntries,
    Vector<MaterialEntry>& inOutMaterialEntries
){
    Core::Alloc::ScratchArena<> scratchArena;
    HashMap<
        PreparedShaderKey,
        const PreparedShaderEntry*,
        PreparedShaderKeyHasher,
        EqualTo<PreparedShaderKey>,
        Core::Alloc::ScratchAllocator<Pair<const PreparedShaderKey, const PreparedShaderEntry*>>
    > preparedShaderLookup(
        0,
        PreparedShaderKeyHasher(),
        EqualTo<PreparedShaderKey>(),
        Core::Alloc::ScratchAllocator<Pair<const PreparedShaderKey, const PreparedShaderEntry*>>(scratchArena)
    );
    preparedShaderLookup.reserve(preparedEntries.size());
    for(const PreparedShaderEntry& preparedEntry : preparedEntries){
        const PreparedShaderKey shaderKey{
            ToName(preparedEntry.entry.name),
            ToName(preparedEntry.entry.archiveStage.view())
        };
        if(!preparedShaderLookup.emplace(shaderKey, &preparedEntry).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: duplicate prepared shader key '{}' stage '{}'"),
                StringConvert(preparedEntry.entry.name),
                StringConvert(preparedEntry.entry.archiveStage.c_str())
            );
            return false;
        }
    }

    for(MaterialEntry& materialEntry : inOutMaterialEntries){
        AString normalizedVariant;
        bool hasNormalizedVariant = false;

        for(const auto& [stageName, shaderAsset] : materialEntry.stageShaders){
            const PreparedShaderKey shaderLookupKey{ shaderAsset.name(), stageName };
            const auto foundShader = preparedShaderLookup.find(shaderLookupKey);
            if(foundShader == preparedShaderLookup.end()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Material '{}' references unknown shader '{}' for stage '{}'"),
                    StringConvert(materialEntry.virtualPath.c_str()),
                    StringConvert(shaderAsset.name().c_str()),
                    StringConvert(stageName.c_str())
                );
                return false;
            }

            AString stageNormalizedVariant;
            if(!NormalizeMaterialVariant(shaderCook, materialEntry, *foundShader.value(), stageName, stageNormalizedVariant))
                return false;

            if(!hasNormalizedVariant){
                normalizedVariant = Move(stageNormalizedVariant);
                hasNormalizedVariant = true;
                continue;
            }

            if(normalizedVariant != stageNormalizedVariant){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Material '{}' resolves to different normalized variants across stages ('{}' vs '{}')"),
                    StringConvert(materialEntry.virtualPath.c_str()),
                    StringConvert(normalizedVariant),
                    StringConvert(stageNormalizedVariant)
                );
                return false;
            }
        }

        if(!hasNormalizedVariant)
            normalizedVariant = AString(Core::ShaderArchive::s_DefaultVariant);

        materialEntry.shaderVariant = Move(normalizedVariant);
    }

    return true;
}


static VariantCachePaths BuildVariantCachePaths(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView shaderSafeName,
    const AStringView stageSafeName,
    const AStringView variantName
){
    AString bytecodeFileName;
    bytecodeFileName.reserve(shaderSafeName.size() + 2u + stageSafeName.size() + 2u + 16u + 4u);
    bytecodeFileName += shaderSafeName;
    bytecodeFileName += "__";
    bytecodeFileName += stageSafeName;
    bytecodeFileName += "__";
    AppendHexU64(ComputeFnv64Text(variantName), bytecodeFileName);
    bytecodeFileName += ".spv";

    VariantCachePaths cachePaths;
    cachePaths.bytecodePath = cacheDirectory / configurationSafeName / bytecodeFileName;
    cachePaths.sourceChecksumPath = cachePaths.bytecodePath;
    cachePaths.sourceChecksumPath += ".source";
    return cachePaths;
}


static bool GetVariantBytecode(
    const Core::ShaderCook::ShaderEntry& entry,
    const AStringView variantName,
    const Core::ShaderCook::DefineCombo& defineCombo,
    const Core::ShaderCook::CookVector<Path>& includeDirectories,
    const Path& sourcePath,
    const VariantCachePaths& cachePaths,
    const AStringView sourceChecksumHex,
    Core::ShaderCook& shaderCook,
    Vector<u8>& outBytecode
)
{
    ErrorCode errorCode;
    Core::Alloc::ScratchArena<> scratchArena;

    outBytecode.clear();

    errorCode.clear();
    const bool hasCachedBytecode = FileExists(cachePaths.bytecodePath, errorCode);
    if(errorCode){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to query bytecode cache '{}' for entry '{}': {}"),
            PathToString<tchar>(cachePaths.bytecodePath),
            StringConvert(entry.name),
            StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    const bool hasCachedChecksum = FileExists(cachePaths.sourceChecksumPath, errorCode);
    if(errorCode){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to query checksum cache '{}' for entry '{}': {}"),
            PathToString<tchar>(cachePaths.sourceChecksumPath),
            StringConvert(entry.name),
            StringConvert(errorCode.message())
        );
        return false;
    }

    bool cacheUpToDate = false;
    if(hasCachedBytecode && hasCachedChecksum){
        AString cachedText;
        cacheUpToDate = ReadTextFile(cachePaths.sourceChecksumPath, cachedText)
            && TrimView(cachedText) == AStringView(sourceChecksumHex.data(), sourceChecksumHex.size())
        ;
    }
    if(cacheUpToDate){
        errorCode.clear();
        if(ReadBinaryFile(cachePaths.bytecodePath, outBytecode, errorCode) && !outBytecode.empty() && (outBytecode.size() & 3u) == 0u)
            return true;
        outBytecode.clear();
    }

    HashMap<
        AString,
        AString,
        Hasher<AString>,
        EqualTo<AString>,
        Core::Alloc::ScratchAllocator<Pair<const AString, AString>>
    > mergedDefines(
        0,
        Hasher<AString>(),
        EqualTo<AString>(),
        Core::Alloc::ScratchAllocator<Pair<const AString, AString>>(scratchArena)
    );
    if(defineCombo.size() > Limit<usize>::s_Max - entry.implicitDefines.size()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: define count overflow for entry '{}'"),
            StringConvert(entry.name)
        );
        return false;
    }

    const usize mergedDefineCapacity = defineCombo.size() + entry.implicitDefines.size();
    mergedDefines.reserve(mergedDefineCapacity);
    for(const auto& [defineName, value] : defineCombo)
        mergedDefines.insert_or_assign(defineName, value);
    for(const auto& [defineName, value] : entry.implicitDefines)
        mergedDefines.insert_or_assign(defineName, value);

    Vector<Core::ShaderMacroDefinition, Core::Alloc::ScratchAllocator<Core::ShaderMacroDefinition>> compileDefines{
        Core::Alloc::ScratchAllocator<Core::ShaderMacroDefinition>(scratchArena)
    };
    compileDefines.reserve(mergedDefines.size());
    for(const auto& [defineName, value] : mergedDefines)
        compileDefines.push_back(Core::ShaderMacroDefinition{ AStringView(defineName), AStringView(value) });
    if(compileDefines.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: entry '{}' has too many merged defines for shader compilation"),
            StringConvert(entry.name)
        );
        return false;
    }

    const Core::ShaderCompilerRequest compileRequest = {
        entry.name,
        entry.compiler.view(),
        entry.stage.view(),
        entry.targetProfile.view(),
        entry.entryPoint,
        variantName,
        compileDefines.data(),
        static_cast<u32>(compileDefines.size()),
        includeDirectories,
        sourcePath
    };
    if(!shaderCook.compileVariant(compileRequest, outBytecode))
        return false;

    errorCode.clear();
    if(!EnsureDirectories(cachePaths.bytecodePath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to create cache directory '{}': {}"),
            PathToString<tchar>(cachePaths.bytecodePath.parent_path()),
            StringConvert(errorCode.message())
        );
        return false;
    }

    if(!WriteBinaryFile(cachePaths.bytecodePath, outBytecode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Failed to write shader bytecode cache '{}' for entry '{}'"),
            PathToString<tchar>(cachePaths.bytecodePath),
            StringConvert(entry.name)
        );
        return false;
    }

    if(!WriteTextFile(cachePaths.sourceChecksumPath, sourceChecksumHex)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Failed to write cook source checksum '{}'"),
            PathToString<tchar>(cachePaths.sourceChecksumPath)
        );
        return false;
    }

    return true;
}

static bool AddPlannedFileCount(const u64 additionalFileCount, u64& inOutPlannedFileCount){
    if(inOutPlannedFileCount > Limit<u64>::s_Max - additionalFileCount){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: planned file count overflow"));
        return false;
    }

    inOutPlannedFileCount += additionalFileCount;
    return true;
}

static bool ReserveShaderIndexRecords(const u64 plannedFileCount, Vector<Core::ShaderArchive::Record>& outShaderIndexRecords){
    const u64 shaderRecordCount = plannedFileCount > 0 ? plannedFileCount - 1 : 0;
    if(shaderRecordCount > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: shader record count exceeds container capacity"));
        return false;
    }

    outShaderIndexRecords.reserve(static_cast<usize>(shaderRecordCount));
    return true;
}

static bool ParseAssetMetadata(
    Core::ShaderCook::CookArena& cookArena,
    Core::ShaderCook& shaderCook,
    const Vector<DiscoveredNwbFile>& nwbFiles,
    const Vector<Path>& assetRoots,
    ParsedAssetMetadata& outMetadata
){
    outMetadata.includeMetadata.reserve(nwbFiles.size());
    outMetadata.shaderEntries.reserve(nwbFiles.size());
    outMetadata.materialEntries.reserve(nwbFiles.size());
    outMetadata.geometryEntries.reserve(nwbFiles.size());
    outMetadata.deformableGeometryEntries.reserve(nwbFiles.size());

    HashSet<
        PreparedShaderKey,
        PreparedShaderKeyHasher,
        EqualTo<PreparedShaderKey>,
        Core::ShaderCook::CookAllocator<PreparedShaderKey>
    > seenShaderIdentityKeys{
        0,
        PreparedShaderKeyHasher(),
        EqualTo<PreparedShaderKey>(),
        Core::ShaderCook::CookAllocator<PreparedShaderKey>(cookArena)
    };
    Core::Alloc::ScratchArena<> scratchArena;
    HashSet<NameHash, Hasher<NameHash>, EqualTo<NameHash>, Core::Alloc::ScratchAllocator<NameHash>> seenPropertyAssetPathHashes(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        Core::Alloc::ScratchAllocator<NameHash>(scratchArena)
    );

    seenShaderIdentityKeys.reserve(nwbFiles.size());
    seenPropertyAssetPathHashes.reserve(nwbFiles.size());

    for(const DiscoveredNwbFile& discoveredNwbFile : nwbFiles){
        const Path& nwbFile = discoveredNwbFile.filePath;
        Core::Metascript::Document doc(cookArena);
        if(!shaderCook.parseDocument(nwbFile, doc))
            return false;

        const AStringView rawAssetTypeText(doc.assetType().data(), doc.assetType().size());
        const Name assetType = ToName(rawAssetTypeText);
        if(assetType == Shader::AssetTypeName()){
            Core::ShaderCook::ShaderEntry shaderEntry(cookArena);
            if(!shaderCook.parseShaderMeta(nwbFile, doc, shaderEntry))
                return false;

            if(!Core::Assets::BuildDerivedAssetVirtualPath(
                discoveredNwbFile.assetRoot,
                discoveredNwbFile.virtualRoot.view(),
                Path(shaderEntry.source),
                shaderEntry.name
            )){
                return false;
            }

            const PreparedShaderKey shaderIdentityKey{
                ToName(shaderEntry.name),
                ToName(shaderEntry.archiveStage.view())
            };
            if(!seenShaderIdentityKeys.insert(shaderIdentityKey).second){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: duplicate shader identity '{}' for stage '{}' from meta '{}'"),
                    StringConvert(shaderEntry.name),
                    StringConvert(shaderEntry.archiveStage.c_str()),
                    PathToString<tchar>(nwbFile)
                );
                return false;
            }

            if(!shaderEntry.name.empty())
                outMetadata.shaderEntries.push_back(Move(shaderEntry));
            continue;
        }

        if(assetType == IncludeAssetTypeName()){
            Core::ShaderCook::IncludeEntry includeEntry(cookArena);
            if(!shaderCook.parseIncludeMeta(nwbFile, doc, includeEntry))
                return false;

            if(!includeEntry.source.empty() && (!includeEntry.defineValues.empty() || !includeEntry.defaultVariant.empty())){
                ErrorCode errorCode;
                const Path absSource = AbsolutePath(Path(includeEntry.source), errorCode).lexically_normal();
                if(errorCode){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("ShaderAssetCooker: failed to resolve include metadata source '{}' from '{}': {}"),
                        StringConvert(includeEntry.source),
                        PathToString<tchar>(nwbFile),
                        StringConvert(errorCode.message())
                    );
                    return false;
                }

                AString key = CanonicalizeText(PathToString(absSource));
                if(!outMetadata.includeMetadata.emplace(Move(key), Move(includeEntry)).second){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("ShaderAssetCooker: duplicate include metadata for source '{}'"),
                        PathToString<tchar>(absSource)
                    );
                    return false;
                }
            }

            continue;
        }

        if(assetType == Material::AssetTypeName()){
            MaterialEntry materialEntry;
            if(!ParseMaterialMeta(shaderCook, discoveredNwbFile, doc, materialEntry))
                return false;

            if(!AppendUniquePropertyAssetEntry(materialEntry, seenPropertyAssetPathHashes, outMetadata.materialEntries))
                return false;
            continue;
        }

        if(assetType == Geometry::AssetTypeName()){
            GeometryEntry geometryEntry;
            if(!ParseGeometryMeta(discoveredNwbFile, doc, geometryEntry))
                return false;

            if(!AppendUniquePropertyAssetEntry(geometryEntry, seenPropertyAssetPathHashes, outMetadata.geometryEntries))
                return false;
            continue;
        }

        if(assetType == DeformableGeometry::AssetTypeName()){
            DeformableGeometryEntry geometryEntry;
            if(!ParseDeformableGeometryMeta(discoveredNwbFile, assetRoots, doc, geometryEntry))
                return false;

            if(!AppendUniquePropertyAssetEntry(geometryEntry, seenPropertyAssetPathHashes, outMetadata.deformableGeometryEntries))
                return false;
            continue;
        }

        if(assetType == DeformableDisplacementTexture::AssetTypeName()){
            DeformableDisplacementTextureEntry textureEntry;
            if(!ParseDeformableDisplacementTextureMeta(discoveredNwbFile, doc, textureEntry))
                return false;

            if(!AppendUniquePropertyAssetEntry(
                textureEntry,
                seenPropertyAssetPathHashes,
                outMetadata.deformableDisplacementTextureEntries
            ))
                return false;
            continue;
        }

        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: unsupported asset type '{}' in meta '{}'"),
            StringConvert(rawAssetTypeText),
            PathToString<tchar>(nwbFile)
        );
        return false;
    }

    if(outMetadata.shaderEntries.empty()){
        if(!outMetadata.materialEntries.empty() || !outMetadata.geometryEntries.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: material and static geometry assets require at least one shader entry")
            );
            return false;
        }
        if(!outMetadata.deformableGeometryEntries.empty())
            return true;
        if(!outMetadata.deformableDisplacementTextureEntries.empty())
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: no shader entries found in asset roots"));
        return false;
    }

    return true;
}

static bool PrepareShaderEntriesForCook(
    Core::ShaderCook::CookArena& cookArena,
    Core::ShaderCook& shaderCook,
    const ResolvedCookPaths& resolvedPaths,
    const IncludeMetadataMap& includeMetadata,
    ShaderEntryVector& inOutShaderEntries,
    PreparedShaderPlan& outPreparedPlan
)
{
    ErrorCode errorCode;

    outPreparedPlan.preparedEntries.clear();
    if(inOutShaderEntries.size() > Limit<usize>::s_Max / 2u){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: prepared shader entry reserve count overflows"));
        return false;
    }
    outPreparedPlan.preparedEntries.reserve(inOutShaderEntries.size() * 2u);
    outPreparedPlan.plannedFileCount = 1; // shader archive index

    for(Core::ShaderCook::ShaderEntry& entry : inOutShaderEntries){
        PreparedShaderEntry preparedEntry(cookArena);
        preparedEntry.entry = Move(entry);

        errorCode.clear();
        if(!ResolveAbsolutePath(resolvedPaths.repoRoot, preparedEntry.entry.source, preparedEntry.sourcePath, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Failed to resolve source path '{}' for entry '{}': {}"),
                    StringConvert(preparedEntry.entry.source),
                    StringConvert(preparedEntry.entry.name),
                    StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Failed to resolve source path '{}' for entry '{}': path is empty or invalid"),
                    StringConvert(preparedEntry.entry.source),
                    StringConvert(preparedEntry.entry.name)
                );
            }
            return false;
        }

        errorCode.clear();
        const bool sourceExists = FileExists(preparedEntry.sourcePath, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to query source path '{}' for entry '{}': {}"),
                PathToString<tchar>(preparedEntry.sourcePath),
                StringConvert(preparedEntry.entry.name),
                StringConvert(errorCode.message())
            );
            return false;
        }
        if(!sourceExists){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Shader source does not exist for entry '{}': '{}'"),
                StringConvert(preparedEntry.entry.name),
                PathToString<tchar>(preparedEntry.sourcePath)
            );
            return false;
        }

        errorCode.clear();
        const bool isRegularSourceFile = IsRegularFile(preparedEntry.sourcePath, errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to inspect source path '{}' for entry '{}': {}"),
                PathToString<tchar>(preparedEntry.sourcePath),
                StringConvert(preparedEntry.entry.name),
                StringConvert(errorCode.message())
            );
            return false;
        }
        if(!isRegularSourceFile){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Shader source is not a regular file for entry '{}': '{}'"),
                StringConvert(preparedEntry.entry.name),
                PathToString<tchar>(preparedEntry.sourcePath)
            );
            return false;
        }

        if(!BuildIncludeDirectories(resolvedPaths.repoRoot, resolvedPaths.assetRoots, preparedEntry.entry, preparedEntry.includeDirectories))
            return false;
        if(!shaderCook.gatherShaderDependencies(preparedEntry.sourcePath, preparedEntry.includeDirectories, preparedEntry.dependencies))
            return false;

        shaderCook.mergeInheritedDefines(preparedEntry.entry, preparedEntry.dependencies, includeMetadata);
        if(!shaderCook.validateDefaultVariant(preparedEntry.entry.name, preparedEntry.entry.defaultVariant, preparedEntry.entry.defineValues))
            return false;
        if(!shaderCook.canonicalizeVariantSignature(preparedEntry.entry.defaultVariant, preparedEntry.entry.defaultVariant)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to canonicalize merged default_variant for '{}'"),
                StringConvert(preparedEntry.entry.name)
            );
            return false;
        }
        if(!shaderCook.computeDependencyChecksum(preparedEntry.dependencies, preparedEntry.dependencyChecksum))
            return false;
        if(!CountShaderVariants(preparedEntry.entry, preparedEntry.variantCount))
            return false;
        if(!AddPlannedFileCount(preparedEntry.variantCount, outPreparedPlan.plannedFileCount))
            return false;

        const bool emitMeshComputeShadow = IsMeshShaderStage(preparedEntry.entry.archiveStage.view());
        outPreparedPlan.preparedEntries.push_back(Move(preparedEntry));

        if(!emitMeshComputeShadow)
            continue;

        PreparedShaderEntry meshComputeShadowEntry(cookArena);
        if(!BuildMeshComputeShadowEntry(outPreparedPlan.preparedEntries.back().entry, meshComputeShadowEntry.entry)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to build mesh-compute shadow entry for '{}'"),
                StringConvert(outPreparedPlan.preparedEntries.back().entry.name)
            );
            return false;
        }

        meshComputeShadowEntry.sourcePath = outPreparedPlan.preparedEntries.back().sourcePath;
        meshComputeShadowEntry.includeDirectories = outPreparedPlan.preparedEntries.back().includeDirectories;
        meshComputeShadowEntry.dependencies = outPreparedPlan.preparedEntries.back().dependencies;
        meshComputeShadowEntry.dependencyChecksum = outPreparedPlan.preparedEntries.back().dependencyChecksum;
        meshComputeShadowEntry.variantCount = outPreparedPlan.preparedEntries.back().variantCount;

        if(!AddPlannedFileCount(meshComputeShadowEntry.variantCount, outPreparedPlan.plannedFileCount))
            return false;

        outPreparedPlan.preparedEntries.push_back(Move(meshComputeShadowEntry));
    }

    return true;
}

static bool AppendPreparedShadersToVolume(
    Core::ShaderCook::CookArena& cookArena,
    Core::ShaderCook& shaderCook,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    PreparedShaderVector& preparedEntries,
    Core::Filesystem::VolumeSession& volumeSession,
    VirtualPathHashSet& inOutSeenVirtualPathHashes,
    Vector<Core::ShaderArchive::Record>& outShaderIndexRecords
){
    Vector<u8> cookedBytecode;

    for(PreparedShaderEntry& preparedEntry : preparedEntries){
        Core::ShaderCook::ShaderEntry& entry = preparedEntry.entry;
        const Name shaderName = ToName(entry.name);
        const Name stageName = ToName(entry.archiveStage.view());
        if(!shaderName || !stageName || entry.entryPoint.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Shader cook failed to canonicalize shader identity for '{}' stage '{}' entry point '{}'"),
                StringConvert(entry.name),
                StringConvert(entry.archiveStage.c_str()),
                StringConvert(entry.entryPoint)
            );
            return false;
        }

        const AString shaderSafeName = BuildSafeCacheName(entry.name);
        const AString stageSafeName = BuildSafeCacheName(entry.archiveStage.view());

        Core::ShaderCook::CookVector<Core::ShaderCook::DefineCombo> defineCombinations{Core::ShaderCook::CookAllocator<Core::ShaderCook::DefineCombo>(cookArena)};
        if(!shaderCook.expandDefineCombinations(entry.defineValues, defineCombinations)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: variant combination count exceeds runtime limits for entry '{}'"),
                StringConvert(entry.name)
            );
            return false;
        }
        if(defineCombinations.empty())
            defineCombinations.push_back(Core::ShaderCook::DefineCombo(Core::ShaderCook::CookAllocator<Pair<const AString, AString>>(cookArena)));

        for(const Core::ShaderCook::DefineCombo& defineCombo : defineCombinations){
            const AString generatedVariantName = shaderCook.buildVariantName(defineCombo);
            const AString variantName = NormalizeVariantName(entry, generatedVariantName);

            u64 sourceChecksum = 0;
            if(!shaderCook.computeSourceChecksum(entry, generatedVariantName, preparedEntry.dependencyChecksum, sourceChecksum))
                return false;
            const AString sourceChecksumHex = FormatHex64(sourceChecksum);

            const VariantCachePaths cachePaths = BuildVariantCachePaths(
                cacheDirectory,
                configurationSafeName,
                shaderSafeName,
                stageSafeName,
                variantName
            );
            if(!GetVariantBytecode(
                entry,
                variantName,
                defineCombo,
                preparedEntry.includeDirectories,
                preparedEntry.sourcePath,
                cachePaths,
                sourceChecksumHex,
                shaderCook,
                cookedBytecode
            )){
                return false;
            }

            const Name virtualPath = Core::ShaderArchive::buildVirtualPathName(shaderName, variantName, stageName);
            if(!virtualPath){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Shader cook failed to build virtual path for '{}' stage '{}' variant '{}'"),
                    StringConvert(entry.name),
                    StringConvert(entry.archiveStage.c_str()),
                    StringConvert(variantName)
                );
                return false;
            }

            const NameHash virtualPathHash = virtualPath.hash();
            if(!inOutSeenVirtualPathHashes.insert(virtualPathHash).second){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Shader cook produced duplicate virtual path '{}' (entry='{}', variant='{}')"),
                    StringConvert(virtualPath.c_str()),
                    StringConvert(entry.name),
                    StringConvert(variantName)
                );
                return false;
            }

            if(!volumeSession.pushDataDeferred(virtualPath, cookedBytecode)){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Failed to push shader bytecode '{}'"),
                    StringConvert(virtualPath.c_str())
                );
                return false;
            }

            Core::ShaderArchive::Record record;
            record.shaderName = shaderName;
            record.variantName = variantName;
            record.stage = stageName;
            record.sourceChecksum = sourceChecksum;
            record.bytecodeChecksum = ComputeFnv64Bytes(cookedBytecode.data(), cookedBytecode.size());
            record.virtualPathHash = virtualPathHash;
            outShaderIndexRecords.push_back(Move(record));
        }
    }

    return true;
}

static bool AppendMaterialAssetsToVolume(
    const Vector<MaterialEntry>& materialEntries,
    Core::Filesystem::VolumeSession& volumeSession,
    VirtualPathHashSet& inOutSeenVirtualPathHashes
){
    MaterialAssetCodec materialCodec;
    Vector<u8> materialBinary;

    for(const MaterialEntry& materialEntry : materialEntries){
        const NameHash materialVirtualPathHash = materialEntry.virtualPath.hash();
        if(!inOutSeenVirtualPathHashes.insert(materialVirtualPathHash).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: duplicate material virtual path '{}'"),
                StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }

        Material cookedMaterial(materialEntry.virtualPath);
        cookedMaterial.setShaderVariant(materialEntry.shaderVariant);
        for(const auto& [stageName, shaderAsset] : materialEntry.stageShaders)
            cookedMaterial.setShaderForStage(stageName, shaderAsset);
        for(const auto& [paramName, paramValue] : materialEntry.parameters){
            if(!cookedMaterial.setParameter(paramName, paramValue)){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: invalid material parameter '{}' for '{}'"),
                    StringConvert(paramName.c_str()),
                    StringConvert(materialEntry.virtualPath.c_str())
                );
                return false;
            }
        }

        if(!materialCodec.serialize(cookedMaterial, materialBinary)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to serialize material '{}'"),
                StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }

        if(!volumeSession.pushDataDeferred(materialEntry.virtualPath, materialBinary)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to push material '{}'"),
                StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }
    }

    return true;
}

static bool AppendGeometryAssetsToVolume(
    const Vector<GeometryEntry>& geometryEntries,
    Core::Filesystem::VolumeSession& volumeSession,
    VirtualPathHashSet& inOutSeenVirtualPathHashes
){
    GeometryAssetCodec geometryCodec;
    Vector<u8> geometryBinary;

    for(const GeometryEntry& geometryEntry : geometryEntries){
        const NameHash geometryVirtualPathHash = geometryEntry.virtualPath.hash();
        if(!inOutSeenVirtualPathHashes.insert(geometryVirtualPathHash).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: duplicate geometry virtual path '{}'"),
                StringConvert(geometryEntry.virtualPath.c_str())
            );
            return false;
        }

        Geometry cookedGeometry;
        if(!BuildGeometryAsset(geometryEntry, cookedGeometry)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to build geometry '{}'"),
                StringConvert(geometryEntry.virtualPath.c_str())
            );
            return false;
        }

        if(!geometryCodec.serialize(cookedGeometry, geometryBinary)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to serialize geometry '{}'"),
                StringConvert(geometryEntry.virtualPath.c_str())
            );
            return false;
        }

        if(!volumeSession.pushDataDeferred(geometryEntry.virtualPath, geometryBinary)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to push geometry '{}'"),
                StringConvert(geometryEntry.virtualPath.c_str())
            );
            return false;
        }
    }

    return true;
}

static bool AppendDeformableGeometryAssetsToVolume(
    Vector<DeformableGeometryEntry>& geometryEntries,
    Core::Filesystem::VolumeSession& volumeSession,
    VirtualPathHashSet& inOutSeenVirtualPathHashes
){
    DeformableGeometryAssetCodec geometryCodec;
    Vector<u8> geometryBinary;

    for(DeformableGeometryEntry& geometryEntry : geometryEntries){
        const NameHash geometryVirtualPathHash = geometryEntry.virtualPath.hash();
        if(!inOutSeenVirtualPathHashes.insert(geometryVirtualPathHash).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: duplicate deformable geometry virtual path '{}'"),
                StringConvert(geometryEntry.virtualPath.c_str())
            );
            return false;
        }

        DeformableGeometry cookedGeometry;
        if(!BuildDeformableGeometryAsset(geometryEntry, cookedGeometry)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to build deformable geometry '{}'"),
                StringConvert(geometryEntry.virtualPath.c_str())
            );
            return false;
        }

        if(!geometryCodec.serialize(cookedGeometry, geometryBinary)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to serialize deformable geometry '{}'"),
                StringConvert(geometryEntry.virtualPath.c_str())
            );
            return false;
        }

        if(!volumeSession.pushDataDeferred(geometryEntry.virtualPath, geometryBinary)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to push deformable geometry '{}'"),
                StringConvert(geometryEntry.virtualPath.c_str())
            );
            return false;
        }
    }

    return true;
}

static bool AppendDeformableDisplacementTexturesToVolume(
    Vector<DeformableDisplacementTextureEntry>& textureEntries,
    Core::Filesystem::VolumeSession& volumeSession,
    VirtualPathHashSet& inOutSeenVirtualPathHashes
){
    DeformableDisplacementTextureAssetCodec textureCodec;
    Vector<u8> textureBinary;

    for(DeformableDisplacementTextureEntry& textureEntry : textureEntries){
        const NameHash textureVirtualPathHash = textureEntry.virtualPath.hash();
        if(!inOutSeenVirtualPathHashes.insert(textureVirtualPathHash).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: duplicate deformable displacement texture virtual path '{}'"),
                StringConvert(textureEntry.virtualPath.c_str())
            );
            return false;
        }

        DeformableDisplacementTexture texture(textureEntry.virtualPath);
        texture.setSize(textureEntry.width, textureEntry.height);
        texture.setTexels(Move(textureEntry.texels));

        if(!textureCodec.serialize(texture, textureBinary)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to serialize deformable displacement texture '{}'"),
                StringConvert(textureEntry.virtualPath.c_str())
            );
            return false;
        }

        if(!volumeSession.pushDataDeferred(textureEntry.virtualPath, textureBinary)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: failed to push deformable displacement texture '{}'"),
                StringConvert(textureEntry.virtualPath.c_str())
            );
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderAssetCooker::cook(const Core::Assets::AssetCookOptions& options){
    ShaderCookEnvironment environment;
    environment.configuration = options.configuration;
    environment.repoRoot = options.repoRoot.empty() ? Path(".") : Path(options.repoRoot.c_str());
    environment.assetRoots.reserve(options.assetRoots.size());
    for(const AString& assetRoot : options.assetRoots)
        environment.assetRoots.push_back(Path(assetRoot.c_str()));
    environment.outputDirectory = Path(options.outputDirectory.c_str());
    environment.cacheDirectory = options.cacheDirectory.empty() ? Path() : Path(options.cacheDirectory.c_str());

    ShaderCookResult result;
    if(!cookShaderAssets(environment, result))
        return false;

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Graphics asset cook complete [{}] - volume='{}', files={}, segments={}, mount='{}'"),
        StringConvert(options.configuration.c_str()),
        StringConvert(result.volumeName.c_str()),
        result.fileCount,
        result.segmentCount,
        PathToString<tchar>(environment.outputDirectory)
    );

    return true;
}


bool ShaderAssetCooker::cookShaderAssets(const ShaderCookEnvironment& environment, ShaderCookResult& outResult){
    outResult = {};

    Core::ShaderCook shaderCook(m_arena);

    __hidden_assets::ResolvedCookPaths resolvedPaths;
    if(!__hidden_assets::ResolveCookPaths(environment, resolvedPaths))
        return false;

    // discover .nwb files from asset roots
    Vector<__hidden_assets::DiscoveredNwbFile> nwbFiles;
    if(!__hidden_assets::DiscoverNwbFiles(resolvedPaths.assetRoots, nwbFiles))
        return false;

    __hidden_assets::ParsedAssetMetadata parsedMetadata(m_arena);
    if(!__hidden_assets::ParseAssetMetadata(m_arena, shaderCook, nwbFiles, resolvedPaths.assetRoots, parsedMetadata))
        return false;

    AString normalizedConfiguration = CanonicalizeText(environment.configuration.view());
    if(normalizedConfiguration.empty())
        normalizedConfiguration = "default";
    const AString configurationSafeName = BuildSafeCacheName(normalizedConfiguration);

    __hidden_assets::PreparedShaderPlan preparedPlan(m_arena);
    if(!__hidden_assets::PrepareShaderEntriesForCook(
        m_arena,
        shaderCook,
        resolvedPaths,
        parsedMetadata.includeMetadata,
        parsedMetadata.shaderEntries,
        preparedPlan
    )){
        return false;
    }
    if(!__hidden_assets::AddPlannedFileCount(static_cast<u64>(parsedMetadata.materialEntries.size()), preparedPlan.plannedFileCount))
        return false;
    if(!__hidden_assets::AddPlannedFileCount(static_cast<u64>(parsedMetadata.geometryEntries.size()), preparedPlan.plannedFileCount))
        return false;
    if(!__hidden_assets::AddPlannedFileCount(
        static_cast<u64>(parsedMetadata.deformableGeometryEntries.size()),
        preparedPlan.plannedFileCount
    ))
        return false;
    if(!__hidden_assets::AddPlannedFileCount(
        static_cast<u64>(parsedMetadata.deformableDisplacementTextureEntries.size()),
        preparedPlan.plannedFileCount
    ))
        return false;

    if(!__hidden_assets::ValidateAndNormalizeMaterials(shaderCook, preparedPlan.preparedEntries, parsedMetadata.materialEntries))
        return false;

    Vector<Core::ShaderArchive::Record> shaderIndexRecords;
    if(!__hidden_assets::ReserveShaderIndexRecords(preparedPlan.plannedFileCount, shaderIndexRecords))
        return false;

    __hidden_assets::VirtualPathHashSet seenVirtualPathHashes{Core::ShaderCook::CookAllocator<NameHash>(m_arena)};
    if(preparedPlan.plannedFileCount <= static_cast<u64>(Limit<usize>::s_Max))
        seenVirtualPathHashes.reserve(static_cast<usize>(preparedPlan.plannedFileCount));
    const Name& shaderIndexVirtualPath = Core::ShaderArchive::IndexVirtualPathName();
    seenVirtualPathHashes.insert(shaderIndexVirtualPath.hash());

    Core::Filesystem::VolumeBuildConfig volumeConfig;
    if(!__hidden_assets::ConfigureVolumeSizing(preparedPlan.plannedFileCount, volumeConfig))
        return false;

    const __hidden_assets::StagedVolumePaths stagedVolumePaths = __hidden_assets::BuildStagedVolumePaths(
        resolvedPaths.outputDirectory,
        volumeConfig.volumeName,
        configurationSafeName
    );
    if(!Core::Filesystem::EnsureEmptyStagedDirectory(stagedVolumePaths.stageDirectory, __hidden_assets::s_CookerLogPrefix, "stage directory"))
        return false;
    Core::Filesystem::StagedDirectoryCleanupGuard stageDirectoryCleanup(stagedVolumePaths.stageDirectory, __hidden_assets::s_CookerLogPrefix);
    if(!Core::Filesystem::RemoveStagedDirectoryIfPresent(stagedVolumePaths.backupDirectory, __hidden_assets::s_CookerLogPrefix, "backup directory"))
        return false;

    u64 stagedFileCount = 0;
    usize stagedSegmentCount = 0;
    {
        Core::Filesystem::VolumeSession volumeSession(m_arena);
        if(!volumeSession.create(stagedVolumePaths.stageDirectory, volumeConfig)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: failed to create staged volume session"));
            return false;
        }

        if(!__hidden_assets::AppendPreparedShadersToVolume(
            m_arena,
            shaderCook,
            resolvedPaths.cacheDirectory,
            configurationSafeName,
            preparedPlan.preparedEntries,
            volumeSession,
            seenVirtualPathHashes,
            shaderIndexRecords
        )){
            return false;
        }

        Vector<u8> indexBinary;
        if(!Core::ShaderArchive::serializeIndex(shaderIndexRecords, indexBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: failed to serialize shader index"));
            return false;
        }
        if(!volumeSession.pushDataDeferred(shaderIndexVirtualPath, indexBinary)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: failed to push shader index"));
            return false;
        }

        if(!__hidden_assets::AppendMaterialAssetsToVolume(parsedMetadata.materialEntries, volumeSession, seenVirtualPathHashes))
            return false;
        if(!__hidden_assets::AppendGeometryAssetsToVolume(parsedMetadata.geometryEntries, volumeSession, seenVirtualPathHashes))
            return false;
        if(!__hidden_assets::AppendDeformableGeometryAssetsToVolume(
            parsedMetadata.deformableGeometryEntries,
            volumeSession,
            seenVirtualPathHashes
        ))
            return false;
        if(!__hidden_assets::AppendDeformableDisplacementTexturesToVolume(
            parsedMetadata.deformableDisplacementTextureEntries,
            volumeSession,
            seenVirtualPathHashes
        ))
            return false;

        if(!volumeSession.flush()){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: failed to flush staged volume metadata"));
            return false;
        }

        stagedFileCount = volumeSession.fileCount();
        stagedSegmentCount = volumeSession.segmentCount();
    }

    if(!Core::Filesystem::PublishStagedVolume(stagedVolumePaths, resolvedPaths.outputDirectory, volumeConfig.volumeName, stagedSegmentCount))
        return false;
    stageDirectoryCleanup.dismiss();

    if(!outResult.volumeName.assign(__hidden_assets::s_VolumeName)){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: volume name exceeds CompactString capacity"));
        return false;
    }
    outResult.fileCount = stagedFileCount;
    outResult.segmentCount = static_cast<u64>(stagedSegmentCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

