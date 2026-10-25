// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "dependencies.h"
#include "provider_catalog.h"

#include <impl/assets_graphics/mesh/names.h>
#include <impl/assets_subsystem/asset.h>

#include <core/assets/bunch/cook.h>
#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_dependency_computer_dependencies{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
namespace Assets = Core::Assets;

inline constexpr Name s_AssetBunchTypeName("asset_bunch");

using KnownVirtualPathTextMap = HashMap<Name, Assets::AssetString, Assets::ScratchArena, Hasher<Name>, EqualTo<Name>>;


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

static void RecordVirtualPathText(const AStringView rawText, Assets::AssetArena& arena, KnownVirtualPathTextMap& outMap){
    const AStringView trimmed = TrimView(rawText);
    if(trimmed.empty() || trimmed.find('/') == AStringView::npos)
        return;

    const auto name = Name::FromText(trimmed);
    if(!name || !*name)
        return;

    if(outMap.find(*name) == outMap.end())
        outMap.emplace(*name, Assets::AssetString(trimmed, arena));
}

static void CollectValueVirtualPathTexts(const Core::Metascript::Value& value, Assets::AssetArena& arena, KnownVirtualPathTextMap& outMap){
    if(value.isString()){
        const Core::Metascript::MStringView str = value.asString();
        RecordVirtualPathText(AStringView(str.data(), str.size()), arena, outMap);
        return;
    }
    if(value.isList()){
        for(const Core::Metascript::Value& item : value.asList())
            CollectValueVirtualPathTexts(item, arena, outMap);
        return;
    }
    if(value.isMap()){
        for(const auto& [key, fieldValue] : value.asMap()){
            RecordVirtualPathText(AStringView(key.data(), key.size()), arena, outMap);
            CollectValueVirtualPathTexts(fieldValue, arena, outMap);
        }
    }
}

static void CollectDocumentVirtualPathTexts(const Core::Metascript::Document& doc, Assets::AssetArena& arena, KnownVirtualPathTextMap& outMap){
    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        const Core::Metascript::MStringView varView(declaration.variable.data(), declaration.variable.size());
        if(const Core::Metascript::Value* value = doc.findVariable(varView))
            CollectValueVirtualPathTexts(*value, arena, outMap);
    }
}

[[nodiscard]] static AStringView LookupVirtualPathTextHint(const Name& virtualPath, const KnownVirtualPathTextMap& knownTexts)noexcept{
    const auto found = knownTexts.find(virtualPath);
    if(found != knownTexts.end())
        return AStringView(found.value());
    return virtualPath.resolvedText();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString>> ComputeAssetDependencies(const PipelineOptions& options, NWB::Core::Assets::AssetArena& arena, NWB::Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_dependency_computer_dependencies;
    using namespace NWB;
    namespace Assets = Core::Assets;

    Assets::CookDependencyRegistry dependencyRegistry(arena);
    if(!Assets::RegisterAutoCollectedCookDependencyTypes(dependencyRegistry, scratchArena))
        return MakeUnexpected(Failure{});

    DependencyProviderCatalog catalog(arena);
    if(!catalog.discover(options, scratchArena))
        return MakeUnexpected(Failure{});
    auto selection = catalog.selectInputs(options, scratchArena);
    if(!selection)
        return MakeUnexpected(Failure{});

    const auto& files = catalog.m_files;
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
    KnownVirtualPathTextMap knownVirtualPathTexts(0, Hasher<Name>(), EqualTo<Name>(), scratchArena);

    RecordVirtualPathText(Impl::AssetsGraphicsMesh::s_SharedMeshShaderText, arena, knownVirtualPathTexts);
    RecordVirtualPathText(Impl::BuiltInSubsystems::s_SkinningText, arena, knownVirtualPathTexts);
    RecordVirtualPathText(Impl::BuiltInSubsystems::s_TransparentText, arena, knownVirtualPathTexts);
    RecordVirtualPathText(Impl::BuiltInSubsystems::s_RefractiveText, arena, knownVirtualPathTexts);
    RecordVirtualPathText(Impl::BuiltInSubsystems::s_CsgText, arena, knownVirtualPathTexts);

    const auto enqueueDependency = [&](const Name& depVirtualPath, const Name& expectedAssetType) -> bool{
        if(!depVirtualPath)
            return false;
        const AStringView hint = LookupVirtualPathTextHint(depVirtualPath, knownVirtualPathTexts);
        const auto provider = catalog.resolve(depVirtualPath, expectedAssetType, hint, scratchArena);
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
        explicit DependencyCollector(const Function<bool(const Name&, const Name&)>& enqueue)noexcept
            : m_enqueue(enqueue)
        {}


    public:
        [[nodiscard]] virtual bool addAssetDependency(const Name& virtualPath, const Name& expectedAssetType = s_NameNone)override{
            return m_enqueue(virtualPath, expectedAssetType);
        }

        [[nodiscard]] virtual bool addSubsystemDependency(const Name& subsystemVirtualPath)override{
            return m_enqueue(subsystemVirtualPath, Impl::Subsystem::AssetTypeName());
        }


    private:
        const Function<bool(const Name&, const Name&)>& m_enqueue;
    };

    const Function<bool(const Name&, const Name&)> enqueueCallback(enqueueDependency);
    DependencyCollector collector(enqueueCallback);

    const auto processAssetItem = [&](
        const usize fileIndex,
        const Name& assetType,
        const Name& virtualPath,
        const Core::Metascript::Value& assetValue,
        const Core::Metascript::Document* documentOrNull
    ) -> bool{
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
        CollectDocumentVirtualPathTexts(*document, arena, knownVirtualPathTexts);

        const auto hasBunch = HasAssetBunchDeclaration(*document);
        if(!hasBunch)
            return MakeUnexpected(Failure{});
        if(*hasBunch){
            const auto& file = files[fileIndex];
            const auto expandedAssets = Assets::AssetsBunchCook::ExpandAssetBunch(file.assetRoot, file.virtualRoot, file.filePath, *document, scratchArena);
            if(!expandedAssets)
                return MakeUnexpected(Failure{});
            for(const Assets::ExpandedAssetMetadata& expanded : *expandedAssets)
                CollectValueVirtualPathTexts(expanded.value, arena, knownVirtualPathTexts);
            for(const Assets::ExpandedAssetMetadata& expanded : *expandedAssets){
                if(!processAssetItem(fileIndex, expanded.assetType, expanded.virtualPath, expanded.value, nullptr))
                    return MakeUnexpected(Failure{});
            }
            continue;
        }

        if(document->declarations().size() != 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: provider '{}' declares multiple assets without an asset_bunch"), PathToString<tchar>(files[fileIndex].filePath));
            return MakeUnexpected(Failure{});
        }
        const auto* assetDeclaration = Assets::FindMetadataAssetDeclaration(*document);
        if(!assetDeclaration){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: meta '{}' is missing asset declaration 'asset'"), PathToString<tchar>(files[fileIndex].filePath));
            return MakeUnexpected(Failure{});
        }
        const auto documentAssetType = Name::FromText(AStringView(assetDeclaration->type.data(), assetDeclaration->type.size()));
        if(!documentAssetType)
            return MakeUnexpected(Failure{});
        const auto assetValue = Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(files[fileIndex].filePath, *document, "DependencyComputer");
        if(!assetValue)
            return MakeUnexpected(Failure{});
        if(!processAssetItem(fileIndex, *documentAssetType, catalog.m_virtualPaths[fileIndex], **assetValue, &*document))
            return MakeUnexpected(Failure{});
    }

    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

