// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "skin_cook.h"
#include "binary_payload.h"
#include "skin_binary_payload.h"
#include "subsystem.h"

#include <impl/assets_subsystem/cook.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_mesh_volume_entries{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_SkinDiagnosticPrefix = "Skin meta";
static constexpr AStringView s_MeshField = "mesh";
static constexpr AStringView s_SkeletonField = "skeleton";


static Expected<MeshCookEntry> ParseMeshDocument(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::CookEntryParseContext& context){
    return ParseMeshCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.cpuScheduler, context.scratchArena);
}

static Expected<MeshCookEntry> ParseMeshValue(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::CookEntryParseContext& context){
    return ParseMeshCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena, context.cpuScheduler, context.scratchArena);
}

static bool RegisterMeshCookEntries(Core::Assets::CookEntryRegistry& registry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<SkinCookEntry> ParseSkinDocument(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::CookEntryParseContext& context){
    return ParseSkinCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static Expected<SkinCookEntry> ParseSkinValue(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::CookEntryParseContext& context){
    return ParseSkinCookMetadata(virtualPath, nwbFilePath, asset, context.cookArena);
}

static bool ExtractSkinDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto meshRef = Core::Assets::ReadMetadataAssetRefField<Mesh>(context.nwbFilePath, context.asset, s_SkinDiagnosticPrefix, s_MeshField, true);
    if(!meshRef)
        return false;
    const auto skeletonRef = Core::Assets::ReadMetadataAssetRefField<Skeleton>(context.nwbFilePath, context.asset, s_SkinDiagnosticPrefix, s_SkeletonField, true);
    if(!skeletonRef)
        return false;
    if(!collector.addAssetDependency(meshRef->name(), Mesh::AssetTypeName()))
        return false;
    return collector.addAssetDependency(skeletonRef->name(), Skeleton::AssetTypeName());
}

static bool RegisterMeshCookEntries(Core::Assets::CookEntryRegistry& registry){
    return
        Core::Assets::RegisterDocumentValueCookEntry<MeshCookEntry, Mesh, MeshAssetCodec>(registry, MeshBinaryPayload::s_MeshAssetKindLabel, &ParseMeshDocument, &ParseMeshValue, &BuildMeshAsset)
        && Core::Assets::RegisterDocumentValueCookEntry<SkinCookEntry, Skin, SkinAssetCodec>(
            registry,
            SkinBinaryPayload::s_SkinAssetKindLabel,
            &ParseSkinDocument,
            &ParseSkinValue,
            [](SkinCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){ return BuildSkinAsset(entry, arena); }
        )
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<SkinningSubsystem>(registry)
    ;
}

static bool RegisterSkinCookDependency(Core::Assets::CookDependencyRegistry& registry){
    return
        registry.registerType(Skin::AssetTypeName(), &ExtractSkinDependencies)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<SkinningSubsystem>(registry)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_MeshCookEntryRegistrar, RegisterMeshCookEntries);
NWB_DEFINE_COOK_DEPENDENCY_REGISTRAR(s_SkinCookDependencyRegistrar, RegisterSkinCookDependency);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
