// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "skin_cook.h"
#include "binary_payload.h"
#include "skin_binary_payload.h"

#include <core/assets/cook_entry_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_mesh_volume_entries{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<MeshCookEntry> ParseMeshDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseMeshCookMetadata(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        context.cookArena,
        context.cpuScheduler,
        context.scratchArena
    );
}

static Expected<MeshCookEntry> ParseMeshValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseMeshCookMetadata(
        virtualPath,
        nwbFilePath,
        asset,
        context.cookArena,
        context.cpuScheduler,
        context.scratchArena
    );
}

static bool RegisterMeshCookEntries(Core::Assets::CookEntryRegistry& registry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<SkinCookEntry> ParseSkinDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSkinCookMetadata(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        context.cookArena,
        context.scratchArena
    );
}

static Expected<SkinCookEntry> ParseSkinValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSkinCookMetadata(
        virtualPath,
        nwbFilePath,
        asset,
        context.cookArena
    );
}

static bool RegisterMeshCookEntries(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<MeshCookEntry, Mesh, MeshAssetCodec>(
        registry,
        MeshBinaryPayload::s_MeshAssetKindLabel,
        &ParseMeshDocument,
        &ParseMeshValue,
        [](MeshCookEntry& entry, Core::Assets::AssetArena& arena){ return Core::Assets::ForwardCookBuild(entry, arena, &BuildMeshAsset); }
    )
        && Core::Assets::RegisterDocumentValueCookEntry<SkinCookEntry, Skin, SkinAssetCodec>(
            registry,
            SkinBinaryPayload::s_SkinAssetKindLabel,
            &ParseSkinDocument,
            &ParseSkinValue,
            [](SkinCookEntry& entry, Core::Assets::AssetArena& arena){ return Core::Assets::ForwardCookBuild(entry, arena, &BuildSkinAsset); }
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_MeshCookEntryRegistrar, RegisterMeshCookEntries);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

