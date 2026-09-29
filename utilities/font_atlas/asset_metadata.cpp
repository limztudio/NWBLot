// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_metadata.h"

#include <impl/assets_font_atlas/source_payload.h>
#include <global/base64.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_ATLAS_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AString HashText(const Sha256Digest& hash){
    constexpr AStringView digits = "0123456789abcdef";
    AString text(64u, '0');
    for(u32 index = 0u; index < 32u; ++index){
        text[index * 2u] = digits[hash.bytes[index] >> 4u];
        text[index * 2u + 1u] = digits[hash.bytes[index] & 15u];
    }
    return text;
}

[[nodiscard]] static AString TableTag(const u32 tag){
    if(tag == Impl::s_FontAtlasKernTag)
        return "kern";
    if(tag == Impl::s_FontAtlasGposTag)
        return "GPOS";
    return "GDEF";
}

[[nodiscard]] static bool AppendPayload(MetadataString& text, const Core::Assets::AssetBytes& raw){
    Core::Assets::AssetBytes packed(raw.get_allocator().arena());
    MetadataString encoded(raw.get_allocator().arena());
    if(
        !Impl::FontAtlasSource::EncodePayload(raw, packed)
        || !EncodeBase64({packed.data(), packed.size()}, encoded)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("font_atlas: cannot encode embedded payload"));
        return false;
    }
    StringAppendFormat(text, ", \"packed_byte_count\": {}, \"data_base64\": [\r\n", packed.size());
    constexpr usize chunkChars = Impl::FontAtlasSource::s_Base64ChunkChars;
    for(usize start = 0u; start < encoded.size(); start += chunkChars){
        const usize remaining = encoded.size() - start;
        const usize count = remaining < chunkChars ? remaining : chunkChars;
        StringAppendFormat(text, "        \"{}\"{}\r\n"
            , AStringView(encoded.data() + start, count)
            , start + count == encoded.size() ? "" : ","
        );
    }
    StringAppendFormat(text, "    ]");
    return true;
}

[[nodiscard]] static bool AppendGroups(MetadataString& text, const Impl::FontAtlasPayload& payload){
    StringAppendFormat(text, "asset.groups = [\r\n");
    for(usize index = 0u; index < payload.groups.size(); ++index){
        const auto& group = payload.groups[index];
        StringAppendFormat(text, "    {{\"extent\": [{}, {}], \"byte_count\": {}, \"sha256\": \"{}\""
            , group.width, group.height, group.pixels.size(), HashText(group.sha256)
        );
        if(!AppendPayload(text, group.pixels))
            return false;
        StringAppendFormat(text, "}}{}\r\n", index + 1u == payload.groups.size() ? "" : ",");
    }
    StringAppendFormat(text, "];\r\n");
    return true;
}

static void AppendGlyphs(MetadataString& text, const Impl::FontAtlasPayload& payload){
    StringAppendFormat(text, "asset.glyphs = [\r\n");
    for(usize index = 0u; index < payload.glyphs.size(); ++index){
        const auto& glyph = payload.glyphs[index];
        StringAppendFormat(text, "    {{\"glyph_id\": {}, \"drawable\": {}, \"advance_units\": {}", glyph.glyphId, glyph.drawable, glyph.advanceUnits);
        if(glyph.drawable != 0u){
            StringAppendFormat(text, ", \"group\": {}, \"channel\": {}, \"rect\": [{}, {}, {}, {}], \"plane_bounds_units\": [{}, {}, {}, {}]"
                , glyph.group, glyph.channel, glyph.x, glyph.y, glyph.width, glyph.height
                , glyph.planeLeft, glyph.planeTop, glyph.planeRight, glyph.planeBottom
            );
        }
        StringAppendFormat(text, "}}{}\r\n", index + 1u == payload.glyphs.size() ? "" : ",");
    }
    StringAppendFormat(text, "];\r\n");
}

[[nodiscard]] static bool AppendPositioningTables(MetadataString& text, const Impl::FontAtlasPayload& payload){
    StringAppendFormat(text, "asset.kerning_mode = \"opentype_tables\";\r\nasset.positioning_tables = [\r\n");
    for(usize index = 0u; index < payload.positioningTables.size(); ++index){
        const auto& table = payload.positioningTables[index];
        StringAppendFormat(text, "    {{\"tag\": \"{}\", \"byte_count\": {}, \"sha256\": \"{}\""
            , TableTag(table.tag), table.bytes.size(), HashText(table.sha256)
        );
        if(!AppendPayload(text, table.bytes))
            return false;
        StringAppendFormat(text, "}}{}\r\n", index + 1u == payload.positioningTables.size() ? "" : ",");
    }
    StringAppendFormat(text, "];\r\n");
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildFontAtlasMetadata(const BakeOptions& options, const Impl::FontAtlasPayload& payload, MetadataString& outText){
    MetadataString text(outText.get_allocator().arena());
    const AString escapedFont = MakeJsonEscapedText<AString>(options.fontAsset);
    const AString sourceHash = __hidden_font_atlas_metadata::HashText(payload.fontSha256);
    StringAppendFormat(text, "// Generated by font_atlas schema{}; FreeType2.14.3 0a0221a1347e2f1e07c395263540026e9a0aa7c7.\r\n", Impl::FontAtlasSource::s_SchemaVersion);
    StringAppendFormat(text, "// Kerning scope: lossless original kern/GPOS/GDEF tables, not normalized pairs or a shaping result.\r\n");
    StringAppendFormat(text, "font_atlas asset;\r\n\r\nasset.schema_version = {};\r\nasset.payload_encoding = \"zstd_base64\";\r\n", Impl::FontAtlasSource::s_SchemaVersion);
    StringAppendFormat(text, "asset.font = \"{}\";\r\nasset.font_sha256 = \"{}\";\r\n", escapedFont, sourceHash);
    StringAppendFormat(text, "asset.face_index = {};\r\nasset.units_per_em = {};\r\nasset.source_glyph_count = {};\r\nasset.glyph_policy = \"all\";\r\n", payload.faceIndex, payload.unitsPerEm, payload.sourceGlyphCount);
    StringAppendFormat(text, "asset.bake_ppem = {};\r\nasset.sdf_renderer = \"{}\";\r\nasset.spread_pixels = {};\r\n", payload.bakePpem, options.outline ? "outline" : "bitmap", payload.spreadPixels);
    StringAppendFormat(text, "asset.distance_encoding = \"freetype_sdf_u8_v1\";\r\nasset.guard_texels = {};\r\nasset.payload_format = \"rgba8_linear\";\r\nasset.mip_count = 1;\r\n", payload.guardTexels);
    StringAppendFormat(text, "asset.ascender_units = {};\r\nasset.descender_units = {};\r\nasset.line_gap_units = {};\r\n", payload.ascenderUnits, payload.descenderUnits, payload.lineGapUnits);
    if(!__hidden_font_atlas_metadata::AppendGroups(text, payload))
        return false;
    __hidden_font_atlas_metadata::AppendGlyphs(text, payload);
    if(!__hidden_font_atlas_metadata::AppendPositioningTables(text, payload))
        return false;
    outText = Move(text);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_ATLAS_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

