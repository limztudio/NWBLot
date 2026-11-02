// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "prepared_source.h"

#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::AssetPairedSidecarRuleAutoRegistrar s_FontPairedSidecarRuleAutoRegistrar({
    .firstToken = "font",
    .sidecarExtension = s_PreparedFontExtension,
    .bunchScanKeyword = "font",
});


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 FontAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, NWB_TEXT("FontAssetCodec::serialize")))
        return false;
    const Font& font = *MakeNotNull(checked_cast<const Font*>(&asset));
    if(!font.validatePayload())
        return false;

    FontBinaryHeader header;
    header.faceIndex = font.faceIndex();
    header.byteCount = static_cast<u64>(font.fontBytes().size());
    outBinary.clear();
    outBinary.reserve(sizeof(header) + font.fontBytes().size());
    AppendPOD(outBinary, header);
    outBinary.insert(outBinary.end(), font.fontBytes().begin(), font.fontBytes().end());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FontCookEntry> ParseFontCookMetadata(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    return Core::Assets::ParseCookMetadataDocument<FontCookEntry>(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        s_FontMetaDiagnosticPrefix,
        [&nwbFilePath, &arena](const Name virtualPath, const Core::Metascript::Value& asset){
            return ParseFontCookMetadataValue(virtualPath, nwbFilePath, asset, arena);
        },
        scratchArena
    );
}

Expected<FontCookEntry> ParseFontCookMetadataValue(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena){
    FontCookEntry entry(arena);
    if(!virtualPath)
        return MakeUnexpected(Failure{});
    if(!asset.isNull()){
        if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_FontMetaDiagnosticPrefix) || !Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_FontMetaDiagnosticPrefix, InitializerList<AStringView>{}))
            return MakeUnexpected(Failure{});
    }
    Path fontPath = nwbFilePath;
    fontPath.replaceExtension(s_PreparedFontExtension);
    auto source = ReadPreparedFontSource(fontPath, false, arena);
    if(!source || !ValidateFontSource(source->fontBytes, source->faceIndex))
        return MakeUnexpected(Failure{});
    entry.fontBytes = Move(source->fontBytes);
    entry.virtualPath = virtualPath;
    entry.faceIndex = source->faceIndex;
    return entry;
}

Expected<Font> BuildFontAsset(FontCookEntry&& entry, Core::Assets::AssetArena& arena){
    Font candidate(arena, entry.virtualPath);
    if(&entry.fontBytes.get_allocator().arena() == &arena){
        candidate.setFontBytes(Move(entry.fontBytes), entry.faceIndex);
    }
    else{
        Core::Assets::AssetBytes bytes(entry.fontBytes.begin(), entry.fontBytes.end(), arena);
        candidate.setFontBytes(Move(bytes), entry.faceIndex);
    }
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

