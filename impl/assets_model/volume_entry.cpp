// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_model_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<ModelCookEntry> ParseModelDocument(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::CookEntryParseContext& context){
    return ParseModelCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static Expected<ModelCookEntry> ParseModelValue(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::CookEntryParseContext& context){
    return ParseModelCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

static bool ExtractModelDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto modelEntry = ParseModelCookMetadata(context.virtualPath, context.nwbFilePath, context.asset, context.arena, context.scratchArena);
    if(!modelEntry)
        return false;

    for(const ModelSkeletonObject& skeletonObject : modelEntry->skeletonObjects){
        if(!collector.addAssetDependency(skeletonObject.skeleton.name(), Skeleton::AssetTypeName()))
            return false;
    }
    for(const ModelStaticMeshObject& staticMeshObject : modelEntry->staticMeshObjects){
        if(!collector.addAssetDependency(staticMeshObject.mesh.name(), Mesh::AssetTypeName()))
            return false;
        if(staticMeshObject.material.valid() && !collector.addAssetDependency(staticMeshObject.material.name(), Material::AssetTypeName()))
            return false;
    }
    for(const ModelSkinnedMeshObject& skinnedMeshObject : modelEntry->skinnedMeshObjects){
        if(!collector.addAssetDependency(skinnedMeshObject.skin.name(), Skin::AssetTypeName()))
            return false;
        if(skinnedMeshObject.material.valid() && !collector.addAssetDependency(skinnedMeshObject.material.name(), Material::AssetTypeName()))
            return false;
    }
    return true;
}

static bool RegisterModelCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<ModelCookEntry, Model, ModelAssetCodec>(registry, ModelBinaryPayload::s_ModelAssetKindLabel, &ParseModelDocument, &ParseModelValue, &BuildModelAsset);
}

static bool RegisterModelCookDependency(Core::Assets::CookDependencyRegistry& registry){
    return registry.registerType(Model::AssetTypeName(), &ExtractModelDependencies);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_ModelCookEntryRegistrar, RegisterModelCookEntry);
NWB_DEFINE_COOK_DEPENDENCY_REGISTRAR(s_ModelCookDependencyRegistrar, RegisterModelCookDependency);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

