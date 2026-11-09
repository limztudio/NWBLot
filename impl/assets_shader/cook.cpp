// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "shader_define_planner.h"
#include "slang_compiler.h"
#include "slang_compiler_fingerprint.h"
#include "slang_compiler_workspace.h"
#include "source_dependencies.h"
#include "shader_types.h"

#include <impl/assets_shader/shader_source_fingerprint.adhd.h>
#include <impl/assets_shader/slang_compiler_request_validator.adhd.h>

#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/graphics/shader_stage_names.h>
#include <core/graphics/spirv_entry_point.h>
#include <core/common/log.h>

#include <global/filesystem/cleanup.h>
#include <global/hash_utils.h>
#include <global/process_execution.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_SLANGC_EXECUTABLE
#error "NWB_SLANGC_EXECUTABLE must be defined by the build configuration"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;
namespace Alloc = Core::Alloc;
namespace Metascript = Core::Metascript;

using CookString = ShaderCook::CookString;
using DefineEntry = ShaderCook::DefineEntry;
using IncludeEntry = ShaderCook::IncludeEntry;
using ShaderEntry = ShaderCook::ShaderEntry;
using DefineCombo = ShaderCook::DefineCombo;
template<typename T> using CookVector = ShaderCook::CookVector<T>;
template<typename K, typename V> using CookMap = ShaderCook::CookMap<K, V>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook_checksum{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchPath = ::Path<Core::Alloc::ScratchArena>;
using ScratchString = Core::Assets::ScratchString;

struct NormalizedDependencyRootAlias{
    ScratchPath root;
    ScratchString key;
    usize depth = 0u;


public:
    explicit NormalizedDependencyRootAlias(Core::Alloc::ScratchArena& scratchArena)noexcept
        : root(scratchArena)
        , key(scratchArena)
    {}
};

struct SortedDependencyItem{
    ScratchString canonicalPath;
    NotNull<const Path*> path;


public:
    SortedDependencyItem(const Path& dependency, Core::Alloc::ScratchArena& scratchArena)noexcept
        : canonicalPath(scratchArena)
        , path(&dependency)
    {}
};

static ScratchPath NormalizeDependencyRootAliasPath(const ScratchPath& source){
    ScratchPath path = source.lexicallyNormal();
    while(!path.empty() && !path.hasFilename()){
        const ScratchPath parentPath = path.parentPath();
        if(parentPath.empty() || parentPath == path)
            break;
        path = parentPath;
    }
    return path;
}

static Expected<ScratchString> BuildCanonicalDependencyPath(const Path& dependency, const Span<const NormalizedDependencyRootAlias> normalizedRootAliases, const b8 allowUnaliasedPath, Core::Alloc::ScratchArena& scratchArena){
    const auto normalizedDependencyResult = AbsolutePath(ScratchPath(scratchArena, dependency));
    if(!normalizedDependencyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency path '{}' : {}"), PathToString<tchar>(dependency), ConvertString(normalizedDependencyResult.error().message()));
        return MakeUnexpected(Failure{});
    }
    const ScratchPath normalizedDependency = normalizedDependencyResult->lexicallyNormal();

    const NormalizedDependencyRootAlias* bestRootAlias = nullptr;
    for(const NormalizedDependencyRootAlias& rootAlias : normalizedRootAliases){
        if(normalizedDependency != rootAlias.root && !PathHasDirectoryAncestor(normalizedDependency, rootAlias.root))
            continue;
        if(!bestRootAlias || rootAlias.depth > bestRootAlias->depth)
            bestRootAlias = &rootAlias;
    }
    if(!bestRootAlias){
        if(!allowUnaliasedPath){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum path '{}' is outside the declared dependency root aliases"), PathToString<tchar>(dependency));
            return MakeUnexpected(Failure{});
        }
        const ScratchString absolutePathText = PathToString(scratchArena, normalizedDependency);
        ScratchString canonicalPath(ShaderBinaryPayload::s_ShaderChecksumAbsolutePrefix, scratchArena);
        canonicalPath.reserve(AddSize(canonicalPath.size(), absolutePathText.size()));
        canonicalPath += absolutePathText;
        return canonicalPath;
    }

    const ScratchString relativePathText = PathToString(scratchArena, normalizedDependency.lexicallyRelative(bestRootAlias->root));

    const AStringView pathPrefix = allowUnaliasedPath ? ShaderBinaryPayload::s_ShaderChecksumAliasPrefix : AStringView();
    ScratchString canonicalPath(scratchArena);
    const usize relativePathSize = relativePathText.empty() ? 0u : AddSize(1u, relativePathText.size());
    canonicalPath.reserve(AddSize(AddSize(pathPrefix.size(), bestRootAlias->key.size()), relativePathSize));
    canonicalPath += pathPrefix;
    canonicalPath += bestRootAlias->key;
    if(!relativePathText.empty()){
        canonicalPath += '/';
        canonicalPath += relativePathText;
    }
    return canonicalPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_slang_compiler_workspace{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace ShaderBinaryPayload;

Atomic<u64> g_TemporarySequence{0u};

struct CompilerIncludeReplacement{
    SlangCompilerWorkspace::ScratchString pathText;
    usize sourceOffset;
    usize sourceBytes;
};

[[nodiscard]] static b8 WriteBomStrippedCompilerInput(
    const Path& sourcePath,
    const ShaderCook::ShaderCompilerRequest& request,
    const SlangCompilerWorkspace::ScratchPath& overlayRoot,
    const SlangCompilerWorkspace::ScratchPath& outputPath,
    SlangCompilerWorkspace::ScratchString& source,
    Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_slang_compiler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace ShaderBinaryPayload;
using ScratchPath = SlangCompilerWorkspace::ScratchPath;
using ScratchString = SlangCompilerWorkspace::ScratchString;
template<typename T>
using ScratchVector = SlangCompilerWorkspace::ScratchVector<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Slang's SPIR-V 1.5 profile does not include the standard capability set used by the generated Vulkan shaders.
// Declare it explicitly so Slang produces the same output without silently rewriting the requested profile.
static constexpr Array<AStringView, s_SpirvBaselineCapabilityCount> s_SpirvBaselineCapabilities = {
    s_SpirvCapabilityNonSemanticInfo,
    s_SpirvCapabilityUserType,
    s_SpirvCapabilityDerivativeControl,
    s_SpirvCapabilityImageQuery,
    s_SpirvCapabilityImageGatherExtended,
    s_SpirvCapabilitySparseResidency,
    s_SpirvCapabilityMinLod,
    s_SpirvCapabilityFragmentFullyCovered,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<AStringView, s_CommonCompilerArgumentCount> s_CommonCompilerArguments = {
    s_SlangTargetArgument, s_SlangTargetSpirv,
    s_SlangEmitSpirvDirectlyArgument,
    s_SlangUseEntryPointNameArgument,
    s_SlangWarningsAsErrorsArgument, s_SlangWarningsAsErrorsAll,
    s_SlangProfileArgument, ShaderBinaryPayload::s_Spirv15TargetProfileText,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<ScratchString> BuildCompilerDefineArgument(const ShaderBinaryPayload::ShaderMacroDefinition& define, const ShaderCook::ShaderCompilerRequest& request, const ScratchPath& overlayRoot, Alloc::ScratchArena& scratchArena){
    const b8 plannedMacroInclude = request.compilerInputsHaveBom && FindIf(request.externallyPlannedMacroIncludes.begin(), request.externallyPlannedMacroIncludes.end(), [&define](const AStringView name)noexcept{
            return name == define.name;
        }) != request.externallyPlannedMacroIncludes.end();
    const AStringView includeName = plannedMacroInclude ? UnquoteDoubleQuotedView(define.value) : AStringView();
    const ScratchPath includePath(scratchArena, includeName);
    Expected<ScratchPath> overlayIncludePath = MakeUnexpected(Failure{});
    usize valueCapacity = define.value.size();
    if(!includeName.empty() && includePath.isAbsolute()){
        const auto absoluteIncludePath = AbsolutePath(includePath);
        if(!absoluteIncludePath){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve planned macro include '{}': {}:{}"), PathToString<tchar>(includePath), ConvertString(absoluteIncludePath.error().category().name()), absoluteIncludePath.error().value());
            return MakeUnexpected(Failure{});
        }
        const ScratchPath normalizedIncludePath = absoluteIncludePath->lexicallyNormal();
        overlayIncludePath = SlangCompilerWorkspace::BuildCompilerOverlayPath(overlayRoot, normalizedIncludePath, scratchArena);
        if(!SlangCompilerWorkspace::IsCompilerDependency(request.dependencies, normalizedIncludePath) || !overlayIncludePath){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: planned macro include '{}' is not a mapped compiler dependency"), ConvertString(includeName));
            return MakeUnexpected(Failure{});
        }
        const auto capacity = SlangCompilerWorkspace::CompilerPathTextCapacity(*overlayIncludePath);
        if(!capacity)
            return MakeUnexpected(Failure{});
        const auto quotedCapacity = ShaderBinaryPayload::TryComputeQuotedCompilerPathCapacity(*capacity);
        if(!quotedCapacity)
            return MakeUnexpected(Failure{});
        valueCapacity = *quotedCapacity;
    }
    const usize fixedBytes = s_SlangDefineArgument.size() + (!define.value.empty() ? 1u : 0u);
    const auto argumentCapacity = ShaderBinaryPayload::TryComputeCompilerDefineArgumentCapacity(fixedBytes, define.name.size(), valueCapacity);
    if(!argumentCapacity)
        return MakeUnexpected(Failure{});
    ScratchString argument{scratchArena};
    argument.reserve(*argumentCapacity);
    argument += s_SlangDefineArgument;
    argument += define.name;
    if(!define.value.empty()){
        argument += '=';
        if(overlayIncludePath){
            argument += '"';
            auto out = BackInserter(argument);
            WriteConvertedText<char>(out, overlayIncludePath->native());
            argument += '"';
        }
        else
            argument += define.value;
    }
    return argument;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const Array<SlangStageMapping, 14u> s_StageMappings = {{
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::VertexStage), s_SlangStageVertex },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::HullStage), s_SlangStageHull },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::DomainStage), s_SlangStageDomain },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::GeometryStage), s_SlangStageGeometry },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::PixelStage), s_SlangStageFragment },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::ComputeStage), s_SlangStageCompute },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::AmplificationStage), s_SlangStageAmplification },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MeshStage), s_SlangStageMesh },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::RayGenerationStage), s_SlangStageRayGeneration },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::AnyHitStage), s_SlangStageAnyHit },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::ClosestHitStage), s_SlangStageClosestHit },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MissStage), s_SlangStageMiss },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::IntersectionStage), s_SlangStageIntersection },
    { Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::CallableStage), s_SlangStageCallable },
}};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace ShaderBinaryPayload;
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
        const ShaderDefineNameStatus::Enum nameStatus = ClassifyShaderDefineName(AStringView(defineName));
        if(nameStatus == ShaderDefineNameStatus::Reserved){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' is an engine transport feature selected in shader source"), PathToString<tchar>(nwbFilePath), ConvertString(defineName));
            return MakeUnexpected(Failure{});
        }
        if(nameStatus == ShaderDefineNameStatus::Invalid){
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


Expected<ShaderBinaryPayload::DependencyChecksum> ShaderCook::ComputeDependencyChecksum(
    const Span<const Path> dependencies,
    const Span<const Path> includeDirectories,
    const InitializerList<DependencyRootAlias> dependencyRootAliases,
    Core::Alloc::ScratchArena& scratchArena
){
    using __hidden_shader_cook_checksum::SortedDependencyItem;

    ShaderBinaryPayload::DependencyChecksum result;
    result.checksum = s_Fnv64OffsetBasis;

    if(dependencyRootAliases.size() == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires at least one dependency root alias"));
        return MakeUnexpected(Failure{});
    }

    Vector<__hidden_shader_cook_checksum::NormalizedDependencyRootAlias, Core::Alloc::ScratchArena> normalizedRootAliases{scratchArena};
    normalizedRootAliases.reserve(dependencyRootAliases.size());
    for(const DependencyRootAlias& rootAlias : dependencyRootAliases){
        if(rootAlias.root.empty() || rootAlias.key.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires non-empty dependency root aliases"));
            return MakeUnexpected(Failure{});
        }

        __hidden_shader_cook_checksum::NormalizedDependencyRootAlias normalizedAlias{scratchArena};
        const auto rootPath = AbsolutePath(__hidden_shader_cook_checksum::ScratchPath(scratchArena, rootAlias.root));
        if(!rootPath){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency root alias '{}' : {}"), PathToString<tchar>(rootAlias.root), ConvertString(rootPath.error().message()));
            return MakeUnexpected(Failure{});
        }

        normalizedAlias.root = __hidden_shader_cook_checksum::NormalizeDependencyRootAliasPath(*rootPath);
        normalizedAlias.key = rootAlias.key;
        CanonicalizeTextInPlace(normalizedAlias.key);
        if(normalizedAlias.key.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires non-empty dependency root alias keys"));
            return MakeUnexpected(Failure{});
        }
        normalizedAlias.depth = PathDepth(normalizedAlias.root);
        normalizedRootAliases.push_back(Move(normalizedAlias));
    }

    Fnv64AppendValue(result.checksum, ShaderBinaryPayload::s_ShaderDependencyChecksumVersion);
    Fnv64AppendValue(result.checksum, includeDirectories.size());
    for(const Path& includeDirectory : includeDirectories){
        const auto canonicalPath = __hidden_shader_cook_checksum::BuildCanonicalDependencyPath(includeDirectory, normalizedRootAliases, true, scratchArena);
        if(!canonicalPath)
            return MakeUnexpected(Failure{});
        result.checksum = UpdateFnv64TextExact(result.checksum, AStringView(*canonicalPath));
    }
    Fnv64AppendValue(result.checksum, dependencies.size());

    Vector<SortedDependencyItem, Core::Alloc::ScratchArena> sortedDependencies{scratchArena};
    sortedDependencies.reserve(dependencies.size());
    for(const Path& dependency : dependencies){
        SortedDependencyItem item(dependency, scratchArena);
        auto canonicalPath = __hidden_shader_cook_checksum::BuildCanonicalDependencyPath(dependency, normalizedRootAliases, false, scratchArena);
        if(!canonicalPath)
            return MakeUnexpected(Failure{});
        item.canonicalPath = Move(*canonicalPath);
        sortedDependencies.push_back(Move(item));
    }

    Sort(sortedDependencies.begin(), sortedDependencies.end(), [](const SortedDependencyItem& lhs, const SortedDependencyItem& rhs)noexcept(noexcept(lhs.canonicalPath < rhs.canonicalPath)){ return lhs.canonicalPath < rhs.canonicalPath; });

    Vector<u8, Core::Alloc::ScratchArena> dependencyBytes{scratchArena};
    for(const SortedDependencyItem& item : sortedDependencies){
        dependencyBytes.clear();
        auto readBinaryFileResult = ReadBinaryFile(*item.path, dependencyBytes);
        if(!readBinaryFileResult){
            if(readBinaryFileResult.error()){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency file '{}' : {}"), PathToString<tchar>(*item.path), ConvertString(readBinaryFileResult.error().message()));
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency file '{}'"), PathToString<tchar>(*item.path));
            }
            return MakeUnexpected(Failure{});
        }
        ShaderBinaryPayload::ShaderSourceFingerprint{}.appendDependencyChecksum(
            AStringView(item.canonicalPath),
            Span<const u8>(dependencyBytes.data(), dependencyBytes.size()),
            result.checksum,
            result.compilerInputsHaveBom
        );
    }

    return result;
}

u64 ShaderCook::ComputeSourceChecksum(const ShaderEntry& entry, const AStringView variantSignature, const u64 dependencyChecksum, const u64 compilerFingerprint, Core::Alloc::ScratchArena& scratchArena){
    u64 checksum = ShaderBinaryPayload::ShaderSourceFingerprint{}.beginSourceChecksum(
        AStringView(entry.name),
        entry.stage,
        entry.archiveStage,
        entry.rayQuery,
        entry.optimizationLevel,
        AStringView(entry.entryPoint),
        variantSignature
    );
    if(entry.implicitDefines.size() <= 1u){
        for(const auto& [defineName, defineValue] : entry.implicitDefines){
            ShaderBinaryPayload::ShaderSourceFingerprint{}.appendChecksumLine(defineName, checksum);
            ShaderBinaryPayload::ShaderSourceFingerprint{}.appendChecksumLine(defineValue, checksum);
        }
    }
    else{
        for(const auto& entryDefine : SortedDefineEntries(entry.implicitDefines, scratchArena)){
            ShaderBinaryPayload::ShaderSourceFingerprint{}.appendChecksumLine(*entryDefine.key, checksum);
            ShaderBinaryPayload::ShaderSourceFingerprint{}.appendChecksumLine(*entryDefine.value, checksum);
        }
    }
    return ShaderBinaryPayload::ShaderSourceFingerprint{}.finalizeSourceChecksum(dependencyChecksum, compilerFingerprint, checksum);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ShaderDefinePlanner::mergeInheritedDefines(const Span<const Path> dependencies, const CookMap<CookString, IncludeEntry>& includeMetadata, Alloc::ScratchArena& scratchArena, ShaderEntry& inOutEntry){
    Core::Assets::ScratchVector<NotNull<const IncludeEntry*>> inheritedEntries{scratchArena};
    inheritedEntries.reserve(dependencies.size());

    usize defineReserveCount = inOutEntry.defineValues.size();
    b8 canReserveDefineValues = true;
    for(const Path& dep : dependencies){
        Core::Assets::ScratchString depKey = PathToString(scratchArena, dep);
#if defined(NWB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(depKey);
#endif
        const auto found = includeMetadata.find(AStringView(depKey));
        if(found == includeMetadata.end())
            continue;

        const IncludeEntry& includeEntry = found.value();
        inheritedEntries.push_back(MakeNotNull(&includeEntry));

        if(!ShaderBinaryPayload::TryAccumulateInheritedShaderDefineReserve(includeEntry.defineValues.size(), defineReserveCount))
            canReserveDefineValues = false;
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

b8 ShaderDefinePlanner::expandDefineCombinations(const CookMap<CookString, DefineEntry>& defineValues, Alloc::ScratchArena& scratchArena, CookVector<DefineCombo>& outCombinations){
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

    for(const auto& entry : ShaderCook::SortedDefineEntries(defineValues, scratchArena)){
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

        const auto expandedCount = ShaderBinaryPayload::TryComputeShaderExpandedCombinationCount(outCombinations.size(), values.size());
        if(!expandedCount){
            outCombinations.clear();
            return false;
        }

        CookVector<DefineCombo> expanded{m_memoryArena};
        expanded.reserve(*expandedCount);

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


SlangCompilerWorkspace::ScopedDirectoryCleanupGuard::~ScopedDirectoryCleanupGuard()noexcept{
    const auto removed = RemoveAllForCleanup(m_path);
    if(removed)
        return;

    const DiagnosticEventText diagnostic = MakeDiagnosticEventText("ShaderCook: failed to remove temporary compiler directory '{}', error {}", m_path.c_str(), removed.error().value());
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = ShaderBinaryPayload::s_SlangWorkspaceDiagnosticWarningEventName,
        .category = ShaderBinaryPayload::s_SlangWorkspaceCleanupDiagnosticCategory,
        .message = diagnostic.view(),
        .file = __FILE__,
        .line = __LINE__,
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SlangCompilerWorkspace::ScratchPath> SlangCompilerWorkspace::CreateCompilerWorkDirectory(const ScratchPath& parentDirectory, Alloc::ScratchArena& scratchArena){
    auto ensureDirectoriesResult = EnsureDirectories(parentDirectory);
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create compiler temporary parent '{}' : {}"), PathToString<tchar>(parentDirectory), ConvertString(ensureDirectoriesResult.error().message()));
        return MakeUnexpected(Failure{});
    }

    for(;;){
        const u64 sequence = __hidden_slang_compiler_workspace::g_TemporarySequence.fetch_add(1u, MemoryOrder::relaxed);
        if(sequence == Limit<u64>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler temporary sequence overflow"));
            return MakeUnexpected(Failure{});
        }
        ScratchString directoryName(ShaderBinaryPayload::s_ShaderWorkspacePrefix, scratchArena);
        directoryName.reserve(directoryName.size() + ShaderBinaryPayload::s_ShaderWorkspaceHexPidAndSeqReserveBytes);
        AppendHexU64(CurrentProcessId(), directoryName);
        directoryName += '_';
        AppendHexU64(sequence, directoryName);
        ScratchPath directory = parentDirectory / directoryName;
        const auto created = CreateDirectories(directory);
        if(created && *created)
            return directory;
        if(!created){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create isolated compiler directory '{}' : {}"), PathToString<tchar>(directory), ConvertString(created.error().message()));
            return MakeUnexpected(Failure{});
        }
    }
}

Expected<usize> SlangCompilerWorkspace::CompilerPathTextCapacity(const ScratchPath& path){
    return ShaderBinaryPayload::TryComputeCompilerPathTextCapacity(path.native().size(), IsSame_V<ScratchPath::value_type, char>);
}

Expected<SlangCompilerWorkspace::ScratchString> SlangCompilerWorkspace::BuildCompilerPathText(const ScratchPath& path, Alloc::ScratchArena& scratchArena){
    const auto capacity = CompilerPathTextCapacity(path);
    if(!capacity)
        return MakeUnexpected(Failure{});
    ScratchString text{scratchArena};
    text.reserve(*capacity);
    auto out = BackInserter(text);
    WriteConvertedText<char>(out, path.native());
    return text;
}

Expected<SlangCompilerWorkspace::ScratchPath> SlangCompilerWorkspace::BuildCompilerOverlayPath(const ScratchPath& overlayRoot, const ScratchPath& absolutePath, Alloc::ScratchArena& scratchArena){
    if(!absolutePath.isAbsolute())
        return MakeUnexpected(Failure{});

    const auto rootIt = absolutePath.begin();
    if(rootIt == absolutePath.end())
        return MakeUnexpected(Failure{});

    const ScratchPath rootPath(scratchArena, rootIt.nativeComponent());
    const ScratchPath relativePath = absolutePath.lexicallyRelative(rootPath);
    if(relativePath.empty())
        return MakeUnexpected(Failure{});

    const auto rootText = BuildCompilerPathText(rootPath, scratchArena);
    if(!rootText)
        return MakeUnexpected(Failure{});
    const auto rootKeyCapacity = ShaderBinaryPayload::TryComputeCompilerOverlayRootKeyCapacity(rootText->size());
    if(!rootKeyCapacity)
        return MakeUnexpected(Failure{});
    ScratchString rootKey(ShaderBinaryPayload::s_SlangWorkspaceRootKeyPrefix, scratchArena);
    rootKey.reserve(*rootKeyCapacity);
    for(const char ch : *rootText){
        const u8 byte = static_cast<u8>(ch);
        rootKey += ShaderBinaryPayload::s_SlangWorkspaceHexDigits[byte >> s_HexNibbleBits];
        rootKey += ShaderBinaryPayload::s_SlangWorkspaceHexDigits[byte & s_HexNibbleMask];
    }
    return overlayRoot / rootKey / relativePath;
}

b8 SlangCompilerWorkspace::IsCompilerDependency(const Span<const Path> dependencies, const ScratchPath& absolutePath)noexcept{
    for(const Path& dependency : dependencies){
        if(dependency == absolutePath)
            return true;
    }
    return false;
}

Expected<SlangCompilerWorkspace::CompilerInputs> SlangCompilerWorkspace::PrepareBomStrippedCompilerInputs(const ShaderCook::ShaderCompilerRequest& request, const ScratchPath& overlayRoot, Alloc::ScratchArena& scratchArena){
    ScratchVector<ScratchPath> includeDirectories(scratchArena);

    if(request.dependencies.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: compiler request for '{}' has no resolved source dependencies"), ConvertString(request.shaderName));
        return MakeUnexpected(Failure{});
    }

    if(!request.compilerInputsHaveBom){
        includeDirectories.reserve(request.includeDirectories.size());
        for(const Path& includeDirectory : request.includeDirectories)
            includeDirectories.emplace_back(scratchArena, includeDirectory);
        return CompilerInputs{ .includeDirectories = Move(includeDirectories), .sourcePath = ScratchPath(scratchArena, request.sourcePath) };
    }

    auto ensureDirectoriesResult2 = EnsureDirectories(overlayRoot);
    if(!ensureDirectoriesResult2){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create temporary compiler source directory '{}' : {}"), PathToString<tchar>(overlayRoot), ConvertString(ensureDirectoriesResult2.error().message()));
        return MakeUnexpected(Failure{});
    }

    // Encode each absolute root separately and preserve nested relative includes within that root.
    for(const Path& dependency : request.dependencies){
        const auto overlayDependencyPath = BuildCompilerOverlayPath(overlayRoot, ScratchPath(scratchArena, dependency), scratchArena);
        if(!overlayDependencyPath){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map compiler input '{}' into the temporary source directory"), PathToString<tchar>(dependency));
            return MakeUnexpected(Failure{});
        }

        auto ensureDirectoriesResult3 = EnsureDirectories(overlayDependencyPath->parentPath());
        if(!ensureDirectoriesResult3){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to create temporary compiler source parent '{}' : {}"), PathToString<tchar>(overlayDependencyPath->parentPath()), ConvertString(ensureDirectoriesResult3.error().message()));
            return MakeUnexpected(Failure{});
        }

        ScratchString sourceText{scratchArena};
        const auto sourceRead = ReadTextFile(dependency, sourceText);
        if(!sourceRead){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read compiler input '{}' while writing BOM-stripped source: {}:{}"), PathToString<tchar>(dependency), ConvertString(sourceRead.error().category().name()), sourceRead.error().value());
            return MakeUnexpected(Failure{});
        }
        StripUtf8Bom(sourceText);
        if(!__hidden_slang_compiler_workspace::WriteBomStrippedCompilerInput(dependency, request, overlayRoot, *overlayDependencyPath, sourceText, scratchArena))
            return MakeUnexpected(Failure{});
    }

    const auto absoluteSourcePath = AbsolutePath(ScratchPath(scratchArena, request.sourcePath));
    if(!absoluteSourcePath){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve compiler source '{}': {}:{}"), PathToString<tchar>(request.sourcePath), ConvertString(absoluteSourcePath.error().category().name()), absoluteSourcePath.error().value());
        return MakeUnexpected(Failure{});
    }
    const ScratchPath normalizedSourcePath = absoluteSourcePath->lexicallyNormal();
    auto sourcePathResult = BuildCompilerOverlayPath(overlayRoot, normalizedSourcePath, scratchArena);
    if(!IsCompilerDependency(request.dependencies, normalizedSourcePath) || !sourcePathResult){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map source '{}' into the temporary compiler source directory"), PathToString<tchar>(request.sourcePath));
        return MakeUnexpected(Failure{});
    }

    const auto includeDirectoryCapacity = ShaderBinaryPayload::TryComputeCompilerIncludeDirectoryCapacity(request.shaderName, request.includeDirectories.size());
    if(!includeDirectoryCapacity)
        return MakeUnexpected(Failure{});

    includeDirectories.reserve(*includeDirectoryCapacity);
    for(const Path& includeDirectory : request.includeDirectories){
        const auto absoluteIncludeDirectory = AbsolutePath(ScratchPath(scratchArena, includeDirectory));
        if(!absoluteIncludeDirectory){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve compiler include directory '{}': {}:{}"), PathToString<tchar>(includeDirectory), ConvertString(absoluteIncludeDirectory.error().category().name()), absoluteIncludeDirectory.error().value());
            return MakeUnexpected(Failure{});
        }
        const ScratchPath normalizedIncludeDirectory = absoluteIncludeDirectory->lexicallyNormal();
        auto overlayIncludeDirectory = BuildCompilerOverlayPath(overlayRoot, normalizedIncludeDirectory, scratchArena);
        if(!overlayIncludeDirectory){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to map include directory '{}' into the temporary compiler source directory"), PathToString<tchar>(includeDirectory));
            return MakeUnexpected(Failure{});
        }
        includeDirectories.push_back(Move(*overlayIncludeDirectory));
    }
    for(const Path& includeDirectory : request.includeDirectories)
        includeDirectories.emplace_back(scratchArena, includeDirectory);

    return CompilerInputs{ .includeDirectories = Move(includeDirectories), .sourcePath = Move(*sourcePathResult) };
}

b8 __hidden_slang_compiler_workspace::WriteBomStrippedCompilerInput(
    const Path& sourcePath,
    const ShaderCook::ShaderCompilerRequest& request,
    const SlangCompilerWorkspace::ScratchPath& overlayRoot,
    const SlangCompilerWorkspace::ScratchPath& outputPath,
    SlangCompilerWorkspace::ScratchString& source,
    Alloc::ScratchArena& scratchArena
){
    using namespace SlangCompilerWorkspace;
    SpliceSourceLines(source);
    ScratchString scanSource(AStringView(source), scratchArena);
    MaskCStyleSourceComments(scanSource);
    const AStringView sourceView(source.data(), source.size());
    const AStringView scanView(scanSource);
    usize includeCount = 0u;
    for(usize begin = 0u; begin < scanView.size();){
        const usize foundEnd = scanView.find('\n', begin);
        const usize end = foundEnd == AStringView::npos ? scanView.size() : foundEnd;
        if(ShaderBinaryPayload::TryExtractIncludeDirective(scanView.substr(begin, end - begin)).has_value())
            ++includeCount;
        begin = end < scanView.size() ? end + 1u : end;
    }
    ScratchVector<__hidden_slang_compiler_workspace::CompilerIncludeReplacement> replacements{scratchArena};
    replacements.reserve(includeCount);
    usize outputBytes = sourceView.size();
    for(usize begin = 0u; begin < scanView.size();){
        const usize foundEnd = scanView.find('\n', begin);
        const usize end = foundEnd == AStringView::npos ? scanView.size() : foundEnd;
        const AStringView scanLine = scanView.substr(begin, end - begin);
        const Expected<ShaderBinaryPayload::IncludeDirective> include = ShaderBinaryPayload::TryExtractIncludeDirective(scanLine);
        if(include && include->kind == ShaderBinaryPayload::IncludeKind::Unsupported){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: unsupported dependency directive '{}' in '{}'"), ConvertString(TrimView(scanLine)), PathToString<tchar>(sourcePath));
            return false;
        }
        if(include && include->kind != ShaderBinaryPayload::IncludeKind::Macro){
            const ScratchPath includePath(scratchArena, include->name);
            if(includePath.isAbsolute()){
                const auto absoluteIncludePath = AbsolutePath(includePath);
                if(!absoluteIncludePath){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to resolve compiler include '{}': {}:{}"), PathToString<tchar>(includePath), ConvertString(absoluteIncludePath.error().category().name()), absoluteIncludePath.error().value());
                    return false;
                }
                const ScratchPath normalizedIncludePath = absoluteIncludePath->lexicallyNormal();
                auto overlayIncludePath = BuildCompilerOverlayPath(overlayRoot, normalizedIncludePath, scratchArena);
                if(!IsCompilerDependency(request.dependencies, normalizedIncludePath) || !overlayIncludePath){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: absolute include '{}' from '{}' is not a mapped compiler dependency"), ConvertString(include->name), PathToString<tchar>(sourcePath));
                    return false;
                }
                auto pathText = BuildCompilerPathText(*overlayIncludePath, scratchArena);
                if(!pathText)
                    return false;
                const auto replacementOutputBytes = ShaderBinaryPayload::TryComputeCompilerReplacementOutputBytes(outputBytes, include->name.size(), pathText->size());
                if(!replacementOutputBytes){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: BOM-stripped compiler input '{}' exceeds representable size"), PathToString<tchar>(outputPath));
                    return false;
                }
                outputBytes = *replacementOutputBytes;
                const usize sourceOffset = begin + static_cast<usize>(include->name.data() - scanLine.data());
                replacements.push_back({ Move(*pathText), sourceOffset, include->name.size() });
            }
        }
        begin = end < scanView.size() ? end + 1u : end;
    }
    if(!CanRepresentStreamSize(static_cast<u64>(outputBytes))){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: BOM-stripped compiler input '{}' exceeds stream size"), PathToString<tchar>(outputPath));
        return false;
    }

    // Stream borrowed slices so replacement output never blocks the source/scan buffers' LIFO retirement.
    OutputFileStream stream(outputPath, s_FileOpenBinary | s_FileOpenTruncate);
    if(!stream.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to open BOM-stripped compiler input '{}' for writing"), PathToString<tchar>(outputPath));
        return false;
    }
    usize sourceCursor = 0u;
    for(const __hidden_slang_compiler_workspace::CompilerIncludeReplacement& replacement : replacements){
        if(replacement.sourceOffset > sourceCursor)
            stream.write(sourceView.data() + sourceCursor, static_cast<StreamSize>(replacement.sourceOffset - sourceCursor));
        if(!replacement.pathText.empty())
            stream.write(replacement.pathText.data(), static_cast<StreamSize>(replacement.pathText.size()));
        sourceCursor = replacement.sourceOffset + replacement.sourceBytes;
    }
    if(sourceCursor < sourceView.size())
        stream.write(sourceView.data() + sourceCursor, static_cast<StreamSize>(sourceView.size() - sourceCursor));
    stream.close();
    if(!stream.good()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to write or close BOM-stripped compiler input '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> SlangCompilerFingerprint::Compute(const Path& temporaryRoot, Alloc::ScratchArena& scratchArena, const Span<const AStringView> commonArguments, const Span<const AStringView> baselineCapabilities, const Span<const ShaderBinaryPayload::SlangStageMapping> stageMappings){
    using namespace ShaderBinaryPayload;
    using ScratchPath = SlangCompilerWorkspace::ScratchPath;
    using ScratchString = SlangCompilerWorkspace::ScratchString;

    u64 fingerprint = s_Fnv64OffsetBasis;
    const ScratchPath compilerPath(scratchArena, NWB_SLANGC_EXECUTABLE);
    InputFileStream compilerStream(compilerPath, s_FileOpenBinary);
    if(!compilerStream.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to open configured compiler '{}' for fingerprinting"), PathToString<tchar>(compilerPath));
        return MakeUnexpected(Failure{});
    }

    Array<char, s_SlangFingerprintReadBufferBytes> compilerBytes{};
    for(;;){
        compilerStream.read(compilerBytes.data(), static_cast<StreamSize>(compilerBytes.size()));
        const StreamSize bytesRead = compilerStream.gcount();
        if(bytesRead > 0)
            fingerprint = UpdateFnv64TextExact(fingerprint, AStringView(compilerBytes.data(), static_cast<usize>(bytesRead)));
        if(compilerStream.eof() && !compilerStream.bad())
            break;
        if(!compilerStream.good()){
            NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read configured compiler '{}' for fingerprinting"), PathToString<tchar>(compilerPath));
            return MakeUnexpected(Failure{});
        }
    }

    const auto workDirectoryResult = SlangCompilerWorkspace::CreateCompilerWorkDirectory(ScratchPath(scratchArena, temporaryRoot), scratchArena);
    if(!workDirectoryResult)
        return MakeUnexpected(Failure{});
    const ScratchPath& workDirectory = *workDirectoryResult;
    SlangCompilerWorkspace::ScopedDirectoryCleanupGuard workDirectoryCleanup(workDirectory);
    const ScratchPath versionPath = workDirectory / s_SlangVersionFileName;
    const auto versionPathText = SlangCompilerWorkspace::BuildCompilerPathText(versionPath, scratchArena);
    if(!versionPathText)
        return MakeUnexpected(Failure{});
    const Array<AStringView, 2u> arguments = { AStringView(NWB_SLANGC_EXECUTABLE), s_SlangVersionArgument };
    const auto exitCode = RunProcessRedirectedToFile(scratchArena, arguments, AStringView(*versionPathText));
    if(!exitCode){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to execute configured compiler '{}' for version query: process execution failure {}"), PathToString<tchar>(compilerPath), static_cast<u32>(exitCode.error()));
        return MakeUnexpected(Failure{});
    }
    if(*exitCode != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: configured compiler '{}' version query failed with exit code {}"), PathToString<tchar>(compilerPath), *exitCode);
        return MakeUnexpected(Failure{});
    }
    ScratchString versionText(scratchArena);
    const auto versionRead = ReadBinaryFile(versionPath, versionText);
    if(!versionRead){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read configured compiler version '{}': {}:{}"), PathToString<tchar>(versionPath), ConvertString(versionRead.error().category().name()), versionRead.error().value());
        return MakeUnexpected(Failure{});
    }
    if(TrimView(versionText).empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: configured compiler '{}' returned empty version text"), PathToString<tchar>(compilerPath));
        return MakeUnexpected(Failure{});
    }

    // The version identifies the loaded Slang compiler library as well as its executable launcher.
    return ShaderSourceFingerprint{}.finalizeCompilerFingerprint(
        AStringView(versionText),
        commonArguments,
        baselineCapabilities,
        stageMappings,
        fingerprint
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> SlangShaderCompiler::ComputeCompilerFingerprint(const Path& temporaryRoot, Alloc::ScratchArena& scratchArena){
    using namespace __hidden_slang_compiler;
    return SlangCompilerFingerprint::Compute(temporaryRoot, scratchArena, s_CommonCompilerArguments, s_SpirvBaselineCapabilities, s_StageMappings);
}

b8 SlangShaderCompiler::CompileVariant(const ShaderCook::ShaderCompilerRequest& request, Core::Alloc::ScratchArena& scratchArena, ShaderCook::CookVector<u8>& outBytecode){
    using namespace __hidden_slang_compiler;

    outBytecode.clear();

    AStringView slangStage;
    AStringView optimizationArgument;
    if(!SlangCompilerRequestValidator{}.validateInputs(
        request.shaderName,
        request.sourcePath.empty(),
        request.outputPath.empty(),
        request.entryPoint,
        request.stage,
        static_cast<u8>(request.optimizationLevel),
        s_StageMappings,
        slangStage,
        optimizationArgument
    ))
        return false;

    const ScratchPath outputParent = request.outputPath.parentPath();
    const auto compilerWorkDirectoryResult = SlangCompilerWorkspace::CreateCompilerWorkDirectory(outputParent.empty() ? ScratchPath(scratchArena, s_CurrentDirectoryPathText) : outputParent, scratchArena);
    if(!compilerWorkDirectoryResult)
        return false;
    const ScratchPath& compilerWorkDirectory = *compilerWorkDirectoryResult;
    SlangCompilerWorkspace::ScopedDirectoryCleanupGuard workDirectoryCleanup(compilerWorkDirectory);
    const ScratchPath diagnosticsPath = compilerWorkDirectory / s_CompilerDiagFileName;
    const ScratchPath compilerOutputPath = compilerWorkDirectory / s_ModuleSpvFileName;
    const ScratchPath compilerOverlayRoot = compilerWorkDirectory / s_SourcesDirName;
    const auto compilerInputs = SlangCompilerWorkspace::PrepareBomStrippedCompilerInputs(request, compilerOverlayRoot, scratchArena);
    if(!compilerInputs)
        return false;
    const ScratchVector<ScratchPath>& compilerIncludeDirectories = compilerInputs->includeDirectories;

    const usize includeDirectoryCount = compilerIncludeDirectories.size();
    const usize defineCount = request.defines.size();
    usize argumentReserve = 0u;
    usize ownedArgumentCount = 0u;
    if(!SlangCompilerRequestValidator{}.argumentCounts(request.shaderName, includeDirectoryCount, defineCount, argumentReserve, ownedArgumentCount))
        return false;

    ScratchVector<AStringView> arguments(scratchArena);
    arguments.reserve(argumentReserve);
    ScratchVector<ScratchString> ownedArguments(scratchArena);
    // Retire nested string allocations before the owner array, independently of STL destruction order.
    ScopeExit retireArguments([&ownedArguments]()noexcept{
        while(!ownedArguments.empty())
            ownedArguments.pop_back();
    });
    // Reserve owners before publishing views so inline string storage cannot move.
    ownedArguments.reserve(ownedArgumentCount);
    arguments.push_back(AStringView(NWB_SLANGC_EXECUTABLE));

    auto sourcePathText = SlangCompilerWorkspace::BuildCompilerPathText(compilerInputs->sourcePath, scratchArena);
    if(!sourcePathText)
        return false;
    ownedArguments.push_back(Move(*sourcePathText));
    arguments.push_back(AStringView(ownedArguments.back()));
    for(const AStringView argument : s_CommonCompilerArguments)
        arguments.push_back(argument);
    if(!optimizationArgument.empty())
        arguments.push_back(optimizationArgument);
    if(request.rayQuery){
        arguments.push_back(s_SlangCapabilityArgument);
        arguments.push_back(ShaderBinaryPayload::s_SpvRayQueryCapabilityText);
    }
    for(const AStringView capability : s_SpirvBaselineCapabilities){
        arguments.push_back(s_SlangCapabilityArgument);
        arguments.push_back(capability);
    }
    arguments.push_back(s_SlangEntryPointArgument);
    arguments.push_back(request.entryPoint);
    arguments.push_back(s_SlangStageArgument);
    arguments.push_back(slangStage);
    for(const ScratchPath& includeDirectory : compilerIncludeDirectories){
        arguments.push_back(s_SlangIncludeArgument);
        auto includeDirectoryText = SlangCompilerWorkspace::BuildCompilerPathText(includeDirectory, scratchArena);
        if(!includeDirectoryText)
            return false;
        ownedArguments.push_back(Move(*includeDirectoryText));
        arguments.push_back(AStringView(ownedArguments.back()));
    }

    for(const ShaderBinaryPayload::ShaderMacroDefinition& define : request.defines){
        auto defineArgument = BuildCompilerDefineArgument(define, request, compilerOverlayRoot, scratchArena);
        if(!defineArgument)
            return false;
        ownedArguments.push_back(Move(*defineArgument));
        arguments.push_back(AStringView(ownedArguments.back()));
    }

    arguments.push_back(s_SlangOutputArgument);
    auto outputPathText = SlangCompilerWorkspace::BuildCompilerPathText(compilerOutputPath, scratchArena);
    if(!outputPathText)
        return false;
    ownedArguments.push_back(Move(*outputPathText));
    arguments.push_back(AStringView(ownedArguments.back()));

    const auto diagnosticsPathText = SlangCompilerWorkspace::BuildCompilerPathText(diagnosticsPath, scratchArena);
    if(!diagnosticsPathText)
        return false;
    const auto exitCode = ::RunProcessRedirectedToFile(scratchArena, arguments, AStringView(*diagnosticsPathText));
    if(!exitCode || *exitCode != 0){
        const TStringView failureStage = exitCode ? NWB_TEXT("compiler exit code") : NWB_TEXT("process execution failure");
        const i64 failureCode = exitCode ? static_cast<i64>(*exitCode) : static_cast<i64>(exitCode.error());
        ScratchString diagnostics{scratchArena};
        const auto diagnosticsRead = ReadBinaryFile(diagnosticsPath, diagnostics);
        if(!diagnosticsRead){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): {} {}; diagnostics read '{}' failed: {}:{}")
                , ConvertString(request.shaderName)
                , ConvertString(request.variantName)
                , failureStage
                , failureCode
                , PathToString<tchar>(diagnosticsPath)
                , ConvertString(diagnosticsRead.error().category().name())
                , diagnosticsRead.error().value()
            );
        }
        else if(!diagnostics.empty())
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): {} {} :\n{}"), ConvertString(request.shaderName), ConvertString(request.variantName), failureStage, failureCode, ConvertString(diagnostics));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): {} {}"), ConvertString(request.shaderName), ConvertString(request.variantName), failureStage, failureCode);
        return false;
    }

    ScratchString diagnostics{scratchArena};
    const auto diagnosticsRead = ReadBinaryFile(diagnosticsPath, diagnostics);
    if(!diagnosticsRead){
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to read compiler diagnostics '{}' for '{}' (variant '{}'): {}:{}")
            , PathToString<tchar>(diagnosticsPath)
            , ConvertString(request.shaderName)
            , ConvertString(request.variantName)
            , ConvertString(diagnosticsRead.error().category().name())
            , diagnosticsRead.error().value()
        );
        return false;
    }
    if(!diagnostics.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compiler emitted unexpected diagnostics for '{}' (variant '{}') :\n{}"), ConvertString(request.shaderName), ConvertString(request.variantName), ConvertString(diagnostics));
        return false;
    }

    auto readBinaryFileResult = ReadBinaryFile(compilerOutputPath, outBytecode);
    if(!readBinaryFileResult){
        if(readBinaryFileResult.error()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}' : {}")
                , ConvertString(request.shaderName)
                , ConvertString(request.variantName)
                , PathToString<tchar>(compilerOutputPath)
                , ConvertString(readBinaryFileResult.error().message())
            );
        }
        else
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}'"), ConvertString(request.shaderName), ConvertString(request.variantName), PathToString<tchar>(compilerOutputPath));
        return false;
    }

    const auto stageName = Name::FromText(request.stage);
    if(!stageName){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): shader stage contains an embedded null"), ConvertString(request.shaderName), ConvertString(request.variantName));
        outBytecode.clear();
        return false;
    }
    const Core::ShaderType::Enum shaderType = Core::ShaderStageNames::ShaderTypeFromArchiveStageName(*stageName);
    if(!Core::ResolveSpirvEntryPointName(BinaryByteView{ outBytecode.data(), outBytecode.size() }, request.entryPoint, Core::ShaderType::ToMask(shaderType))){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}'): malformed SPIR-V or wrong physical stage/entry point"), ConvertString(request.shaderName), ConvertString(request.variantName));
        outBytecode.clear();
        return false;
    }

    auto renamePathResult = RenamePath(compilerOutputPath, request.outputPath);
    if(!renamePathResult){
        ScratchVector<u8> publishedBytecode(scratchArena);
        if(
            ReadBinaryFile(request.outputPath, publishedBytecode)
            && publishedBytecode.size() == outBytecode.size()
            && NWB_MEMCMP(publishedBytecode.data(), outBytecode.data(), outBytecode.size()) == 0
            && Core::ResolveSpirvEntryPointName(BinaryByteView{ publishedBytecode.data(), publishedBytecode.size() }, request.entryPoint, Core::ShaderType::ToMask(shaderType))
        )
            return true;
        NWB_LOGGER_ERROR(NWB_TEXT("ShaderCook: failed to publish verified bytecode cache '{}' : {}"), PathToString<tchar>(request.outputPath), ConvertString(renamePathResult.error().message()));
        outBytecode.clear();
        return false;
    }
    return true;
}


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
        });
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
