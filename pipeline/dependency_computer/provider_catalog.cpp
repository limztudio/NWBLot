// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "provider_catalog.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_dependency_provider_catalog{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
namespace Assets = Core::Assets;

inline constexpr AStringView s_ImplDirectoryName = "impl";
inline constexpr AStringView s_DiagnosticPrefix = "DependencyComputer";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddRoot(const NWB::Path& path, Assets::CookVector<Assets::ResolvedAssetRoot>& roots,
    Assets::ScratchArena& scratchArena
){
    Assets::ScratchString pathText = PathToString(scratchArena, path.lexicallyNormal());
#if defined(NWB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(pathText);
#endif
    for(const auto& root : roots){
        Assets::ScratchString existing = PathToString(scratchArena, root.path);
#if defined(NWB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(existing);
#endif
        if(existing == pathText)
            return;
    }
    Assets::ScratchString parentName = PathToString(scratchArena, path.parentPath().filename());
    CanonicalizeTextInPlace(parentName);
    const AStringView rootName = parentName == s_ImplDirectoryName ? Assets::s_EngineVirtualRoot : Assets::s_ProjectVirtualRoot;
    const ACompactString virtualRoot(rootName);
    roots.emplace_back(NWB::Path(path), virtualRoot);
}

[[nodiscard]] static Expected<Assets::CookVector<Assets::ResolvedAssetRoot>> ResolveRoots(const PipelineOptions& options, const NWB::Path& repoRoot,
    Assets::ScratchArena& scratchArena
){
    Assets::CookVector<Assets::ResolvedAssetRoot> roots(repoRoot.arena());
    const auto& sources = options.assetRoots.empty() ? options.inputs : options.assetRoots;
    roots.reserve(sources.size());
    for(const auto& source : sources){
        auto resolved = ResolveAbsolutePath(repoRoot.arena(), repoRoot, AStringView(source));
        if(!resolved){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: failed to resolve asset root from '{}'"), StringConvert(source));
            return MakeUnexpected(Failure{});
        }
        NWB::Path path = Move(*resolved);
        if(options.assetRoots.empty()){
            const auto directory = IsDirectory(path);
            if(!directory){
                NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: failed to inspect input '{}'"), PathToString<tchar>(path));
                return MakeUnexpected(Failure{});
            }
            if(!*directory)
                path = path.parentPath();
            NWB::Path ancestor = path;
            while(!ancestor.empty()){
                Assets::ScratchString name = PathToString(scratchArena, ancestor.filename());
                CanonicalizeTextInPlace(name);
                if(name == Assets::s_AssetsDirectoryName){
                    path = ancestor;
                    break;
                }
                const NWB::Path parent = ancestor.parentPath();
                if(parent == ancestor)
                    break;
                ancestor = parent;
            }
        }
        AddRoot(path, roots, scratchArena);
    }
    if(roots.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: no asset roots available for skin dependencies"));
        return MakeUnexpected(Failure{});
    }
    return roots;
}

[[nodiscard]] static bool SelectInput(const NWB::Path& path, const bool directory,
    const Assets::DiscoveredNwbFileVector& files, Vector<u8, Assets::ScratchArena>& selected,
    Vector<usize, Assets::ScratchArena>& order,
    Assets::ScratchArena& scratchArena
){
    Assets::ScratchString input = PathToString(scratchArena, path);
#if defined(NWB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(input);
#else
    if(!directory)
        CanonicalizeTextInPlace(input);
#endif
    bool matched = false;
    for(usize index = 0u; index < files.size(); ++index){
        const auto& file = files[index];
        if(directory){
            Assets::ScratchString physical = PathToString(scratchArena, file.filePath.lexicallyNormal());
#if defined(NWB_PLATFORM_WINDOWS)
            CanonicalizeTextInPlace(physical);
#endif
            if(!IsPathPrefixText(AStringView(input), AStringView(physical)))
                continue;
        }
        else if(AStringView(file.normalizedPathText) != AStringView(input))
            continue;
        if(!selected[index]){
            order.push_back(index);
            selected[index] = 1u;
        }
        matched = true;
    }
    return matched;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DependencyProviderCatalog::DependencyProviderCatalog(NWB::Core::Assets::AssetArena& arena)
    : m_arena(arena)
    , m_repoRoot(arena)
    , m_roots(arena)
    , m_files(arena)
    , m_virtualPaths(arena)
{}

bool DependencyProviderCatalog::discover(const PipelineOptions& options, NWB::Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_dependency_provider_catalog;
    const AStringView repoText = options.repoRoot.empty() ? AStringView(".") : AStringView(options.repoRoot);
    const auto absoluteRoot = AbsolutePath(NWB::Path(m_arena, repoText));
    if(!absoluteRoot){
        NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: failed to resolve repository root"));
        return false;
    }
    NWB::Path repoRoot = absoluteRoot->lexicallyNormal();
    auto roots = ResolveRoots(options, repoRoot, scratchArena);
    if(!roots)
        return false;
    auto files = Assets::DiscoverFilesWithExtension(m_arena, *roots, Assets::s_NwbExtension, scratchArena);
    if(!files)
        return false;
    Assets::AssetVector<Name> virtualPaths(m_arena);
    virtualPaths.reserve(files->size());
    for(const auto& file : *files){
        const auto virtualPath = Assets::BuildDerivedAssetVirtualPath(file.assetRoot, file.virtualRoot, file.filePath, scratchArena);
        if(!virtualPath)
            return false;
        virtualPaths.push_back(*virtualPath);
    }
    m_repoRoot = Move(repoRoot);
    m_roots = Move(*roots);
    m_files = Move(*files);
    m_virtualPaths = Move(virtualPaths);
    return true;
}

Expected<DependencyInputSelection> DependencyProviderCatalog::selectInputs(const PipelineOptions& options,
    NWB::Core::Alloc::ScratchArena& scratchArena
)const{
    using namespace __hidden_dependency_provider_catalog;
    DependencyInputSelection selection(scratchArena);
    auto& selected = selection.selected;
    auto& order = selection.order;
    selected.resize(m_files.size(), u8(0));
    order.reserve(m_files.size());
    for(const auto& input : options.inputs){
        const auto resolved = ResolveAbsolutePath(m_arena, m_repoRoot, AStringView(input));
        if(!resolved){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: failed to resolve input '{}'"), StringConvert(input));
            return MakeUnexpected(Failure{});
        }
        const NWB::Path& path = *resolved;
        const auto directoryResult = IsDirectory(path);
        const auto regular = directoryResult && *directoryResult ? Expected<bool, ErrorCode>(true) : IsRegularFile(path);
        if(!directoryResult || (!*directoryResult && (!regular || !*regular))){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: input is not a file or directory '{}'"), PathToString<tchar>(path));
            return MakeUnexpected(Failure{});
        }
        const bool directory = *directoryResult;
        bool contained = !directory;
        if(directory){
            Assets::ScratchString inputPath = PathToString(scratchArena, path);
#if defined(NWB_PLATFORM_WINDOWS)
            CanonicalizeTextInPlace(inputPath);
#endif
            for(const auto& root : m_roots){
                Assets::ScratchString rootPath = PathToString(scratchArena, root.path);
#if defined(NWB_PLATFORM_WINDOWS)
                CanonicalizeTextInPlace(rootPath);
#endif
                if(IsPathPrefixText(AStringView(rootPath), AStringView(inputPath))){
                    contained = true;
                    break;
                }
            }
        }
        if(!contained || (!SelectInput(path, directory, m_files, selected, order, scratchArena) && !directory)){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: input selects no .nwb asset within the asset roots '{}'")
                , PathToString<tchar>(path)
            );
            return MakeUnexpected(Failure{});
        }
    }
    return selection;
}

Expected<NWB::Core::Metascript::Document> DependencyProviderCatalog::read(const usize index,
    NWB::Core::Alloc::ScratchArena& scratchArena
)const{
    using namespace __hidden_dependency_provider_catalog;
    if(index >= m_files.size())
        return MakeUnexpected(Failure{});
    Assets::ScratchString text(scratchArena);
    Core::Metascript::Document document(m_arena);
    if(!Assets::ParseMetadataDocumentText(
        m_files[index].filePath,
        s_DiagnosticPrefix,
        text,
        document,
        [&](const AStringView source){ return document.parse(source); }
    ))
        return MakeUnexpected(Failure{});
    return document;
}

Expected<usize> DependencyProviderCatalog::resolve(const Name& virtualPath, const Name& assetType,
    NWB::Core::Alloc::ScratchArena& scratchArena
)const{
    using namespace __hidden_dependency_provider_catalog;
    if(!virtualPath || !assetType)
        return MakeUnexpected(Failure{});
    usize matched = Limit<usize>::s_Max;
    for(usize index = 0u; index < m_files.size(); ++index){
        if(m_virtualPaths[index] != virtualPath)
            continue;
        const auto document = read(index, scratchArena);
        if(!document)
            return MakeUnexpected(Failure{});
        if(Name(document->assetType()) != assetType)
            continue;
        if(document->declarations().size() != 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: provider '{}' declares multiple assets for '{}'")
                , PathToString<tchar>(m_files[index].filePath)
                , StringConvert(virtualPath)
            );
            return MakeUnexpected(Failure{});
        }
        if(matched != Limit<usize>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: multiple physical providers for '{}'"), StringConvert(virtualPath));
            return MakeUnexpected(Failure{});
        }
        matched = index;
    }
    if(matched == Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: no typed '{}' provider for '{}'")
            , StringConvert(assetType)
            , StringConvert(virtualPath)
        );
        return MakeUnexpected(Failure{});
    }
    return matched;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

