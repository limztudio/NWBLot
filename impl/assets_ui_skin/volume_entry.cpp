// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <impl/assets_ui_skin/subsystem.adhd.h>

#include <impl/assets_subsystem/cook.h>
#include <impl/assets_texture/asset.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<UiSkinCookEntry> ParseUiSkinDocument(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::CookEntryParseContext& context){
    return ParseUiSkinCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, context.cookArena, context.scratchArena);
}

static b8 ExtractUiSkinDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    if(!context.document)
        return false;
    const auto skinEntry = ParseUiSkinCookMetadata(context.assetRoot, context.virtualRoot, context.nwbFilePath, *context.document, context.arena, context.scratchArena);
    if(!skinEntry)
        return false;
    return collector.addAssetDependency(skinEntry->texture.name(), Texture::AssetTypeName());
}

static b8 RegisterUiSkinCookEntry(Core::Assets::CookEntryRegistry& registry){
    return
        Core::Assets::RegisterSingleDocumentCookEntry<UiSkinCookEntry, UiSkin, UiSkinAssetCodec>(
            registry,
            s_UiSkinAssetKindLabel,
            &ParseUiSkinDocument,
            [](UiSkinCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){ return BuildUiSkinAsset(entry, arena); },
            false
        )
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<UiSubsystem>(registry)
    ;
}

static b8 RegisterUiSkinCookDependency(Core::Assets::CookDependencyRegistry& registry){
    return
        registry.registerType(UiSkin::AssetTypeName(), &ExtractUiSkinDependencies)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<UiSubsystem>(registry)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_UiSkinCookEntryRegistrar, RegisterUiSkinCookEntry);
NWB_DEFINE_COOK_DEPENDENCY_REGISTRAR(s_UiSkinCookDependencyRegistrar, RegisterUiSkinCookDependency);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

