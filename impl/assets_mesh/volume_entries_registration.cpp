// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_mesh/volume_entries.adhd.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Impl{
namespace __hidden_assets_mesh_volume_entries_registration{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::CookEntryAutoRegistrar s_MeshCookEntriesRegistrar([](Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterCookEntry<SkinCookEntry, Skin, SkinAssetCodec, s_SkinMetaDiagnosticPrefix, true>(
            registry,
            FOUNDATION_TEXT("skin"),
            true,
            NWB_COOK_DISPATCH(ParseSkinCookMetadata),
            NWB_COOK_DISPATCH(CookSkinVolumeEntry)
        )
        && Core::Assets::RegisterCookEntry<MeshCookEntry, Mesh, MeshAssetCodec, s_MeshMetaText, true>(
            registry,
            FOUNDATION_TEXT("mesh"),
            true,
            NWB_COOK_DISPATCH(ParseMeshCookMetadata),
            NWB_COOK_DISPATCH(CookMeshVolumeEntry)
        );
});

static Core::Assets::CookDependencyAutoRegistrar s_SkinDependencyRegistrar(&RegisterSkinCookDependency);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

