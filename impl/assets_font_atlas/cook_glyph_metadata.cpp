// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_glyph_metadata.h"

#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_cook_glyph_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Metascript::Value;
static constexpr AStringView s_DiagnosticPrefix = "Font atlas glyph meta";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ReadU32(const Path& path, const Value& glyph, const AStringView field, const u32 maximum, u32& outValue){
    const Value* value = glyph.findField(field);
    if(value && value->isInteger() && value->asInteger() >= 0 && static_cast<u64>(value->asInteger()) <= maximum){
        outValue = static_cast<u32>(value->asInteger());
        return true;
    }
    NWB_LOGGER_ERROR(GLOBAL_TEXT("Font atlas glyph meta '{}': field '{}' must be an integer in 0..{}")
        , PathToString<tchar>(path)
        , StringConvert(field)
        , maximum
    );
    return false;
}

[[nodiscard]] static bool ReadGlyph(const Path& path, const Value& map, const u32 glyphId, const f32 unitsPerPixel, FontAtlasGlyph& glyph){
    if(!Core::Assets::CheckMetadataAssetMap(path, map, s_DiagnosticPrefix))
        return false;
    const bool hasBitmap = map.findField("width") != nullptr;
    if(!Core::Assets::ValidateMetadataAssetFields(
        path, map, s_DiagnosticPrefix,
        [hasBitmap](const AStringView field){
            return
                field == "advance_units"
                || (hasBitmap && (field == "group" || field == "channel" || field == "x" || field == "y"
                    || field == "width" || field == "height" || field == "plane_left" || field == "plane_top"))
            ;
        }
    ))
        return false;
    if(!Core::Assets::ReadMetadataFiniteF32Field(path, map, s_DiagnosticPrefix, "advance_units", true, glyph.advanceUnits))
        return false;
    glyph.glyphId = glyphId;
    if(!hasBitmap)
        return true;
    if(
        !ReadU32(path, map, "group", s_FontAtlasMaxGroupCount - 1u, glyph.group)
        || !ReadU32(path, map, "channel", 3u, glyph.channel)
        || !ReadU32(path, map, "x", s_FontAtlasMaxExtent, glyph.x)
        || !ReadU32(path, map, "y", s_FontAtlasMaxExtent, glyph.y)
        || !ReadU32(path, map, "width", s_FontAtlasMaxExtent, glyph.width)
        || !ReadU32(path, map, "height", s_FontAtlasMaxExtent, glyph.height)
        || !Core::Assets::ReadMetadataFiniteF32Field(path, map, s_DiagnosticPrefix, "plane_left", true, glyph.planeLeft)
        || !Core::Assets::ReadMetadataFiniteF32Field(path, map, s_DiagnosticPrefix, "plane_top", true, glyph.planeTop)
    )
        return false;
    if(glyph.width == 0u || glyph.height == 0u){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("Font atlas glyph meta '{}': bitmap dimensions must be positive; empty glyphs declare only advance_units"), PathToString<tchar>(path));
        return false;
    }
    glyph.planeRight = glyph.planeLeft + static_cast<f32>(glyph.width) * unitsPerPixel;
    glyph.planeBottom = glyph.planeTop + static_cast<f32>(glyph.height) * unitsPerPixel;
    glyph.drawable = 1u;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseFontAtlasGlyphMetadata(const Path& nwbFilePath, const Core::Metascript::Value& asset, FontAtlasPayload& outPayload){
    using namespace __hidden_font_atlas_cook_glyph_metadata;
    const Value* glyphs = Core::Assets::FindMetadataListField(nwbFilePath, asset, s_DiagnosticPrefix, "glyphs");
    if(!glyphs)
        return false;
    if(
        outPayload.sourceGlyphCount == 0u || outPayload.sourceGlyphCount > s_FontAtlasMaxGlyphCount
        || glyphs->asList().size() != outPayload.sourceGlyphCount
    ){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("Font atlas glyph meta '{}': exactly one record per source glyph is required"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    Core::Assets::AssetVector<FontAtlasGlyph> candidate(outPayload.glyphs.get_allocator().arena());
    candidate.reserve(outPayload.sourceGlyphCount);
    const f32 unitsPerPixel = static_cast<f32>(outPayload.unitsPerEm) / static_cast<f32>(outPayload.bakePpem);
    for(u32 index = 0u; index < outPayload.sourceGlyphCount; ++index){
        FontAtlasGlyph glyph;
        if(!ReadGlyph(nwbFilePath, glyphs->asList()[index], index, unitsPerPixel, glyph))
            return false;
        candidate.push_back(glyph);
    }
    outPayload.glyphs = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

