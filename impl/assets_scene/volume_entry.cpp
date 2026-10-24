// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <impl/assets_csg/cook.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_mesh/skin_cook.h>
#include <impl/assets_model/cook.h>

#include <core/assets/cook_entry_registry.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/volume/volume_prepare_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_scene_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<ProjectCookEntry> ParseProjectDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseProjectCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static Expected<ProjectCookEntry> ParseProjectValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseProjectCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

static bool RegisterProjectCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<ProjectCookEntry, ProjectAsset, ProjectAssetCodec>(
        registry,
        ProjectBinaryPayload::s_ProjectAssetKindLabel,
        &ParseProjectDocument,
        &ParseProjectValue,
        &BuildProjectAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<SceneCookEntry> ParseSceneDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSceneCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static Expected<SceneCookEntry> ParseSceneValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSceneCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.scratchArena);
}

static void AppendFeatureIfAbsent(SceneAsset::NameVector& features, const Name& featureName){
    if(!featureName)
        return;
    for(const Name& existing : features){
        if(existing == featureName)
            return;
    }
    features.push_back(featureName);
}

static bool PrepareSceneVolumeEntries(Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext& context){
    auto& entryRegistry = context.parsedMetadata.entryRegistry;
    const Core::Assets::ICookEntryBucket* sceneBucket = entryRegistry.find(SceneAsset::AssetTypeName());
    if(!sceneBucket || sceneBucket->size() == 0u)
        return true;

    bool hasSkinning = false;
    if(const Core::Assets::ICookEntryBucket* skinBucket = entryRegistry.find(Skin::AssetTypeName()); skinBucket && skinBucket->size() > 0u)
        hasSkinning = true;
    if(!hasSkinning){
        if(const Core::Assets::ICookEntryBucket* modelBucket = entryRegistry.find(Model::AssetTypeName()); modelBucket && modelBucket->size() > 0u){
            for(const ModelCookEntry& model : entryRegistry.entries<ModelCookEntry>(Model::AssetTypeName())){
                if(!model.skinnedMeshObjects.empty()){
                    hasSkinning = true;
                    break;
                }
            }
        }
    }

    bool hasTransparent = false;
    bool hasRefractive = false;
    if(const Core::Assets::ICookEntryBucket* materialBucket = entryRegistry.find(Material::AssetTypeName()); materialBucket && materialBucket->size() > 0u){
        for(const MaterialCookEntry& materialEntry : entryRegistry.entries<MaterialCookEntry>(Material::AssetTypeName())){
            if(materialEntry.transparent)
                hasTransparent = true;
            if(materialEntry.refractive)
                hasRefractive = true;
        }
    }

    const auto* csgPresence = Core::Assets::FindParsedMetadataExtension<AssetsCsgCook::CsgVolumePresence>(
        context.parsedMetadata,
        AssetsCsgCook::s_CsgVolumePresenceExtensionName
    );
    const bool hasCsg = csgPresence && csgPresence->shapeCount > 0u;

    for(SceneCookEntry& scene : entryRegistry.entries<SceneCookEntry>(SceneAsset::AssetTypeName())){
        if(hasSkinning)
            AppendFeatureIfAbsent(scene.enabledFeatures, BuiltInSubsystemFeatures::s_Skinning);
        if(hasTransparent)
            AppendFeatureIfAbsent(scene.enabledFeatures, BuiltInSubsystemFeatures::s_Transparent);
        if(hasRefractive)
            AppendFeatureIfAbsent(scene.enabledFeatures, BuiltInSubsystemFeatures::s_Refractive);
        if(hasCsg)
            AppendFeatureIfAbsent(scene.enabledFeatures, BuiltInSubsystemFeatures::s_Csg);
    }

    return true;
}

static bool RegisterSceneCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<SceneCookEntry, SceneAsset, SceneAssetCodec>(
        registry,
        SceneBinaryPayload::s_SceneAssetKindLabel,
        &ParseSceneDocument,
        &ParseSceneValue,
        &BuildSceneAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_ProjectCookEntryRegistrar, RegisterProjectCookEntry);
NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_SceneCookEntryRegistrar, RegisterSceneCookEntry);
static Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareAutoRegistrar s_SceneVolumePrepareRegistrar(&PrepareSceneVolumeEntries);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

