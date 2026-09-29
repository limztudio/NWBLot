// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bundle_cook.h"

#include <core/assets/cook_entry_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_bundle_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FontBundleCookEntryBucket final : public Core::Assets::ICookEntryBucket{
public:
    explicit FontBundleCookEntryBucket(Core::Assets::CookArena& arena)
        : m_entries(arena)
    {}


public:
    // Volume sizing counts cooked assets; each bundle writes one Font and one FontAtlas.
    [[nodiscard]] virtual usize size()const noexcept override{ return m_entries.size() * 2u; }

    virtual bool parseDocument(
        const Path& assetRoot,
        const AStringView virtualRoot,
        const Path& nwbFilePath,
        const Core::Metascript::Document& doc,
        Core::Assets::CookEntryParseContext& context
    )override{
        FontBundleCookEntry entry(context.cookArena);
        if(!ParseFontBundleCookMetadata(assetRoot, virtualRoot, nwbFilePath, doc, entry, context.scratchArena))
            return false;
        if(
            !Core::Assets::CookEntryRegistryDetail::RegisterParsedVirtualPath(
                MakeNotNull(NWB_TEXT("font")), entry.fontVirtualPath, context.seenVirtualPathHashes
            )
            || !Core::Assets::CookEntryRegistryDetail::RegisterParsedVirtualPath(
                MakeNotNull(NWB_TEXT("font atlas")), entry.atlasVirtualPath, context.seenVirtualPathHashes
            )
        )
            return false;
        m_entries.push_back(Move(entry));
        return true;
    }

    virtual bool parseValue(
        Name,
        const Path& nwbFilePath,
        const Core::Metascript::Value&,
        Core::Assets::CookEntryParseContext&
    )override{
        NWB_LOGGER_ERROR(NWB_TEXT("Font bundle cannot be declared inside asset_bunch '{}'"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    virtual bool writeCookedAssets(Core::Assets::CookEntryWriteContext& context)override{
        FontAssetCodec fontCodec;
        FontAtlasAssetCodec atlasCodec;
        Core::Assets::AssetArena& arena = m_entries.get_allocator().arena();
        for(const FontBundleCookEntry& entry : m_entries){
            if(
                !Core::Assets::CookEntryRegistryDetail::RegisterCookedVirtualPath(
                    MakeNotNull(NWB_TEXT("font")), entry.fontVirtualPath, context.seenVirtualPathHashes
                )
                || !Core::Assets::CookEntryRegistryDetail::RegisterCookedVirtualPath(
                    MakeNotNull(NWB_TEXT("font atlas")), entry.atlasVirtualPath, context.seenVirtualPathHashes
                )
            )
                return false;

            Font font(arena, entry.fontVirtualPath);
            Core::Assets::AssetBytes fontBytes(entry.fontBytes.begin(), entry.fontBytes.end(), arena);
            font.setFontBytes(Move(fontBytes));
            FontAtlas atlas(arena, entry.atlasVirtualPath);
            FontAtlasPayload atlasPayload(entry.atlasPayload);
            atlas.setPayload(Move(atlasPayload));
            if(!font.validatePayload() || !atlas.validatePayload() || !ValidateFontAtlasSourceMatch(atlas.payload(), font))
                return false;
            if(
                !context.writer.writeCookedAsset(MakeNotNull(NWB_TEXT("font")), entry.fontVirtualPath, font, fontCodec)
                || !context.writer.writeCookedAsset(MakeNotNull(NWB_TEXT("font atlas")), entry.atlasVirtualPath, atlas, atlasCodec)
            )
                return false;
        }
        return true;
    }


private:
    Core::Assets::CookVector<FontBundleCookEntry> m_entries;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool RegisterFontBundleCookEntry(Core::Assets::CookEntryRegistry& registry){
    return registry.registerCustomType<FontBundleCookEntryBucket>(Name("font_bundle"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_FontBundleCookEntryRegistrar, RegisterFontBundleCookEntry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

