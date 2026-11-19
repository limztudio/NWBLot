// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/bunch/cook.h>
#include <pipeline/cook_workspace.h>
#include <core/assets/input_list.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CookDependencyAccess final : NoCopy{
public:
    explicit CookDependencyAccess(AssetCookWorkspace& workspace) : m_workspace(workspace), registry(workspace.arena){}
    [[nodiscard]] b8 registerTypes(){ return Core::Assets::RegisterAutoCollectedCookDependencyTypes(registry, m_workspace.scratch); }

    [[nodiscard]] Expected<Core::Metascript::Document> read(const Core::Assets::DiscoveredNwbFile& file, const AStringView prefix)const{
        Core::Metascript::Document document(m_workspace.arena);
        Core::Assets::ScratchString source(m_workspace.scratch);
        if(!Core::Assets::ParseMetadataDocumentText(file.filePath, prefix, [&](const AStringView text){ return document.parse(text); }, source, document))
            return MakeUnexpected(Failure{});
        return document;
    }

    [[nodiscard]] Expected<Core::Assets::ExpandedAssetMetadataVector, Core::Assets::AssetBunchExpandFailure::Enum> expand(const Core::Assets::DiscoveredNwbFile& file, const Core::Metascript::Document& document)const{
        return Core::Assets::AssetsBunchCook::ExpandAssetBunchForAssetCook({file.assetRoot, file.virtualRoot, file.filePath, document, m_workspace.scratch});
    }
    [[nodiscard]] b8 expandBunch(const Core::Assets::DiscoveredNwbFile& file, const Core::Metascript::Document& document)const{
        return Core::Assets::AssetsBunchCook::ExpandAssetBunch(file.assetRoot, file.virtualRoot, file.filePath, document, m_workspace.scratch).has_value();
    }
    [[nodiscard]] b8 expandComposite(const Core::Assets::DiscoveredNwbFile& file, const Core::Metascript::Document& document)const{
        return Core::Assets::AssetsBunchCook::ExpandCompositeAsset(file.assetRoot, file.virtualRoot, file.filePath, document, m_workspace.scratch).has_value();
    }

    template<typename Visit>
    [[nodiscard]] b8 extract(
        const Core::Assets::DiscoveredNwbFile& file,
        const Name type,
        const Name path,
        const Core::Metascript::Value& asset,
        const Optional<NotNull<const Core::Metascript::Document*>>& document,
        Visit&& visit
    )const{
        class Collector final : public Core::Assets::ICookDependencyCollector{
        public:
            explicit Collector(Visit& callback) : m_callback(callback){}
            b8 addAssetDependency(const Name& dependency, const Name& expectedType)override{ return m_callback(dependency, expectedType); }
        private:
            Visit& m_callback;
        };
        Collector collector(visit);
        return registry.extractDependencies({file.assetRoot, file.virtualRoot, file.filePath, type, path, asset,
            document ? document->get() : nullptr, m_workspace.arena, m_workspace.scratch}, collector);
    }

private:
    AssetCookWorkspace& m_workspace;
public:
    Core::Assets::CookDependencyRegistry registry;
};

template<typename Inputs>
[[nodiscard]] inline Expected<AString<::ArenaBase>> EncodeCookInputPaths(::ArenaBase& output, const Inputs& inputs){
    Core::Assets::AssetArena arena(output.allocationLog());
    Core::Assets::ScratchArena scratch(output.allocationLog());
    const auto values = CopyCookInputStrings(arena, inputs);
    const auto text = Core::Assets::EncodeAssetInputList(values, scratch);
    if(!text)
        return MakeUnexpected(Failure{});
    return AString<::ArenaBase>(AStringView(*text), output);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

