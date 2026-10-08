// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "prepared_source.h"
#include "font_validation.h"

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


Expected<FontCookEntry> ParseFontCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    Name virtualPath = s_NameNone;
    auto virtualPathResult = Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    virtualPath = *virtualPathResult;
    return ParseFontCookMetadataValue(virtualPath, nwbFilePath, doc.asset(), arena);
}

Expected<FontCookEntry> ParseFontCookMetadataValue(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena
){
    FontCookEntry entry(arena);
    using namespace __hidden_font_cook_metadata;
    if(!virtualPath)
        return MakeUnexpected(Failure{});
    if(!asset.isNull()){
        if(
            !Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix)
            || !Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_DiagnosticPrefix, InitializerList<AStringView>{})
        )
            return MakeUnexpected(Failure{});
    }
    Path fontPath = nwbFilePath;
    fontPath.replaceExtension(".font");
    auto source = ReadPreparedFontSource(fontPath, arena, false);
    if(!source || !ValidateFontSource(source->fontBytes, source->faceIndex))
        return MakeUnexpected(Failure{});
    entry.fontBytes = Move(source->fontBytes);
    entry.virtualPath = virtualPath;
    entry.faceIndex = source->faceIndex;
    return entry;
}

Expected<Font> BuildFontAsset(const FontCookEntry& entry, Core::Assets::AssetArena& arena){
    Font candidate(arena, entry.virtualPath);
    Core::Assets::AssetBytes bytes(entry.fontBytes.begin(), entry.fontBytes.end(), arena);
    candidate.setFontBytes(Move(bytes), entry.faceIndex);
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

