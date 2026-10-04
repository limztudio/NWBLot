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
    Assets::ScratchArena& scratchArena){
    Assets::ScratchString pathText = PathToString(scratchArena, path.lexically_normal());
#if defined(GLB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(pathText);
#endif
    for(const auto& root : roots){
        Assets::ScratchString existing = PathToString(scratchArena, root.path);
#if defined(GLB_PLATFORM_WINDOWS)
        CanonicalizeTextInPlace(existing);
#endif
        if(existing == pathText)
            return;
    }
    Assets::ScratchString parentName = PathToString(scratchArena, path.parent_path().filename());
    CanonicalizeTextInPlace(parentName);
    const AStringView rootName = parentName == s_ImplDirectoryName ? Assets::s_EngineVirtualRoot : Assets::s_ProjectVirtualRoot;
    const ACompactString virtualRoot(rootName);
    roots.emplace_back(NWB::Path(path), virtualRoot);
}

[[nodiscard]] static bool ResolveRoots(const PipelineOptions& options, const NWB::Path& repoRoot,
    Assets::CookVector<Assets::ResolvedAssetRoot>& roots, Assets::ScratchArena& scratchArena){
    const auto& sources = options.assetRoots.empty() ? options.inputs : options.assetRoots;
    roots.reserve(sources.size());
    for(const auto& source : sources){
        ErrorCode error;
        NWB::Path path(repoRoot.arena());
        if(!ResolveAbsolutePath(repoRoot, AStringView(source), path, error)){
            NWB_LOGGER_ERROR(GLB_TEXT("DependencyComputer: failed to resolve asset root from '{}'"), StringConvert(source));
            return false;
        }
        if(options.assetRoots.empty()){
            const bool directory = IsDirectory(path, error);
            if(error){
                NWB_LOGGER_ERROR(GLB_TEXT("DependencyComputer: failed to inspect input '{}'"), PathToString<tchar>(path));
                return false;
            }
            if(!directory)
                path = path.parent_path();
            NWB::Path ancestor = path;
            while(!ancestor.empty()){
                Assets::ScratchString name = PathToString(scratchArena, ancestor.filename());
                CanonicalizeTextInPlace(name);
                if(name == Assets::s_AssetsDirectoryName){
                    path = ancestor;
                    break;
                }
                const NWB::Path parent = ancestor.parent_path();
                if(parent == ancestor)
                    break;
                ancestor = parent;
            }
        }
        AddRoot(path, roots, scratchArena);
    }
    if(roots.empty()){
        NWB_LOGGER_ERROR(GLB_TEXT("DependencyComputer: no asset roots available for skin dependencies"));
        return false;
    }
    return true;
}

[[nodiscard]] static bool SelectInput(const NWB::Path& path, const bool directory,
    const Assets::DiscoveredNwbFileVector& files, Vector<u8, Assets::ScratchArena>& selected,
    Vector<usize, Assets::ScratchArena>& order,
    Assets::ScratchArena& scratchArena){
    Assets::ScratchString input = PathToString(scratchArena, path);
#if defined(GLB_PLATFORM_WINDOWS)
    CanonicalizeTextInPlace(input);
#else
    if(!directory)
        CanonicalizeTextInPlace(input);
#endif
    bool matched = false;
    for(usize index = 0u; index < files.size(); ++index){
        const auto& file = files[index];
        if(directory){
            Assets::ScratchString physical = PathToString(scratchArena, file.filePath.lexically_normal());
#if defined(GLB_PLATFORM_WINDOWS)
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
    ErrorCode error;
    const AStringView repoText = options.repoRoot.empty() ? AStringView(".") : AStringView(options.repoRoot);
    NWB::Path repoRoot = AbsolutePath(NWB::Path(m_arena, repoText), error);
    if(error){
        NWB_LOGGER_ERROR(GLB_TEXT("DependencyComputer: failed to resolve repository root"));
        return false;
    }
    repoRoot = repoRoot.lexically_normal();
    Assets::CookVector<Assets::ResolvedAssetRoot> roots(m_arena);
    if(!ResolveRoots(options, repoRoot, roots, scratchArena))
        return false;
    Assets::DiscoveredNwbFileVector files(m_arena);
    if(!Assets::DiscoverFilesWithExtension(roots, Assets::s_NwbExtension, files, scratchArena))
        return false;
    Assets::AssetVector<Name> virtualPaths(m_arena);
    virtualPaths.reserve(files.size());
    for(const auto& file : files){
        Name virtualPath;
        if(!Assets::BuildDerivedAssetVirtualPath(file.assetRoot, file.virtualRoot, file.filePath, virtualPath, scratchArena))
            return false;
        virtualPaths.push_back(virtualPath);
    }
    m_repoRoot = Move(repoRoot);
    m_roots = Move(roots);
    m_files = Move(files);
    m_virtualPaths = Move(virtualPaths);
    return true;
}

bool DependencyProviderCatalog::selectInputs(const PipelineOptions& options,
    Vector<u8, NWB::Core::Alloc::ScratchArena>& outSelected,
    Vector<usize, NWB::Core::Alloc::ScratchArena>& outOrder,
    NWB::Core::Alloc::ScratchArena& scratchArena)const{
    using namespace __hidden_dependency_provider_catalog;
    Vector<u8, Assets::ScratchArena> selected(m_files.size(), u8(0), outSelected.get_allocator().arena());
    Vector<usize, Assets::ScratchArena> order(outOrder.get_allocator().arena());
    order.reserve(m_files.size());
    for(const auto& input : options.inputs){
        ErrorCode error;
        NWB::Path path(m_arena);
        if(!ResolveAbsolutePath(m_repoRoot, AStringView(input), path, error)){
            NWB_LOGGER_ERROR(GLB_TEXT("DependencyComputer: failed to resolve input '{}'"), StringConvert(input));
            return false;
        }
        const bool directory = IsDirectory(path, error);
        if(error || (!directory && !IsRegularFile(path, error))){
            NWB_LOGGER_ERROR(GLB_TEXT("DependencyComputer: input is not a file or directory '{}'"), PathToString<tchar>(path));
            return false;
        }
        bool contained = !directory;
        if(directory){
            Assets::ScratchString inputPath = PathToString(scratchArena, path);
#if defined(GLB_PLATFORM_WINDOWS)
            CanonicalizeTextInPlace(inputPath);
#endif
            for(const auto& root : m_roots){
                Assets::ScratchString rootPath = PathToString(scratchArena, root.path);
#if defined(GLB_PLATFORM_WINDOWS)
                CanonicalizeTextInPlace(rootPath);
#endif
                if(IsPathPrefixText(AStringView(rootPath), AStringView(inputPath))){
                    contained = true;
                    break;
                }
            }
        }
        if(!contained || (!SelectInput(path, directory, m_files, selected, order, scratchArena) && !directory)){
            NWB_LOGGER_ERROR(GLB_TEXT("DependencyComputer: input selects no .nwb asset within the asset roots '{}'")
                , PathToString<tchar>(path)
            );
            return false;
        }
    }
    outSelected.swap(selected);
    outOrder.swap(order);
    return true;
}

bool DependencyProviderCatalog::read(const usize index, NWB::Core::Metascript::Document& outDocument,
    NWB::Core::Alloc::ScratchArena& scratchArena)const{
    using namespace __hidden_dependency_provider_catalog;
    if(index >= m_files.size())
        return false;
    Assets::ScratchString text(scratchArena);
    return Assets::ParseMetadataDocumentText(
        m_files[index].filePath,
        s_DiagnosticPrefix,
        text,
        outDocument,
        [&](const AStringView source){ return outDocument.parse(source); }
    );
}

bool DependencyProviderCatalog::resolve(const Name& virtualPath, const Name& assetType, usize& outIndex,
    NWB::Core::Alloc::ScratchArena& scratchArena)const{
    using namespace __hidden_dependency_provider_catalog;
    if(!virtualPath || !assetType)
        return false;
    usize matched = Limit<usize>::s_Max;
    for(usize index = 0u; index < m_files.size(); ++index){
        if(m_virtualPaths[index] != virtualPath)
            continue;
        Core::Metascript::Document document(m_arena);
        if(!read(index, document, scratchArena))
            return false;
        if(Name(document.assetType()) != assetType)
            continue;
        if(document.declarations().size() != 1u){
            NWB_LOGGER_ERROR(GLB_TEXT("DependencyComputer: provider '{}' declares multiple assets for '{}'")
                , PathToString<tchar>(m_files[index].filePath)
                , StringConvert(virtualPath)
            );
            return false;
        }
        if(matched != Limit<usize>::s_Max){
            NWB_LOGGER_ERROR(GLB_TEXT("DependencyComputer: multiple physical providers for '{}'"), StringConvert(virtualPath));
            return false;
        }
        matched = index;
    }
    if(matched == Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(GLB_TEXT("DependencyComputer: no typed '{}' provider for '{}'")
            , StringConvert(assetType)
            , StringConvert(virtualPath)
        );
        return false;
    }
    outIndex = matched;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

