// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <impl/assets_font/asset.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/cook_metadata.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<FontAtlas> BuildFontAtlasCookedAsset(FontAtlasCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    return BuildFontAtlasAsset(entry, arena, scratchArena);
}

static b8 ExtractFontAtlasDependencies(const Core::Assets::CookDependencyItemContext& context, Core::Assets::ICookDependencyCollector& collector){
    const auto fontRef = Core::Assets::ReadMetadataAssetRefField<Font>(context.nwbFilePath, context.asset, s_FontAtlasMetaDiagnosticPrefix, s_FontAtlasMetaFontField, true);
    if(!fontRef)
        return false;
    return collector.addAssetDependency(fontRef->name(), Font::AssetTypeName());
}

static b8 RegisterFontAtlasCookEntry(Core::Assets::CookEntryRegistry& registry){
    return Core::Assets::RegisterDocumentValueCookEntry<FontAtlasCookEntry, FontAtlas, FontAtlasAssetCodec>(
        registry, NWB_TEXT("font atlas"),
        [](const Path& root, AStringView virtualRoot, const Path& path, const Core::Metascript::Document& doc, Core::Assets::CookEntryParseContext& context){
            return ParseFontAtlasCookMetadata(root, virtualRoot, path, doc, context.cookArena, context.scratchArena);
        },
        [](Name virtualPath, const Path& path, const Core::Metascript::Value& asset, Core::Assets::CookEntryParseContext& context){
            return ParseFontAtlasCookMetadataValue(virtualPath, path, asset, context.cookArena, context.scratchArena);
        },
        &BuildFontAtlasCookedAsset, false
    );
}

static b8 RegisterFontAtlasCookDependency(Core::Assets::CookDependencyRegistry& registry){
    return registry.registerType(FontAtlas::AssetTypeName(), &ExtractFontAtlasDependencies);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_FontAtlasCookEntryRegistrar, RegisterFontAtlasCookEntry);
NWB_DEFINE_COOK_DEPENDENCY_REGISTRAR(s_FontAtlasCookDependencyRegistrar, RegisterFontAtlasCookDependency);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

