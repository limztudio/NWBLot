// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "prepared_source.h"

#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_DiagnosticPrefix = "Font meta";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LoadPairedFontCookSource(const Path& nwbFilePath, Font& outFont){
    Path fontPath = nwbFilePath;
    fontPath.replace_extension(".font");
    Core::Assets::AssetArena& arena = outFont.fontBytes().get_allocator().arena();
    PreparedFontSource source(arena);
    if(!ReadPreparedFontSource(fontPath, source, false))
        return false;
    Font candidate(arena, outFont.virtualPath());
    candidate.setFontBytes(Move(source.fontBytes), source.faceIndex);
    if(!candidate.validatePayload())
        return false;
    outFont = Move(candidate);
    return true;
}

bool ParseFontCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    FontCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena){
    Name virtualPath = NAME_NONE;
    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, virtualPath, scratchArena))
        return false;
    return ParseFontCookMetadataValue(virtualPath, nwbFilePath, doc.asset(), outEntry);
}

bool ParseFontCookMetadataValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    FontCookEntry& outEntry){
    using namespace __hidden_font_cook_metadata;
    if(!virtualPath)
        return false;
    if(!asset.isNull()){
        if(
            !Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix)
            || !Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_DiagnosticPrefix, InitializerList<AStringView>{})
        )
            return false;
    }
    Font font(outEntry.arena, virtualPath);
    if(!LoadPairedFontCookSource(nwbFilePath, font))
        return false;
    Core::Assets::AssetBytes bytes(font.fontBytes().begin(), font.fontBytes().end(), outEntry.arena);
    outEntry.fontBytes = Move(bytes);
    outEntry.virtualPath = virtualPath;
    outEntry.faceIndex = font.faceIndex();
    return true;
}

bool BuildFontAsset(const FontCookEntry& entry, Font& outFont){
    Font candidate(entry.arena, entry.virtualPath);
    Core::Assets::AssetBytes bytes(entry.fontBytes.begin(), entry.fontBytes.end(), entry.arena);
    candidate.setFontBytes(Move(bytes), entry.faceIndex);
    if(!candidate.validatePayload())
        return false;
    outFont = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

