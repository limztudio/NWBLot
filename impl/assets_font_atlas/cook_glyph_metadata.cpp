// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_cook_glyph_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Metascript::Value;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<FontAtlasGlyph> ReadGlyph(const Path& path, const Value& map, const u32 glyphId, const f32 unitsPerPixel){
    FontAtlasGlyph glyph{};

    if(!Core::Assets::CheckMetadataAssetMap(path, map, s_FontAtlasGlyphMetaDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    const b8 hasBitmap = map.findField(s_FontAtlasGlyphWidthField) != nullptr;
    if(!Core::Assets::ValidateMetadataAssetFields(path, map, s_FontAtlasGlyphMetaDiagnosticPrefix, [hasBitmap](const AStringView field)noexcept{
        return IsAllowedFontAtlasGlyphField(field, hasBitmap);
    }))
        return MakeUnexpected(Failure{});
    auto advanceUnitsResult = Core::Assets::ReadMetadataFiniteF32Field(path, map, s_FontAtlasGlyphMetaDiagnosticPrefix, s_FontAtlasGlyphAdvanceUnitsField, true);
    if(!advanceUnitsResult)
        return MakeUnexpected(Failure{});
    glyph.advanceUnits = *advanceUnitsResult;
    glyph.glyphId = glyphId;
    if(!hasBitmap)
        return glyph;
    auto groupResult = ReadFontAtlasMetadataU32(path, map, s_FontAtlasGlyphMetaDiagnosticPrefix, s_FontAtlasGlyphGroupField, 0u, s_FontAtlasMaxGroupCount - 1u);
    if(!groupResult)
        return MakeUnexpected(Failure{});
    glyph.group = *groupResult;
    auto channelResult = ReadFontAtlasMetadataU32(path, map, s_FontAtlasGlyphMetaDiagnosticPrefix, s_FontAtlasGlyphChannelField, 0u, s_FontAtlasMaxChannelCount - 1u);
    if(!channelResult)
        return MakeUnexpected(Failure{});
    glyph.channel = *channelResult;
    auto xResult = ReadFontAtlasMetadataU32(path, map, s_FontAtlasGlyphMetaDiagnosticPrefix, s_FontAtlasGlyphXField, 0u, s_FontAtlasMaxExtent);
    if(!xResult)
        return MakeUnexpected(Failure{});
    glyph.x = *xResult;
    auto yResult = ReadFontAtlasMetadataU32(path, map, s_FontAtlasGlyphMetaDiagnosticPrefix, s_FontAtlasGlyphYField, 0u, s_FontAtlasMaxExtent);
    if(!yResult)
        return MakeUnexpected(Failure{});
    glyph.y = *yResult;
    auto widthResult = ReadFontAtlasMetadataU32(path, map, s_FontAtlasGlyphMetaDiagnosticPrefix, s_FontAtlasGlyphWidthField, 0u, s_FontAtlasMaxExtent);
    if(!widthResult)
        return MakeUnexpected(Failure{});
    glyph.width = *widthResult;
    auto heightResult = ReadFontAtlasMetadataU32(path, map, s_FontAtlasGlyphMetaDiagnosticPrefix, s_FontAtlasGlyphHeightField, 0u, s_FontAtlasMaxExtent);
    if(!heightResult)
        return MakeUnexpected(Failure{});
    glyph.height = *heightResult;
    auto planeLeftResult = Core::Assets::ReadMetadataFiniteF32Field(path, map, s_FontAtlasGlyphMetaDiagnosticPrefix, s_FontAtlasGlyphPlaneLeftField, true);
    if(!planeLeftResult)
        return MakeUnexpected(Failure{});
    glyph.planeLeft = *planeLeftResult;
    auto planeTopResult = Core::Assets::ReadMetadataFiniteF32Field(path, map, s_FontAtlasGlyphMetaDiagnosticPrefix, s_FontAtlasGlyphPlaneTopField, true);
    if(!planeTopResult)
        return MakeUnexpected(Failure{});
    glyph.planeTop = *planeTopResult;
    if(glyph.width == 0u || glyph.height == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas glyph meta '{}': bitmap dimensions must be positive; empty glyphs declare only advance_units"), PathToString<tchar>(path));
        return MakeUnexpected(Failure{});
    }
    ComputeFontAtlasGlyphPlaneBounds(unitsPerPixel, glyph);
    return glyph;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetVector<FontAtlasGlyph>> ParseFontAtlasGlyphMetadata(const Path& nwbFilePath, const Core::Metascript::Value& asset, const FontAtlasPayload& payload){
    using namespace __hidden_font_atlas_cook_glyph_metadata;
    const auto glyphs = Core::Assets::FindMetadataListField(nwbFilePath, asset, s_FontAtlasGlyphMetaDiagnosticPrefix, s_FontAtlasGlyphsField);
    if(!glyphs)
        return MakeUnexpected(glyphs.error());
    if(payload.sourceGlyphCount == 0u || payload.sourceGlyphCount > s_FontAtlasMaxGlyphCount || (*glyphs)->asList().size() != payload.sourceGlyphCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas glyph meta '{}': exactly one record per source glyph is required"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    Core::Assets::AssetVector<FontAtlasGlyph> candidate(payload.glyphs.get_allocator().arena());
    candidate.reserve(payload.sourceGlyphCount);
    const f32 unitsPerPixel = ComputeFontAtlasUnitsPerPixel(payload.unitsPerEm, payload.bakePpem);
    for(u32 index = 0u; index < payload.sourceGlyphCount; ++index){
        auto glyphResult = ReadGlyph(nwbFilePath, (*glyphs)->asList()[index], index, unitsPerPixel);
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

