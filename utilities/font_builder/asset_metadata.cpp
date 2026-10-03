// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_metadata.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_builder_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendFaceFields(
    MetadataString& text,
    const AStringView variable,
    const Impl::FontAtlasPayload& payload){
    StringAppendFormat(text, "{}.face_index = {};\r\n", variable, payload.faceIndex);
    StringAppendFormat(text, "{}.units_per_em = {};\r\n", variable, payload.unitsPerEm);
    StringAppendFormat(text, "{}.glyph_count = {};\r\n", variable, payload.sourceGlyphCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BuildFontMetadata(const Impl::FontAtlasPayload& payload, MetadataString& outText){
    using namespace __hidden_font_builder_metadata;
    outText = "font face;\r\n";
    AppendFaceFields(outText, "face", payload);
    outText += "\r\nfont_atlas atlas;\r\natlas.font = face;\r\n";
    AppendFaceFields(outText, "atlas", payload);
    StringAppendFormat(outText, "atlas.bake_ppem = {};\r\n", payload.bakePpem);
    StringAppendFormat(outText, "atlas.spread_pixels = {};\r\n", payload.spreadPixels);
    StringAppendFormat(outText, "atlas.guard_texels = {};\r\n", payload.guardTexels);
    StringAppendFormat(outText, "atlas.raster_mode = \"{}\";\r\n", payload.rasterMode == Impl::FontAtlasRasterMode::Bitmap ? "bitmap" : "outline");
    StringAppendFormat(outText, "atlas.ascender_units = {:#.9g};\r\n", payload.ascenderUnits);
    StringAppendFormat(outText, "atlas.descender_units = {:#.9g};\r\n", payload.descenderUnits);
    StringAppendFormat(outText, "atlas.line_gap_units = {:#.9g};\r\n", payload.lineGapUnits);
    outText += "atlas.groups = [\r\n";
    for(const auto& group : payload.groups){
        StringAppendFormat(outText, "    {{ \"width\": {}, \"height\": {}, \"channels\": {} }},\r\n",
            group.width,
            group.height,
            group.channelCount
        );
    }
    outText += "];\r\natlas.glyphs = [\r\n";
    for(const auto& glyph : payload.glyphs){
        StringAppendFormat(outText,
            "    {{ \"id\": {}, \"group\": {}, \"channel\": {}, \"x\": {}, \"y\": {}, \"width\": {}, \"height\": {}, "
            "\"plane_left\": {:#.9g}, \"plane_top\": {:#.9g}, \"plane_right\": {:#.9g}, \"plane_bottom\": {:#.9g}, "
            "\"advance_units\": {:#.9g}, \"drawable\": {} }},\r\n",
            glyph.glyphId, glyph.group, glyph.channel, glyph.x, glyph.y, glyph.width, glyph.height,
            glyph.planeLeft, glyph.planeTop, glyph.planeRight, glyph.planeBottom, glyph.advanceUnits,
            glyph.drawable
        );
    }
    outText += "];\r\n\r\nasset_bunch bunch = [face, atlas];\r\n";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

