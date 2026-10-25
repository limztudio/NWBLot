// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "provider_catalog.h"

#include <impl/assets_csg/cook.h>
#include <impl/assets_scene/asset.h>

#include <core/assets/bunch/cook.h>
#include <core/assets/cook_paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_dependency_provider_catalog{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
namespace Assets = Core::Assets;

inline constexpr AStringView s_DiagnosticPrefix = "DependencyComputer";
inline constexpr Name s_AssetBunchTypeName("asset_bunch");
inline constexpr AStringView s_CsgShapeFileSuffix = "/shape";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<bool, NameTextError::Enum> HasAssetBunchDeclaration(const Core::Metascript::Document& doc){
    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        const AStringView typeName(declaration.type.data(), declaration.type.size());
        const auto parsedName = ToName(typeName);
        if(!parsedName)
            return MakeUnexpected(parsedName.error());
        if(*parsedName && **parsedName == s_AssetBunchTypeName)
            return true;
    }
    return false;
}

[[nodiscard]] static Optional<Name> ExtractParentVirtualPath(const AStringView virtualPathText){
    const usize lastSlash = virtualPathText.rfind('/');
    if(lastSlash == AStringView::npos || lastSlash == 0u)
        return {};

    const auto parentName = Name::FromText(virtualPathText.substr(0u, lastSlash));
    if(!parentName || !*parentName)
        return {};
    return Optional<Name>(*parentName);
}

[[nodiscard]] static Optional<Name> BuildCsgShapeFileVirtualPath(const AStringView virtualPathText, Assets::ScratchArena& scratchArena){
    if(virtualPathText.empty())
        return {};

    Assets::ScratchString candidateText(scratchArena);
    candidateText.reserve(virtualPathText.size() + s_CsgShapeFileSuffix.size());
    candidateText.append(virtualPathText.data(), virtualPathText.size());
    candidateText.append(s_CsgShapeFileSuffix.data(), s_CsgShapeFileSuffix.size());

    const auto shapeFileVirtualPath = Name::FromText(AStringView(candidateText));
    if(!shapeFileVirtualPath || !*shapeFileVirtualPath)
        return {};
    return Optional<Name>(*shapeFileVirtualPath);
}

[[nodiscard]] static bool SelectInput(const NWB::Path& path, const bool directory, const Span<const Assets::DiscoveredNwbFile> files, Assets::ScratchArena& scratchArena, const Span<u8> selected, Vector<usize, Assets::ScratchArena>& order){
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


DependencyProviderCatalog::DependencyProviderCatalog(NWB::Core::Assets::AssetArena& arena)noexcept
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
    const auto& sources = options.assetRoots.empty() ? options.inputs : options.assetRoots;
    auto roots = Assets::ResolveAssetRoots(repoRoot, sources, options.assetRoots.empty(), Assets::AssetRootDuplicatePolicy::HostFilesystem, scratchArena);
    if(!roots){
        const auto& error = roots.error();
        if(error.reason == Assets::AssetRootResolutionFailure::InspectInput)
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: failed to inspect input '{}'"), PathToString<tchar>(error.inputPath));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: failed to resolve asset root from '{}'"), ConvertString(sources[error.sourceIndex]));
        return false;
    }
    if(roots->empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: no asset roots available for dependency resolution"));
        return false;
    }
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

Expected<DependencyInputSelection> DependencyProviderCatalog::selectInputs(const PipelineOptions& options, NWB::Core::Alloc::ScratchArena& scratchArena)const{
    using namespace __hidden_dependency_provider_catalog;
    DependencyInputSelection selection(scratchArena);
    auto& selected = selection.selected;
    auto& order = selection.order;
    selected.resize(m_files.size(), u8(0));
    order.reserve(m_files.size());

    if(options.inputs.empty()){
        const auto projectProvider = resolve(
            Impl::ProjectAssetDefaults::s_DefaultProjectVirtualPath,
            Impl::ProjectAsset::s_AssetTypeName,
            Impl::ProjectAssetDefaults::s_DefaultProjectVirtualPathText,
            scratchArena
        );
        if(!projectProvider){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: no project root '{}' found in asset roots"), ConvertString(Impl::ProjectAssetDefaults::s_DefaultProjectVirtualPathText));
            return MakeUnexpected(Failure{});
        }
        selected[*projectProvider] = 1u;
        order.push_back(*projectProvider);
        return selection;
    }

    for(const auto& input : options.inputs){
        const auto resolved = ResolveAbsolutePath(m_arena, m_repoRoot, AStringView(input));
        if(!resolved){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: failed to resolve input '{}'"), ConvertString(input));
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
        if(!contained || (!SelectInput(path, directory, m_files, scratchArena, selected, order) && !directory)){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: input selects no .nwb asset within the asset roots '{}'"), PathToString<tchar>(path));
            return MakeUnexpected(Failure{});
        }
    }
    return selection;
}

Expected<NWB::Core::Metascript::Document> DependencyProviderCatalog::read(const usize index, NWB::Core::Alloc::ScratchArena& scratchArena)const{
    using namespace __hidden_dependency_provider_catalog;
    NWB_ASSERT(index < m_files.size());
    Assets::ScratchString text(scratchArena);
    Core::Metascript::Document document(m_arena);
    if(!Assets::ParseMetadataDocumentText(m_files[index].filePath, s_DiagnosticPrefix, [&](const AStringView source){ return document.parse(source); }, text, document))
        return MakeUnexpected(Failure{});
    return document;
}

Expected<usize> DependencyProviderCatalog::resolve(const Name& virtualPath, const Name& assetType, const AStringView virtualPathTextHint, NWB::Core::Alloc::ScratchArena& scratchArena)const{
    using namespace __hidden_dependency_provider_catalog;
    if(!virtualPath)
        return MakeUnexpected(Failure{});

    const AStringView virtualPathText = !virtualPathTextHint.empty() ? virtualPathTextHint : virtualPath.resolvedText();
    const Optional<Name> parentVirtualPath = ExtractParentVirtualPath(virtualPathText);
    const Optional<Name> csgShapeFileVirtualPath = BuildCsgShapeFileVirtualPath(virtualPathText, scratchArena);

    usize matched = Limit<usize>::s_Max;
    const auto recordMatch = [&](const usize index) -> bool{
        if(matched != Limit<usize>::s_Max && matched != index){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: multiple physical providers for '{}'"), ConvertString(virtualPath));
            return false;
        }
        matched = index;
        return true;
    };

    for(usize index = 0u; index < m_files.size(); ++index){
        if(m_virtualPaths[index] == virtualPath){
            const auto document = read(index, scratchArena);
            if(!document)
                return MakeUnexpected(Failure{});

            const auto hasBunch = HasAssetBunchDeclaration(*document);
            if(!hasBunch)
                return MakeUnexpected(Failure{});
            if(*hasBunch){
                if(assetType){
                    NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: provider '{}' declares multiple assets for '{}'"), PathToString<tchar>(m_files[index].filePath), ConvertString(virtualPath));
                    return MakeUnexpected(Failure{});
                }
                const auto expanded = Assets::AssetsBunchCook::ExpandAssetBunch(m_files[index].assetRoot, m_files[index].virtualRoot, m_files[index].filePath, *document, scratchArena);
                if(!expanded)
                    return MakeUnexpected(Failure{});
                if(!recordMatch(index))
                    return MakeUnexpected(Failure{});
                continue;
            }

            const auto* assetDeclaration = Assets::FindMetadataAssetDeclaration(*document);
            if(!assetDeclaration)
                continue;
            const auto documentAssetType = Name::FromText(AStringView(assetDeclaration->type.data(), assetDeclaration->type.size()));
            if(!documentAssetType)
                return MakeUnexpected(Failure{});
            if(assetType && *documentAssetType != assetType)
                continue;
            if(document->declarations().size() != 1u){
                NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: provider '{}' declares multiple assets for '{}'"), PathToString<tchar>(m_files[index].filePath), ConvertString(virtualPath));
                return MakeUnexpected(Failure{});
            }
            if(!recordMatch(index))
                return MakeUnexpected(Failure{});
            continue;
        }

        if(parentVirtualPath && m_virtualPaths[index] == *parentVirtualPath){
            const auto document = read(index, scratchArena);
            if(!document)
                return MakeUnexpected(Failure{});
            const auto hasBunch = HasAssetBunchDeclaration(*document);
            if(!hasBunch)
                return MakeUnexpected(Failure{});
            if(!*hasBunch)
                continue;

            const auto expanded = Assets::AssetsBunchCook::ExpandAssetBunch(m_files[index].assetRoot, m_files[index].virtualRoot, m_files[index].filePath, *document, scratchArena);
            if(!expanded)
                return MakeUnexpected(Failure{});

            bool bunchItemMatched = false;
            for(const Assets::ExpandedAssetMetadata& item : *expanded){
                if(item.virtualPath != virtualPath)
                    continue;
                if(assetType && item.assetType != assetType)
                    continue;
                bunchItemMatched = true;
                break;
            }
            if(bunchItemMatched && !recordMatch(index))
                return MakeUnexpected(Failure{});
            continue;
        }

        if(csgShapeFileVirtualPath && m_virtualPaths[index] == *csgShapeFileVirtualPath){
            if(assetType && assetType != Impl::AssetsCsgCook::s_CsgShapeAssetTypeName)
                continue;
            const auto document = read(index, scratchArena);
            if(!document)
                return MakeUnexpected(Failure{});
            if(document->declarations().size() != 1u)
                continue;
            const auto* assetDeclaration = Assets::FindMetadataAssetDeclaration(*document);
            if(!assetDeclaration)
                continue;
            const auto documentAssetType = Name::FromText(AStringView(assetDeclaration->type.data(), assetDeclaration->type.size()));
            if(!documentAssetType || *documentAssetType != Impl::AssetsCsgCook::s_CsgShapeAssetTypeName)
                continue;
            const auto csgEntry = Impl::AssetsCsgCook::ParseCsgShapeCookMetadata(m_arena, m_files[index].filePath, *document, scratchArena);
            if(!csgEntry)
                return MakeUnexpected(Failure{});
            if(csgEntry->shapeName == virtualPath && !recordMatch(index))
                return MakeUnexpected(Failure{});
        }
    }

    if(matched == Limit<usize>::s_Max){
        if(assetType)
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: no typed '{}' provider for '{}'"), ConvertString(assetType), ConvertString(virtualPath));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: no provider for '{}'"), ConvertString(virtualPath));
        return MakeUnexpected(Failure{});
    }
    return matched;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

