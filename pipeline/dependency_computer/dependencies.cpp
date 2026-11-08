// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "provider_catalog.h"
#include "dependency_path_hints.h"

#include <core/assets/bunch/cook.h>
#include <core/assets/cook_dependency_registry.h>
#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEPENDENCY_COMPUTER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetVector<Core::Assets::AssetString>> ComputeAssetDependencies(const PipelineOptions& options, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    namespace Assets = Core::Assets;

    Assets::CookDependencyRegistry dependencyRegistry(arena);
    if(!Assets::RegisterAutoCollectedCookDependencyTypes(dependencyRegistry, scratchArena))
        return MakeUnexpected(Failure{});

    DependencyProviderCatalog catalog(arena);
    if(!catalog.discover(options, scratchArena))
        return MakeUnexpected(Failure{});
    auto selection = catalog.selectInputs(options, dependencyRegistry, scratchArena);
    if(!selection)
        return MakeUnexpected(Failure{});

    const auto& files = catalog.files();
    Assets::AssetVector<Assets::AssetString> candidate(arena);
    if(!options.inputs.empty()){
        candidate.reserve(options.inputs.size());
        for(const auto& input : options.inputs)
            candidate.emplace_back(input, arena);
    }
    else{
        candidate.reserve(selection->order.size());
        for(const usize index : selection->order)
            candidate.emplace_back(PathToString(arena, files[index].filePath.lexicallyNormal()));
    }

    const Span<u8> included = selection->selected;
    Vector<u8, Assets::ScratchArena> processed(files.size(), u8(0), scratchArena);
    Vector<usize, Assets::ScratchArena> fileQueue(selection->order, scratchArena);
    fileQueue.reserve(files.size());
    DependencyPathHints knownVirtualPathTexts(arena, scratchArena);
    knownVirtualPathTexts.reserve(files.size());

    const auto enqueueDependency = [&](const Name& depVirtualPath, const Name& expectedAssetType) -> b8{
        if(!depVirtualPath)
            return false;
        const AStringView hint = knownVirtualPathTexts.lookup(depVirtualPath);
        const auto provider = catalog.resolve(depVirtualPath, expectedAssetType, hint, &dependencyRegistry, scratchArena);
        if(!provider)
            return false;
        if(!included[*provider]){
            candidate.emplace_back(PathToString(arena, files[*provider].filePath.lexicallyNormal()));
            included[*provider] = 1u;
        }
        if(!processed[*provider])
            fileQueue.push_back(*provider);
        return true;
    };

    class DependencyCollector final : public Assets::ICookDependencyCollector{
    public:
        explicit DependencyCollector(const Function<b8(const Name&, const Name&)>& enqueue)noexcept
            : m_enqueue(enqueue)
        {}


    public:
        [[nodiscard]] virtual b8 addAssetDependency(const Name& virtualPath, const Name& expectedAssetType = s_NameNone)override{
            return m_enqueue(virtualPath, expectedAssetType);
        }


    private:
        const Function<b8(const Name&, const Name&)>& m_enqueue;
    };

    const Function<b8(const Name&, const Name&)> enqueueCallback(enqueueDependency);
    DependencyCollector collector(enqueueCallback);

    const auto processAssetItem = [&](
        const usize fileIndex,
        const Name& assetType,
        const Name& virtualPath,
        const Core::Metascript::Value& assetValue,
        const Core::Metascript::Document* documentOrNull
    ) -> b8{
        const auto& file = files[fileIndex];
        const Assets::CookDependencyItemContext itemContext{
            .assetRoot = file.assetRoot,
            .virtualRoot = file.virtualRoot,
            .nwbFilePath = file.filePath,
            .assetType = assetType,
            .virtualPath = virtualPath,
            .asset = assetValue,
            .document = documentOrNull,
            .arena = arena,
            .scratchArena = scratchArena,
        };
        return dependencyRegistry.extractDependencies(itemContext, collector);
    };

    for(usize queueCursor = 0u; queueCursor < fileQueue.size(); ++queueCursor){
        const usize fileIndex = fileQueue[queueCursor];
        if(processed[fileIndex])
            continue;
        processed[fileIndex] = 1u;

        const auto document = catalog.read(fileIndex, scratchArena);
        if(!document)
            return MakeUnexpected(Failure{});
        knownVirtualPathTexts.collect(*document);

        const auto expandedAssets = TryExpandBunchOrCompositeDocument(files[fileIndex], *document, scratchArena);
        if(!expandedAssets)
            return MakeUnexpected(Failure{});
        if(*expandedAssets){
            for(const Assets::ExpandedAssetMetadata& expanded : **expandedAssets)
                knownVirtualPathTexts.collect(expanded.value);
            for(const Assets::ExpandedAssetMetadata& expanded : **expandedAssets){
                if(!processAssetItem(fileIndex, expanded.assetType, expanded.virtualPath, expanded.value, nullptr))
                    return MakeUnexpected(Failure{});
            }
            continue;
        }

        if(document->declarations().size() != 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: provider '{}' declares multiple assets without an asset_bunch"), PathToString<tchar>(files[fileIndex].filePath));
            return MakeUnexpected(Failure{});
        }
        const auto* assetDeclaration = document->findDeclaration(Assets::s_MetadataAssetVariable);
        if(!assetDeclaration){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: meta '{}' is missing asset declaration 'asset'"), PathToString<tchar>(files[fileIndex].filePath));
            return MakeUnexpected(Failure{});
        }
        const auto documentAssetType = Name::FromText(AStringView(assetDeclaration->type.data(), assetDeclaration->type.size()));
        if(!documentAssetType)
            return MakeUnexpected(Failure{});
        const auto assetValue = Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(files[fileIndex].filePath, *document, s_DiagnosticPrefix);
        if(!assetValue)
            return MakeUnexpected(Failure{});
        if(!processAssetItem(fileIndex, *documentAssetType, catalog.virtualPaths()[fileIndex], **assetValue, &*document))
            return MakeUnexpected(Failure{});
    }

    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEPENDENCY_COMPUTER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

