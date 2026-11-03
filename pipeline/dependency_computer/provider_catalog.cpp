// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "provider_catalog.h"

#include <core/assets/bunch/cook.h>
#include <core/assets/cook_paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEPENDENCY_COMPUTER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_dependency_provider_catalog{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 MatchesExpectedAssetType(
    const Name& actualType,
    const Name& expectedType,
    const Assets::CookDependencyRegistry* const dependencyRegistry
)noexcept{
    if(!expectedType || actualType == expectedType)
        return true;
    return dependencyRegistry && dependencyRegistry->matchesAssetType(actualType, expectedType);
}

[[nodiscard]] static Optional<Name> ExtractParentVirtualPath(const AStringView virtualPathText){
    const auto parentText = ExtractParentVirtualPathText(virtualPathText);
    if(!parentText)
        return {};

    const auto parentName = Name::FromText(*parentText);
    if(!parentName || !*parentName)
        return {};
    return Optional<Name>(*parentName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DependencyProviderCatalog::discover(const PipelineOptions& options, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_dependency_provider_catalog;
    auto resolvedRepoRoot = ResolvePipelineRepoRoot(AStringView(options.repoRoot), NWB_TEXT("DependencyComputer"), m_arena);
    if(!resolvedRepoRoot)
        return false;
    Path repoRoot = Move(*resolvedRepoRoot);
    auto roots = ResolvePipelineAssetRoots(
        repoRoot,
        options.inputs,
        options.assetRoots,
        Assets::AssetRootDuplicatePolicy::HostFilesystem,
        NWB_TEXT("DependencyComputer"),
        NWB_TEXT("no asset roots available for dependency resolution"),
        scratchArena
    );
    if(!roots)
        return false;
    auto files = Assets::DiscoverFilesWithExtension(*roots, Assets::s_NwbExtension, m_arena, scratchArena);
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

Expected<DependencyInputSelection> DependencyProviderCatalog::selectInputs(
    const PipelineOptions& options,
    const Core::Assets::CookDependencyRegistry& dependencyRegistry,
    Core::Alloc::ScratchArena& scratchArena
)const{
    using namespace __hidden_dependency_provider_catalog;
    DependencyInputSelection selection(scratchArena);
    auto& selected = selection.selected;
    auto& order = selection.order;
    selected.resize(m_files.size(), u8(0));
    order.reserve(m_files.size());

    if(options.inputs.empty()){
        const Assets::DefaultCookEntryAssetDescriptor& defaultEntry = dependencyRegistry.defaultEntryAsset();
        if(!defaultEntry.virtualPath){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: no default entry asset registered"));
            return MakeUnexpected(Failure{});
        }
        const auto projectProvider = resolve(
            defaultEntry.virtualPath,
            defaultEntry.assetType,
            defaultEntry.virtualPathText,
            &dependencyRegistry,
            scratchArena
        );
        if(!projectProvider){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: no project root '{}' found in asset roots"), ConvertString(defaultEntry.virtualPathText));
            return MakeUnexpected(Failure{});
        }
        selected[*projectProvider] = 1u;
        order.push_back(*projectProvider);
        return selection;
    }

    const auto selectionResult = Assets::SelectDiscoveredNwbInputs(m_repoRoot, m_roots, m_files, options.inputs, true, scratchArena, selected, &order);
    if(!selectionResult){
        LogCookInputSelectionFailure(selectionResult.error(), NWB_TEXT("DependencyComputer"));
        return MakeUnexpected(Failure{});
    }
    return selection;
}

Expected<Core::Metascript::Document> DependencyProviderCatalog::read(const usize index, Core::Alloc::ScratchArena& scratchArena)const{
    using namespace __hidden_dependency_provider_catalog;
    NWB_ASSERT(index < m_files.size());
    Assets::ScratchString text(scratchArena);
    Core::Metascript::Document document(m_arena);
    if(!Assets::ParseMetadataDocumentText(m_files[index].filePath, s_DiagnosticPrefix, [&](const AStringView source){ return document.parse(source); }, text, document))
        return MakeUnexpected(Failure{});
    return document;
}

Expected<usize> DependencyProviderCatalog::resolve(
    const Name& virtualPath,
    const Name& assetType,
    const AStringView virtualPathTextHint,
    const Core::Assets::CookDependencyRegistry* const dependencyRegistry,
    Core::Alloc::ScratchArena& scratchArena
)const{
    using namespace __hidden_dependency_provider_catalog;
    if(!virtualPath)
        return MakeUnexpected(Failure{});

    const AStringView virtualPathText = ResolveDependencyVirtualPathText(virtualPath, virtualPathTextHint);
    const Optional<Name> parentVirtualPath = ExtractParentVirtualPath(virtualPathText);

    usize matched = s_InvalidProviderIndex;
    const auto recordMatch = [&](const usize index) -> b8{
        return RecordDependencyProviderMatch(virtualPath, index, matched);
    };

    for(usize index = 0u; index < m_files.size(); ++index){
        if(m_virtualPaths[index] == virtualPath){
            const auto document = read(index, scratchArena);
            if(!document)
                return MakeUnexpected(Failure{});

            const auto hasBunch = Assets::AssetsBunchCook::HasAssetBunchDeclaration(*document);
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

            const auto* assetDeclaration = document->findDeclaration(Assets::s_MetadataAssetVariable);
            if(!assetDeclaration)
                continue;
            const auto documentAssetType = Name::FromText(AStringView(assetDeclaration->type.data(), assetDeclaration->type.size()));
            if(!documentAssetType)
                return MakeUnexpected(Failure{});
            if(!MatchesExpectedAssetType(*documentAssetType, assetType, dependencyRegistry))
                continue;

            const auto hasComposite = Assets::AssetsBunchCook::HasCompositeAssetDeclarations(*document);
            if(!hasComposite)
                return MakeUnexpected(Failure{});
            if(*hasComposite){
                const auto expanded = Assets::AssetsBunchCook::ExpandCompositeAsset(
                    m_files[index].assetRoot,
                    m_files[index].virtualRoot,
                    m_files[index].filePath,
                    *document,
                    scratchArena
                );
                if(!expanded)
                    return MakeUnexpected(Failure{});
                if(!recordMatch(index))
                    return MakeUnexpected(Failure{});
                continue;
            }

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
            const auto expanded = TryExpandBunchOrCompositeDocument(m_files[index], *document, scratchArena);
            if(!expanded)
                return MakeUnexpected(Failure{});
            if(!*expanded)
                continue;

            b8 bunchItemMatched = false;
            for(const Assets::ExpandedAssetMetadata& item : **expanded){
                if(item.virtualPath != virtualPath)
                    continue;
                if(!MatchesExpectedAssetType(item.assetType, assetType, dependencyRegistry))
                    continue;
                bunchItemMatched = true;
                break;
            }
            if(bunchItemMatched && !recordMatch(index))
                return MakeUnexpected(Failure{});
            continue;
        }
    }

    if(matched == s_InvalidProviderIndex){
        if(assetType)
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: no typed '{}' provider for '{}'"), ConvertString(assetType), ConvertString(virtualPathText));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: no provider for '{}'"), ConvertString(virtualPath));
        return MakeUnexpected(Failure{});
    }
    return matched;
}

Expected<Optional<Core::Assets::ExpandedAssetMetadataVector>> TryExpandBunchOrCompositeDocument(
    const Core::Assets::DiscoveredNwbFile& file,
    const Core::Metascript::Document& document,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_dependency_provider_catalog;

    auto expanded = Assets::AssetsBunchCook::ExpandAssetBunchForAssetCook(Assets::AssetBunchExpandContext{
        .assetRoot = file.assetRoot,
        .virtualRoot = file.virtualRoot,
        .nwbFilePath = file.filePath,
        .doc = document,
        .scratchArena = scratchArena,
    });
    if(!expanded){
        if(expanded.error() == Assets::AssetBunchExpandFailure::Unsupported)
            return Optional<Assets::ExpandedAssetMetadataVector>{};
        return MakeUnexpected(Failure{});
    }
    return Optional<Assets::ExpandedAssetMetadataVector>(Move(*expanded));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEPENDENCY_COMPUTER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

