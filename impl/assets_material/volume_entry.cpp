// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <impl/assets_graphics/mesh/names.h>
#include <impl/assets_subsystem/asset.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/paths.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_material_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool IsMaterialResourceParameterValue(const AStringView valueText, Core::Alloc::ScratchArena& scratchArena){
    const AStringView trimmed = TrimView(valueText);
    if(trimmed.empty() || trimmed.find('(') != AStringView::npos || trimmed.find(')') != AStringView::npos)
        return false;
    return Core::Assets::HasReservedAssetVirtualRoot(trimmed, scratchArena);
}

static Expected<MaterialCookEntry> ParseMaterialDocument(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::CookEntryParseContext& context){
    return ParseMaterialCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static bool ExtractMaterialDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    if(!context.document)
        return false;
    const auto materialEntry = ParseMaterialCookMetadata(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena);
    if(!materialEntry)
        return false;

    if(materialEntry->transparent && !collector.addSubsystemDependency(BuiltInSubsystems::s_Transparent.name()))
        return false;
    if(materialEntry->refractive && !collector.addSubsystemDependency(BuiltInSubsystems::s_Refractive.name()))
        return false;
    if(!materialEntry->surfaceSource.empty()){
        if(!collector.addAssetDependency(Name(AssetsGraphicsMesh::s_SharedMeshShaderText)))
            return false;
    }

    Vector<Name, Core::Alloc::ScratchArena> stageShaderNames(context.scratchArena);
    stageShaderNames.reserve(materialEntry->stageShaders.size());
    for(const auto& [shaderType, shaderRef] : materialEntry->stageShaders){
        static_cast<void>(shaderType);
        stageShaderNames.push_back(shaderRef.name());
    }
    Sort(stageShaderNames.begin(), stageShaderNames.end());
    for(const Name& shaderName : stageShaderNames){
        if(!collector.addAssetDependency(shaderName))
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

static bool RegisterMaterialCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterSingleDocumentCookEntry<MaterialCookEntry, Material, MaterialAssetCodec>(
        registry,
        MaterialBinaryPayload::s_MaterialAssetKindLabel,
        &ParseMaterialDocument,
        [](MaterialCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){ return BuildMaterialAsset(entry, arena); },
        false
    );
}

static bool RegisterMaterialCookDependency(Core::Assets::CookDependencyRegistry& registry){
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

