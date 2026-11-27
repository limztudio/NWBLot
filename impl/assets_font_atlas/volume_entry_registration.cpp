// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font_atlas/volume_entry.adhd.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Impl{
namespace __hidden_assets_font_atlas_volume_entry_registration{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::AssetPairedSidecarRuleAutoRegistrar s_PreparedFontAtlasSidecarRegistrar({ .sidecarExtension = s_PreparedFontExtension });

static Core::Assets::CookEntryAutoRegistrar s_FontAtlasCookEntryRegistrar([](Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterCookEntry<FontAtlasCookEntry, FontAtlas, FontAtlasAssetCodec, s_FontAtlasMetaDiagnosticPrefix, true>(
        registry,
        FOUNDATION_TEXT("font atlas"),
        false,
        NWB_COOK_DISPATCH(ParseFontAtlasCookMetadataValue),
        NWB_COOK_DISPATCH(CookFontAtlasVolumeEntry)
    );
});

static Core::Assets::CookDependencyAutoRegistrar s_FontAtlasDependencyRegistrar(&RegisterFontAtlasCookDependency);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

