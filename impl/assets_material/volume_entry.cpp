// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <impl/assets_shader/shader_types.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/paths.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_material_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 IsMaterialResourceParameterValue(const AStringView valueText, Core::Alloc::ScratchArena& scratchArena){
    const AStringView trimmed = TrimView(valueText);
    if(trimmed.empty() || trimmed.find('(') != AStringView::npos || trimmed.find(')') != AStringView::npos)
        return false;
    return Core::Assets::HasReservedAssetVirtualRoot(trimmed, scratchArena);
}

static Expected<MaterialCookEntry> ParseMaterialDocument(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::CookEntryParseContext& context){
    return ParseMaterialCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static b8 ExtractMaterialDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    if(!context.document)
        return false;
    const auto materialEntry = ParseMaterialCookMetadata(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena);
    if(!materialEntry)
        return false;

    Vector<Pair<Name, Name>, Core::Alloc::ScratchArena> stageShaders(context.scratchArena);
    stageShaders.reserve(materialEntry->stageShaders.size());
    for(const auto& [shaderType, shaderRef] : materialEntry->stageShaders){
        const Name expectedShaderType = ShaderAssetTypes::AssetTypeNameFromShaderType(shaderType);
        if(!expectedShaderType)
            return false;
        stageShaders.emplace_back(shaderRef.name(), expectedShaderType);
    }
    Sort(stageShaders.begin(), stageShaders.end(), [](const Pair<Name, Name>& lhs, const Pair<Name, Name>& rhs)noexcept{
        if(lhs.first() != rhs.first())
            return lhs.first() < rhs.first();
        return lhs.second() < rhs.second();
    });
    for(const Pair<Name, Name>& stageShader : stageShaders){
        if(!collector.addAssetDependency(stageShader.first(), stageShader.second()))
            return false;
    }

    Vector<AStringView, Core::Alloc::ScratchArena> resourceParameterValues(context.scratchArena);
    resourceParameterValues.reserve(materialEntry->parameters.size());
    for(const auto& [paramKey, paramValue] : materialEntry->parameters){
        static_cast<void>(paramKey);
        const AStringView valueView = TrimView(paramValue);
        if(IsMaterialResourceParameterValue(valueView, context.scratchArena))
            resourceParameterValues.push_back(valueView);
    }
    Sort(resourceParameterValues.begin(), resourceParameterValues.end());
    for(const AStringView resourceValue : resourceParameterValues){
        const auto resourceName = Name::FromText(resourceValue);
        if(!resourceName || !*resourceName)
            return false;
        if(!collector.addAssetDependency(*resourceName))
            return false;
    }
    return true;
}

static b8 RegisterMaterialCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterSingleDocumentCookEntry<MaterialCookEntry, Material, MaterialAssetCodec>(
        registry,
        MaterialBinaryPayload::s_MaterialAssetKindLabel,
        &ParseMaterialDocument,
        [](MaterialCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){ return BuildMaterialAsset(entry, arena); },
        false
    );
}

static b8 RegisterMaterialCookDependency(Core::Assets::CookDependencyRegistry& registry){
    return registry.registerType(Material::AssetTypeName(), &ExtractMaterialDependencies);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_MaterialCookEntryRegistrar, RegisterMaterialCookEntry);
NWB_DEFINE_COOK_DEPENDENCY_REGISTRAR(s_MaterialCookDependencyRegistrar, RegisterMaterialCookDependency);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

