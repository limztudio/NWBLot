// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"
#include "font_source.h"

#include <impl/assets_font/prepared_source.h>
#include <impl/assets_font_atlas/asset.h>

#include <core/common/log.h>

#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateOptions(const BakeOptions& options){
    if(
        !IsValidBakeNumericOptions(options.ppem, options.spread, options.extent, options.maxGroups)
        || options.source.empty()
        || options.output.empty()
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: invalid options; ppem16..256, spread2..32, extent32..2048, groups1..8, total capacity<=128MiB"));
        return false;
    }
    if(options.output.extensionView() != NWB_TEXT(".nwb")){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: --output must name a .nwb font asset bunch"));
        return false;
    }
    const auto stem = options.output.stemView();
    if(stem.empty() || IsDot(stem) || IsDotDot(stem)){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: output must have a nonempty asset stem"));
        return false;
    }
    static constexpr Array<AStringView, 3u> s_SourceExtensions = { s_TrueTypeExtension, s_OpenTypeExtension, Impl::s_PreparedFontExtension };
    if(!PathHasListedExtension(options.source, s_SourceExtensions)){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: --font must name a .ttf, .otf, or prepared .font source"));
        return false;
    }
    return true;
}

Expected<Impl::FontAtlasPayload> Bake(const BakeOptions& options, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratch, Core::Assets::AssetBytes& outSourceFontBytes){
    Impl::FontAtlasPayload candidate(arena);
    FontSource source(candidate.glyphs.get_allocator().arena());
    if(!source.open(options, scratch, candidate))
        return MakeUnexpected(Failure{});
    auto glyphs = Rasterize(options, source);
    if(!glyphs || !PackGlyphs(options, *glyphs, scratch, candidate) || !ExportPositioning(source, candidate))
        return MakeUnexpected(Failure{});
    if(!Impl::ValidateFontAtlasPayload(candidate, scratch))
        return MakeUnexpected(Failure{});
    outSourceFontBytes = source.takeBytes();
    return candidate;
}

void BuildFontMetadata(const Impl::FontAtlasPayload& payload, MetadataString& outText){
    outText = "font face;\r\n";
    outText += "\r\nfont_atlas atlas;\r\natlas.font = face;\r\n";
    StringAppendFormat(outText, "atlas.bake_ppem = {};\r\n", payload.bakePpem);
    StringAppendFormat(outText, "atlas.spread_pixels = {};\r\n", payload.spreadPixels);
    StringAppendFormat(outText, "atlas.raster_mode = \"{}\";\r\n", payload.rasterMode == Impl::FontAtlasRasterMode::Bitmap ? Impl::s_FontAtlasRasterModeBitmap : Impl::s_FontAtlasRasterModeOutline);
    StringAppendFormat(outText, "atlas.ascender_units = {:#.9g};\r\n", payload.ascenderUnits);
    StringAppendFormat(outText, "atlas.descender_units = {:#.9g};\r\n", payload.descenderUnits);
    StringAppendFormat(outText, "atlas.line_gap_units = {:#.9g};\r\n", payload.lineGapUnits);
    outText += "atlas.glyphs = [\r\n";
    for(const auto& glyph : payload.glyphs){
        if(glyph.drawable == 0u){
            StringAppendFormat(outText, "    {{ \"advance_units\": {:#.9g} }},\r\n", glyph.advanceUnits);
            continue;
        }
        StringAppendFormat(
            outText,
            "    {{ \"group\": {}, \"channel\": {}, \"x\": {}, \"y\": {}, \"width\": {}, \"height\": {}, "
            "\"plane_left\": {:#.9g}, \"plane_top\": {:#.9g}, \"advance_units\": {:#.9g} }},\r\n",
            glyph.group, glyph.channel, glyph.x, glyph.y, glyph.width, glyph.height,
            glyph.planeLeft, glyph.planeTop, glyph.advanceUnits
        );
    }
    outText += "];\r\n\r\nasset_bunch bunch = [face, atlas];\r\n";
}

Expected<Core::Assets::AssetBytes, AStringView> BuildPreparedFont(const Span<const u8> sourceFontBytes, const Impl::FontAtlasPayload& atlas, Core::Alloc::ScratchArena& scratch){
    Core::Assets::AssetArena& arena = atlas.glyphs.get_allocator().arena();
    if(ComputeSha256({ sourceFontBytes.data(), sourceFontBytes.size() }) != atlas.fontSha256){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: source changed during bake"));
        return MakeUnexpected(AStringView("source changed during bake"));
    }

    Vector<Impl::PreparedFontImageView, Core::Alloc::ScratchArena> images(scratch);
    images.reserve(atlas.groups.size());
    for(const auto& group : atlas.groups){
        images.push_back(Impl::PreparedFontImageView(group.width, group.height, group.channelCount, { group.pixels.data(), group.pixels.size() }));
    }
    return Impl::SerializePreparedFontSource({ sourceFontBytes.data(), sourceFontBytes.size() }, atlas.faceIndex, images, arena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

