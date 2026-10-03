// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_metadata.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_builder_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static MetadataString DigestText(const Sha256Digest& digest, Core::Assets::AssetArena& arena){
    MetadataString text(arena);
    text.reserve(64u);
    for(const u8 byte : digest.bytes)
        StringAppendFormat(text, "{:02x}", static_cast<u32>(byte));
    return text;
}

static void AppendFaceFields(
    MetadataString& text,
    const AStringView variable,
    const Impl::FontAtlasPayload& payload,
    const MetadataString& sourceHash){
    StringAppendFormat(text, "{}.schema_version = 1;\r\n", variable);
    StringAppendFormat(text, "{}.face_index = {};\r\n", variable, payload.faceIndex);
    StringAppendFormat(text, "{}.units_per_em = {};\r\n", variable, payload.unitsPerEm);
    StringAppendFormat(text, "{}.glyph_count = {};\r\n", variable, payload.sourceGlyphCount);
    StringAppendFormat(text, "{}.source_sha256 = \"{}\";\r\n", variable, sourceHash);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BuildFontMetadata(const Impl::FontAtlasPayload& payload, MetadataString& outText){
    using namespace __hidden_font_builder_metadata;
    Core::Assets::AssetArena& arena = payload.glyphs.get_allocator().arena();
    const MetadataString sourceHash = DigestText(payload.fontSha256, arena);
    outText = "font face;\r\n";
    AppendFaceFields(outText, "face", payload, sourceHash);
    outText += "\r\nfont_atlas atlas;\r\natlas.font = face;\r\n";
    AppendFaceFields(outText, "atlas", payload, sourceHash);
    StringAppendFormat(outText, "atlas.bake_ppem = {};\r\n", payload.bakePpem);
    StringAppendFormat(outText, "atlas.spread_pixels = {};\r\n", payload.spreadPixels);
    StringAppendFormat(outText, "atlas.guard_texels = {};\r\n", payload.guardTexels);
    StringAppendFormat(outText, "atlas.raster_mode = \"{}\";\r\n", payload.rasterMode == Impl::FontAtlasRasterMode::Bitmap ? "bitmap" : "outline");
    StringAppendFormat(outText, "atlas.ascender_units = {:.9f};\r\n", payload.ascenderUnits);
    StringAppendFormat(outText, "atlas.descender_units = {:.9f};\r\n", payload.descenderUnits);
    StringAppendFormat(outText, "atlas.line_gap_units = {:.9f};\r\n", payload.lineGapUnits);
    outText += "atlas.groups = [\r\n";
    for(const auto& group : payload.groups){
        const MetadataString groupHash = DigestText(group.sha256, arena);
        StringAppendFormat(outText, "    {{ \"width\": {}, \"height\": {}, \"channels\": {}, \"sha256\": \"{}\" }},\r\n",
            group.width,
            group.height,
            group.channelCount,
            groupHash
        );
    }
    outText += "];\r\n\r\nasset_bunch bunch = [face, atlas];\r\n";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

