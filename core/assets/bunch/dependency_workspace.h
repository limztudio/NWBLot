// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include "cook.h"
#include <core/assets/cook_workspace.h>
#include <core/assets/input_list.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CookDependencyAccess final : NoCopy{
public:
    explicit CookDependencyAccess(AssetCookWorkspace& workspace) : m_workspace(workspace), registry(workspace.arena){}
    [[nodiscard]] b8 registerTypes(){ return RegisterAutoCollectedCookDependencyTypes(registry, m_workspace.scratch); }

    [[nodiscard]] Expected<Metascript::Document> read(const DiscoveredNwbFile& file, const AStringView prefix)const{
        Metascript::Document document(m_workspace.arena);
        ScratchString source(m_workspace.scratch);
        if(!ParseMetadataDocumentText(file.filePath, prefix, [&](const AStringView text){ return document.parse(text); }, source, document))
            return MakeUnexpected(Failure{});
        return document;
    }

    [[nodiscard]] Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> expand(const DiscoveredNwbFile& file, const Metascript::Document& document)const{
        return AssetsBunchCook::ExpandAssetBunchForAssetCook({file.assetRoot, file.virtualRoot, file.filePath, document, m_workspace.scratch});
    }
    [[nodiscard]] b8 expandBunch(const DiscoveredNwbFile& file, const Metascript::Document& document)const{
        return AssetsBunchCook::ExpandAssetBunch(file.assetRoot, file.virtualRoot, file.filePath, document, m_workspace.scratch).has_value();
    }
    [[nodiscard]] b8 expandComposite(const DiscoveredNwbFile& file, const Metascript::Document& document)const{
        return AssetsBunchCook::ExpandCompositeAsset(file.assetRoot, file.virtualRoot, file.filePath, document, m_workspace.scratch).has_value();
    }

    template<typename Visit>
    [[nodiscard]] b8 extract(const DiscoveredNwbFile& file, const Name type, const Name path, const Metascript::Value& asset,
        const Optional<NotNull<const Metascript::Document*>>& document, Visit&& visit)const{
        class Collector final : public ICookDependencyCollector{
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
    CookDependencyRegistry registry;
};

template<typename Inputs>
[[nodiscard]] inline Expected<AString<::ArenaBase>> EncodeCookInputPaths(::ArenaBase& output, const Inputs& inputs){
    AssetArena arena(output.allocationLog());
    ScratchArena scratch(output.allocationLog());
    const auto values = CopyCookInputStrings(arena, inputs);
    const auto text = EncodeAssetInputList(values, scratch);
    if(!text)
        return MakeUnexpected(Failure{});
    return AString<::ArenaBase>(AStringView(*text), output);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

