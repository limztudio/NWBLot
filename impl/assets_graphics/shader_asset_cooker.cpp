// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_asset_cooker.h"
#include "geometry_asset.h"
#include "material_asset.h"

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
static constexpr u64 s_DefaultSegmentSize = 16ull * 1024ull * 1024ull;
static constexpr u64 s_DefaultMetadataSize = 512ull * 1024ull;


UniquePtr<Core::Assets::IAssetCooker> CreateShaderAssetCooker(Core::Alloc::CustomArena& arena){
    return MakeUnique<ShaderAssetCooker>(arena);
}
Core::Assets::AssetCookerAutoRegistrar s_ShaderAssetCookerAutoRegistrar(&CreateShaderAssetCooker);

static const Name& ShaderAssetTypeName(){
    static const Name s_Name("shader");
    return s_Name;
}
static const Name& IncludeAssetTypeName(){
    static const Name s_Name("include");
    return s_Name;
}


static AString NormalizeVariantName(const Core::ShaderCook::ShaderEntry& entry, const AStringView generatedVariantName){
    AString canonicalGeneratedVariantName = CanonicalizeText(generatedVariantName);
    if(canonicalGeneratedVariantName.empty())
        canonicalGeneratedVariantName = Core::ShaderArchive::s_DefaultVariant;

    if(entry.defaultVariant.empty())
        return canonicalGeneratedVariantName;

    const AString canonicalDefaultVariantName = CanonicalizeText(entry.defaultVariant);
    if(canonicalGeneratedVariantName == canonicalDefaultVariantName)
        return AString(Core::ShaderArchive::s_DefaultVariant);

    return canonicalGeneratedVariantName;
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
struct MaterialEntry{
    Name virtualPath = NAME_NONE;
    CompactString shaderVariant = CompactString(Core::ShaderArchive::s_DefaultVariant);
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
        const usize stageSeed = Hasher<Name>{}(key.stageName);
        seed ^= stageSeed + static_cast<usize>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);
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
using StagedVolumePaths = StagedDirectoryPaths;

static void RemoveDirectoryIfPresentBestEffort(const Path& directoryPath, const AStringView label);

struct StageDirectoryCleanupGuard{
    Path directoryPath;
    bool active = true;

    ~StageDirectoryCleanupGuard(){
        if(active)
            RemoveDirectoryIfPresentBestEffort(directoryPath, "stage directory");
    }
};

static StagedVolumePaths BuildStagedVolumePaths(const Path& outputDirectory, const AStringView volumeName, const AStringView configurationSafeName){
    const AString outputHashHex = FormatHex64(ComputeFnv64Text(PathToString(outputDirectory)));
    const AString stageToken = StringFormat(
        "{}_{}_{}",
        BuildSafeCacheName(CanonicalizeText(volumeName)),
        configurationSafeName,
        outputHashHex
    );
    return BuildStagedDirectoryPaths(outputDirectory, stageToken);
}

static bool RemoveDirectoryIfPresent(const Path& directoryPath, const AStringView label){
    ErrorCode errorCode;

    if(!RemoveAllIfExists(directoryPath, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to remove {} '{}': {}"),
            StringConvert(label),
            PathToString<tchar>(directoryPath),
            StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}

static void RemoveDirectoryIfPresentBestEffort(const Path& directoryPath, const AStringView label){
    ErrorCode errorCode;

    if(!RemoveAllIfExists(directoryPath, errorCode) && errorCode){
        NWB_LOGGER_WARNING(
            NWB_TEXT("ShaderAssetCooker: failed to remove {} '{}': {}"),
            StringConvert(label),
            PathToString<tchar>(directoryPath),
            StringConvert(errorCode.message())
        );
    }
}

static bool EnsureEmptyDirectory(const Path& directoryPath, const AStringView label){
    ErrorCode errorCode;

    if(!::EnsureEmptyDirectory(directoryPath, errorCode)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: failed to create {} '{}': {}"),
            StringConvert(label),
            PathToString<tchar>(directoryPath),
            StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
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
        : environment.cacheDirectory;
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

    if(!CreateDirectories(outPaths.cacheDirectory, errorCode)){
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

static bool RejectVirtualPathOverrideField(const Path& nwbFilePath, const Core::Metascript::Value& asset, const AStringView assetLabel){
    if(!asset.findField("name"))
        return true;

    NWB_LOGGER_ERROR(
        NWB_TEXT("{} meta '{}': field 'name' is no longer supported; virtual paths are derived from the asset file hierarchy"),
        StringConvert(assetLabel),
        PathToString<tchar>(nwbFilePath)
    );
    return false;
}

static bool ParseVariantField(
    Core::ShaderCook& shaderCook,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const AStringView fieldName,
    const AStringView defaultValue,
    CompactString& outVariant
){
    if(!outVariant.assign(defaultValue)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Material meta '{}': default value for '{}' exceeds CompactString capacity"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }

    const auto* variantValue = asset.findField(fieldName);
    if(!variantValue)
        return true;

    AString rawVariant;
    if(variantValue->isList()){
        const auto& list = variantValue->asList();
        for(usize i = 0; i < list.size(); ++i){
            if(!list[i].isString()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Material meta '{}': field '{}' list elements must be strings"),
                    PathToString<tchar>(nwbFilePath),
                    StringConvert(fieldName)
                );
                return false;
            }
            if(i > 0)
                rawVariant += ';';
            rawVariant += list[i].copyString();
        }
    }
    else if(variantValue->isString()){
        rawVariant = variantValue->copyString();
    }
    else{
        NWB_LOGGER_ERROR(
            NWB_TEXT("Material meta '{}': field '{}' must be a string or list of strings"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }

    rawVariant = Trim(rawVariant);
    if(rawVariant.empty()){
        return outVariant.assign(defaultValue);
    }
    if(rawVariant == Core::ShaderArchive::s_DefaultVariant){
        return outVariant.assign(Core::ShaderArchive::s_DefaultVariant);
    }

    AString canonicalVariant;
    if(!shaderCook.canonicalizeVariantSignature(rawVariant, canonicalVariant)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Material meta '{}': field '{}' has invalid variant signature '{}'"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName),
            StringConvert(rawVariant)
        );
        return false;
    }

    if(!outVariant.assign(canonicalVariant)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Material meta '{}': field '{}' exceeds CompactString capacity"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(fieldName)
        );
        return false;
    }

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

    for(const auto& [stageKey, shaderValue] : shadersValue->asMap()){
        if(!shaderValue.isString()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material meta '{}': shader '{}' must be a string"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(AString(stageKey.data(), stageKey.size()))
            );
            return false;
        }

        const AString stageNameText = CanonicalizeText(AString(stageKey.data(), stageKey.size()));
        const AString shaderNameText = CanonicalizeText(shaderValue.copyString());
        const Name stageName = ToName(stageNameText);
        const Name shaderName = ToName(shaderNameText);
        const Core::Assets::AssetRef<Shader> shaderAsset(shaderName);
        if(!stageName || !shaderAsset.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': shader stage entries must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }

        if(!outStageShaders.insert({ stageName, shaderAsset }).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material meta '{}': duplicate shader stage '{}'"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(stageNameText)
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

    for(const auto& [paramKey, paramValue] : parametersValue->asMap()){
        if(!paramValue.isString()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material meta '{}': parameter '{}' must be a string"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(AString(paramKey.data(), paramKey.size()))
            );
            return false;
        }

        CompactString key;
        CompactString value;
        if(!key.assign(AStringView(paramKey.data(), paramKey.size())) || !value.assign(paramValue.copyString())){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material meta '{}': parameter '{}' exceeds CompactString capacity"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(AString(paramKey.data(), paramKey.size()))
            );
            return false;
        }
        if(!key){
            NWB_LOGGER_ERROR(NWB_TEXT("Material meta '{}': parameter names must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }

        if(!outParameters.insert({ key, value }).second){
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

    if(!RejectVirtualPathOverrideField(discoveredFile.filePath, asset, "Material"))
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

static bool ParseGeometryMeta(const DiscoveredNwbFile& discoveredFile, const Core::Metascript::Document& doc, GeometryEntry& outEntry){
    outEntry = {};

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': asset is not a map"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }

    if(!RejectVirtualPathOverrideField(discoveredFile.filePath, asset, "Geometry"))
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

    const AString indexType = CanonicalizeText(indexTypeValue->copyString());
    if(indexType == "u16")
        outEntry.use32BitIndices = false;
    else if(indexType == "u32")
        outEntry.use32BitIndices = true;
    else{
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry meta '{}': unsupported index_type '{}'"),
            PathToString<tchar>(discoveredFile.filePath),
            StringConvert(indexType)
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
            AppendPOD(outEntry.indexData, static_cast<u32>(numericValue));
            return true;
        }

        if(numericValue > static_cast<f64>(Limit<u16>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data contains a value that exceeds u16"), PathToString<tchar>(discoveredFile.filePath));
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
    if(!appendIndices(appendIndices, *indexDataValue))
        return false;
    if(outEntry.indexData.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry meta '{}': index_data must not be empty"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }

    return true;
}

static bool BuildGeometryAsset(const GeometryEntry& geometryEntry, Geometry& outGeometry){
    if(geometryEntry.vertexStride == 0 || geometryEntry.vertexData.empty() || geometryEntry.indexData.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: geometry '{}' has incomplete payload"),
            StringConvert(geometryEntry.virtualPath.c_str())
        );
        return false;
    }
    if((geometryEntry.vertexData.size() % geometryEntry.vertexStride) != 0){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: geometry '{}' vertex payload size {} is not aligned to stride {}"),
            StringConvert(geometryEntry.virtualPath.c_str()),
            geometryEntry.vertexData.size(),
            geometryEntry.vertexStride
        );
        return false;
    }

    const usize indexElementSize = geometryEntry.use32BitIndices ? sizeof(u32) : sizeof(u16);
    if((geometryEntry.indexData.size() % indexElementSize) != 0){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ShaderAssetCooker: geometry '{}' index payload size {} is not aligned to {}-byte indices"),
            StringConvert(geometryEntry.virtualPath.c_str()),
            geometryEntry.indexData.size(),
            indexElementSize
        );
        return false;
    }

    outGeometry = Geometry(geometryEntry.virtualPath);
    outGeometry.setVertexLayout(geometryEntry.vertexStride);
    outGeometry.setVertexData(geometryEntry.vertexData.data(), geometryEntry.vertexData.size());
    outGeometry.setIndexData(geometryEntry.indexData.data(), geometryEntry.indexData.size(), geometryEntry.use32BitIndices);
    return true;
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

        const AString normalizedIncludeDirectory = CanonicalizeText(PathToString(includeDirectory.lexically_normal()));
        if(!seenIncludeDirectories.insert(normalizedIncludeDirectory).second)
            continue;

        outIncludeDirectories.push_back(Move(includeDirectory));
    }

    return true;
}

static bool CountShaderVariants(const Core::ShaderCook::ShaderEntry& entry, u64& outVariantCount){
    outVariantCount = 1;

    for(const auto& [_, defineEntry] : entry.defineValues){
        const u64 valueCount = static_cast<u64>(defineEntry.values.size());
        if(valueCount == 0){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: entry '{}' has define '{}' with no values"),
                StringConvert(entry.name),
                StringConvert(defineEntry.name)
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

    const AString contextLabel = StringFormat("{} [{}]", materialEntry.virtualPath.c_str(), stageName.c_str());
    const AStringView requestedVariant = materialEntry.shaderVariant.empty()
        ? Core::ShaderArchive::s_DefaultVariant
        : materialEntry.shaderVariant.view();

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
            ToName(preparedEntry.entry.stage)
        };
        if(!preparedShaderLookup.insert({ shaderKey, &preparedEntry }).second){
            NWB_LOGGER_ERROR(
                NWB_TEXT("ShaderAssetCooker: duplicate prepared shader key '{}' stage '{}'"),
                StringConvert(preparedEntry.entry.name),
                StringConvert(preparedEntry.entry.stage)
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

        if(!materialEntry.shaderVariant.assign(normalizedVariant)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material '{}' normalized shader variant exceeds CompactString capacity"),
                StringConvert(materialEntry.virtualPath.c_str())
            );
            return false;
        }
    }

    return true;
}


static VariantCachePaths BuildVariantCachePaths(const Path& cacheDirectory, const AStringView configurationSafeName, const Core::ShaderCook::ShaderEntry& entry, const AStringView variantName){
    const AString shaderSafeName = BuildSafeCacheName(entry.name);
    const AString stageSafeName = BuildSafeCacheName(entry.stage);
    const AString variantHashHex = FormatHex64(ComputeFnv64Text(variantName));

    VariantCachePaths cachePaths;
    cachePaths.bytecodePath = cacheDirectory / configurationSafeName / StringFormat(
        "{}__{}__{}.spv",
        shaderSafeName,
        stageSafeName,
        variantHashHex
    );
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
        cacheUpToDate = ReadTextFile(cachePaths.sourceChecksumPath, cachedText) && (Trim(cachedText) == sourceChecksumHex);
    }
    if(cacheUpToDate){
        errorCode.clear();
        if(ReadBinaryFile(cachePaths.bytecodePath, outBytecode, errorCode) && !outBytecode.empty() && (outBytecode.size() & 3u) == 0u)
            return true;
        outBytecode.clear();
    }

    const Core::ShaderCompilerRequest compileRequest = {
        entry.name,
        entry.compiler,
        entry.stage,
        entry.targetProfile,
        entry.entryPoint,
        variantName,
        defineCombo,
        includeDirectories,
        sourcePath
    };
    if(!shaderCook.compileVariant(compileRequest, outBytecode))
        return false;

    errorCode.clear();
    if(!CreateDirectories(cachePaths.bytecodePath.parent_path(), errorCode)){
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
    ParsedAssetMetadata& outMetadata
){
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
        if(assetType == ShaderAssetTypeName()){
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
                ToName(shaderEntry.stage)
            };
            if(!seenShaderIdentityKeys.insert(shaderIdentityKey).second){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("ShaderAssetCooker: duplicate shader identity '{}' for stage '{}' from meta '{}'"),
                    StringConvert(shaderEntry.name),
                    StringConvert(shaderEntry.stage),
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

                const AString key = CanonicalizeText(PathToString(absSource));
                if(outMetadata.includeMetadata.find(key) != outMetadata.includeMetadata.end()){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("ShaderAssetCooker: duplicate include metadata for source '{}'"),
                        PathToString<tchar>(absSource)
                    );
                    return false;
                }
                outMetadata.includeMetadata.insert_or_assign(key, Move(includeEntry));
            }

            continue;
        }

        if(assetType == Material::AssetTypeName()){
            MaterialEntry materialEntry;
            if(!ParseMaterialMeta(shaderCook, discoveredNwbFile, doc, materialEntry))
                return false;

            if(materialEntry.virtualPath){
                const NameHash materialPathHash = materialEntry.virtualPath.hash();
                if(!seenPropertyAssetPathHashes.insert(materialPathHash).second){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("ShaderAssetCooker: duplicate property asset virtual path '{}'"),
                        StringConvert(materialEntry.virtualPath.c_str())
                    );
                    return false;
                }
                outMetadata.materialEntries.push_back(Move(materialEntry));
            }
            continue;
        }

        if(assetType == Geometry::AssetTypeName()){
            GeometryEntry geometryEntry;
            if(!ParseGeometryMeta(discoveredNwbFile, doc, geometryEntry))
                return false;

            if(geometryEntry.virtualPath){
                const NameHash geometryPathHash = geometryEntry.virtualPath.hash();
                if(!seenPropertyAssetPathHashes.insert(geometryPathHash).second){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("ShaderAssetCooker: duplicate property asset virtual path '{}'"),
                        StringConvert(geometryEntry.virtualPath.c_str())
                    );
                    return false;
                }
                outMetadata.geometryEntries.push_back(Move(geometryEntry));
            }
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
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: geometry or material assets were found, but no shader entries were cooked for this graphics volume"));
            return false;
        }

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
){
    ErrorCode errorCode;

    outPreparedPlan.preparedEntries.clear();
    outPreparedPlan.preparedEntries.reserve(inOutShaderEntries.size());
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

        outPreparedPlan.preparedEntries.push_back(Move(preparedEntry));
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
    for(PreparedShaderEntry& preparedEntry : preparedEntries){
        Core::ShaderCook::ShaderEntry& entry = preparedEntry.entry;
        const Name shaderName = ToName(entry.name);
        const Name stageName = ToName(entry.stage);
        const Name entryPointName = ToName(entry.entryPoint);
        if(!shaderName || !stageName || !entryPointName){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Shader cook failed to canonicalize shader identity for '{}' stage '{}' entry point '{}'"),
                StringConvert(entry.name),
                StringConvert(entry.stage),
                StringConvert(entry.entryPoint)
            );
            return false;
        }

        Core::ShaderCook::CookVector<Core::ShaderCook::DefineCombo> defineCombinations{Core::ShaderCook::CookAllocator<Core::ShaderCook::DefineCombo>(cookArena)};
        shaderCook.expandDefineCombinations(entry.defineValues, defineCombinations);
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
                entry,
                variantName
            );
            Vector<u8> cookedBytecode;
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

            CompactString compactVariantName;
            if(!compactVariantName.assign(variantName)){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Shader cook variant name exceeds CompactString capacity (shader='{}', stage='{}', variant='{}')"),
                    StringConvert(entry.name),
                    StringConvert(entry.stage),
                    StringConvert(variantName)
                );
                return false;
            }

            const Name variantNameId(compactVariantName.view());
            if(!variantNameId){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Shader cook failed to canonicalize variant identity for '{}' stage '{}' variant '{}'"),
                    StringConvert(entry.name),
                    StringConvert(entry.stage),
                    StringConvert(variantName)
                );
                return false;
            }

            const Name virtualPath = Core::ShaderArchive::buildVirtualPathName(shaderName, compactVariantName, stageName);
            if(!virtualPath){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Shader cook failed to build virtual path for '{}' stage '{}' variant '{}'"),
                    StringConvert(entry.name),
                    StringConvert(entry.stage),
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
            record.variantName = variantNameId;
            record.stage = stageName;
            record.entryPoint = entryPointName;
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

        Vector<u8> materialBinary;
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

        Vector<u8> geometryBinary;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderAssetCooker::cook(const Core::Assets::AssetCookOptions& options){
    ShaderCookEnvironment environment;
    environment.configuration = options.configuration;
    environment.repoRoot = options.repoRoot.empty() ? Path(".") : Path(options.repoRoot.c_str());
    environment.assetRoots.reserve(options.assetRoots.size());
    for(const CompactString& assetRoot : options.assetRoots)
        environment.assetRoots.push_back(Path(assetRoot.c_str()));
    environment.outputDirectory = Path(options.outputDirectory.c_str());
    environment.cacheDirectory = options.cacheDirectory.empty() ? Path() : Path(options.cacheDirectory.c_str());

    ShaderCookResult result;
    if(!cookShaderAssets(environment, result))
        return false;

    NWB_LOGGER_INFO(
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
    if(!__hidden_assets::ParseAssetMetadata(m_arena, shaderCook, nwbFiles, parsedMetadata))
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

    if(!__hidden_assets::ValidateAndNormalizeMaterials(shaderCook, preparedPlan.preparedEntries, parsedMetadata.materialEntries))
        return false;

    Vector<Core::ShaderArchive::Record> shaderIndexRecords;
    if(!__hidden_assets::ReserveShaderIndexRecords(preparedPlan.plannedFileCount, shaderIndexRecords))
        return false;

    __hidden_assets::VirtualPathHashSet seenVirtualPathHashes{Core::ShaderCook::CookAllocator<NameHash>(m_arena)};
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
    if(!__hidden_assets::EnsureEmptyDirectory(stagedVolumePaths.stageDirectory, "stage directory"))
        return false;
    __hidden_assets::StageDirectoryCleanupGuard stageDirectoryCleanup{stagedVolumePaths.stageDirectory};
    if(!__hidden_assets::RemoveDirectoryIfPresent(stagedVolumePaths.backupDirectory, "backup directory"))
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

        if(!volumeSession.flush()){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderAssetCooker: failed to flush staged volume metadata"));
            return false;
        }

        stagedFileCount = volumeSession.fileCount();
        stagedSegmentCount = volumeSession.segmentCount();
    }

    if(!Core::Filesystem::PublishStagedVolume(stagedVolumePaths, resolvedPaths.outputDirectory, volumeConfig.volumeName, stagedSegmentCount))
        return false;
    stageDirectoryCleanup.active = false;

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
