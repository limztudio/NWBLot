// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_metadata.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BuildFontMetadata(const Impl::FontAtlasPayload& payload, MetadataString& outText){
    outText = "font face;\r\n";
    outText += "\r\nfont_atlas atlas;\r\natlas.font = face;\r\n";
    StringAppendFormat(outText, "atlas.bake_ppem = {};\r\n", payload.bakePpem);
    StringAppendFormat(outText, "atlas.spread_pixels = {};\r\n", payload.spreadPixels);
    StringAppendFormat(outText, "atlas.raster_mode = \"{}\";\r\n", payload.rasterMode == Impl::FontAtlasRasterMode::Bitmap ? "bitmap" : "outline");
    StringAppendFormat(outText, "atlas.ascender_units = {:#.9g};\r\n", payload.ascenderUnits);
    StringAppendFormat(outText, "atlas.descender_units = {:#.9g};\r\n", payload.descenderUnits);
    StringAppendFormat(outText, "atlas.line_gap_units = {:#.9g};\r\n", payload.lineGapUnits);
    outText += "atlas.glyphs = [\r\n";
    for(const auto& glyph : payload.glyphs){
        if(glyph.drawable == 0u){
            StringAppendFormat(outText, "    {{ \"advance_units\": {:#.9g} }},\r\n", glyph.advanceUnits);
            continue;
        }
        StringAppendFormat(outText,
            "    {{ \"group\": {}, \"channel\": {}, \"x\": {}, \"y\": {}, \"width\": {}, \"height\": {}, "
            "\"plane_left\": {:#.9g}, \"plane_top\": {:#.9g}, \"advance_units\": {:#.9g} }},\r\n",
            glyph.group, glyph.channel, glyph.x, glyph.y, glyph.width, glyph.height,
            glyph.planeLeft, glyph.planeTop, glyph.advanceUnits
        );
    }
    outText += "];\r\n\r\nasset_bunch bunch = [face, atlas];\r\n";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

