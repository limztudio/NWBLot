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


[[nodiscard]] static Expected<u32> ReadU32(const Path& path, const Value& glyph, const AStringView field, const u32 maximum){
    const Value* value = glyph.findField(field);
    if(value && value->isInteger() && value->asInteger() >= 0 && static_cast<u64>(value->asInteger()) <= maximum){
        return static_cast<u32>(value->asInteger());
    }
    NWB_LOGGER_ERROR(NWB_TEXT("Font atlas glyph meta '{}': field '{}' must be an integer in 0..{}")
        , PathToString<tchar>(path)
        , StringConvert(field)
        , maximum
    );
    return MakeUnexpected(Failure{});
}

[[nodiscard]] static Expected<FontAtlasGlyph> ReadGlyph(const Path& path, const Value& map, const u32 glyphId, const f32 unitsPerPixel){
    FontAtlasGlyph glyph{};

    if(!Core::Assets::CheckMetadataAssetMap(path, map, s_DiagnosticPrefix))
        return MakeUnexpected(Failure{});
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
        return MakeUnexpected(Failure{});
    auto advanceUnitsResult = Core::Assets::ReadMetadataFiniteF32Field(path, map, s_DiagnosticPrefix, "advance_units", true);
    if(!advanceUnitsResult)
        return MakeUnexpected(Failure{});
    glyph.advanceUnits = *advanceUnitsResult;
    glyph.glyphId = glyphId;
    if(!hasBitmap)
        return glyph;
    auto groupResult = ReadU32(path, map, "group", s_FontAtlasMaxGroupCount - 1u);
    if(!groupResult)
        return MakeUnexpected(Failure{});
    glyph.group = *groupResult;
    auto channelResult = ReadU32(path, map, "channel", 3u);
    if(!channelResult)
        return MakeUnexpected(Failure{});
    glyph.channel = *channelResult;
    auto xResult = ReadU32(path, map, "x", s_FontAtlasMaxExtent);
    if(!xResult)
        return MakeUnexpected(Failure{});
    glyph.x = *xResult;
    auto yResult = ReadU32(path, map, "y", s_FontAtlasMaxExtent);
    if(!yResult)
        return MakeUnexpected(Failure{});
    glyph.y = *yResult;
    auto widthResult = ReadU32(path, map, "width", s_FontAtlasMaxExtent);
    if(!widthResult)
        return MakeUnexpected(Failure{});
    glyph.width = *widthResult;
    auto heightResult = ReadU32(path, map, "height", s_FontAtlasMaxExtent);
    if(!heightResult)
        return MakeUnexpected(Failure{});
    glyph.height = *heightResult;
    auto planeLeftResult = Core::Assets::ReadMetadataFiniteF32Field(path, map, s_DiagnosticPrefix, "plane_left", true);
    if(!planeLeftResult)
        return MakeUnexpected(Failure{});
    glyph.planeLeft = *planeLeftResult;
    auto planeTopResult = Core::Assets::ReadMetadataFiniteF32Field(path, map, s_DiagnosticPrefix, "plane_top", true);
    if(!planeTopResult)
        return MakeUnexpected(Failure{});
    glyph.planeTop = *planeTopResult;
    if(glyph.width == 0u || glyph.height == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas glyph meta '{}': bitmap dimensions must be positive; empty glyphs declare only advance_units"), PathToString<tchar>(path));
        return MakeUnexpected(Failure{});
    }
    glyph.planeRight = glyph.planeLeft + static_cast<f32>(glyph.width) * unitsPerPixel;
    glyph.planeBottom = glyph.planeTop + static_cast<f32>(glyph.height) * unitsPerPixel;
    glyph.drawable = 1u;
    return glyph;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetVector<FontAtlasGlyph>> ParseFontAtlasGlyphMetadata(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const FontAtlasPayload& payload
){
    using namespace __hidden_font_atlas_cook_glyph_metadata;
    const Value* glyphs = Core::Assets::FindMetadataListField(nwbFilePath, asset, s_DiagnosticPrefix, "glyphs");
    if(!glyphs)
        return MakeUnexpected(Failure{});
    if(
        payload.sourceGlyphCount == 0u || payload.sourceGlyphCount > s_FontAtlasMaxGlyphCount
        || glyphs->asList().size() != payload.sourceGlyphCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas glyph meta '{}': exactly one record per source glyph is required"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    Core::Assets::AssetVector<FontAtlasGlyph> candidate(payload.glyphs.get_allocator().arena());
    candidate.reserve(payload.sourceGlyphCount);
    const f32 unitsPerPixel = static_cast<f32>(payload.unitsPerEm) / static_cast<f32>(payload.bakePpem);
    for(u32 index = 0u; index < payload.sourceGlyphCount; ++index){
        auto glyphResult = ReadGlyph(nwbFilePath, glyphs->asList()[index], index, unitsPerPixel);
        if(!glyphResult)
            return MakeUnexpected(Failure{});
        candidate.push_back(*glyphResult);
    }
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

