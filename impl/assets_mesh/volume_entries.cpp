
#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "skin_cook.h"

#include <core/assets/cook_entry_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_mesh_volume_entries{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMeshDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MeshCookEntry& outEntry,
    Core::Assets::CookEntryParseContext& context
){
    return ParseMeshCookMetadata(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        outEntry,
        context.threadPool,
        context.scratchArena
    );
}

static bool ParseMeshValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    MeshCookEntry& outEntry,
    Core::Assets::CookEntryParseContext& context
){
    return ParseMeshCookMetadata(
        virtualPath,
        nwbFilePath,
        asset,
        outEntry,
        context.threadPool,
        context.scratchArena
    );
}

static bool BuildMeshCookedAsset(MeshCookEntry& entry, Mesh& outAsset){
    return BuildMeshAsset(entry, outAsset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseSkinDocument(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    SkinCookEntry& outEntry,
    Core::Assets::CookEntryParseContext& context
){
    return ParseSkinCookMetadata(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        outEntry,
        context.scratchArena
    );
}

static bool ParseSkinValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    SkinCookEntry& outEntry,
    Core::Assets::CookEntryParseContext&
){
    return ParseSkinCookMetadata(
        virtualPath,
        nwbFilePath,
        asset,
        outEntry
    );
}

static bool BuildSkinCookedAsset(SkinCookEntry& entry, Skin& outAsset){
    return BuildSkinAsset(entry, outAsset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool RegisterMeshCookEntries(Core::Assets::CookEntryRegistry& registry){
    return registry.registerType<MeshCookEntry, Mesh, MeshAssetCodec>(
        Mesh::AssetTypeName(),
        NWB_TEXT("mesh"),
        &ParseMeshDocument,
        &ParseMeshValue,
        &BuildMeshCookedAsset
    )
        && registry.registerType<SkinCookEntry, Skin, SkinAssetCodec>(
            Skin::AssetTypeName(),
            NWB_TEXT("skin"),
            &ParseSkinDocument,
            &ParseSkinValue,
            &BuildSkinCookedAsset
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::CookEntryAutoRegistrar s_MeshCookEntryRegistrar(&RegisterMeshCookEntries);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

