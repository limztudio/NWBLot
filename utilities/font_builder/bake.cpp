// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"
#include "font_source.h"

#include <impl/assets_font_atlas/asset.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateOptions(const BakeOptions& options){
    constexpr u32 s_MinExtent = 32u;
    if(
        options.ppem < Impl::s_FontAtlasMinBakePpem
        || options.ppem > Impl::s_FontAtlasMaxBakePpem
        || options.spread < Impl::s_FontAtlasMinSpreadPixels
        || options.spread > Impl::s_FontAtlasMaxSpreadPixels
        || options.extent < s_MinExtent
        || options.extent > Impl::s_FontAtlasMaxExtent
        || options.maxGroups == 0u
        || options.maxGroups > Impl::s_FontAtlasMaxGroupCount
        || static_cast<u64>(options.extent) * options.extent * Impl::s_FontAtlasMaxChannelCount * options.maxGroups > Impl::s_FontAtlasMaxPixelBytes
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
    constexpr AStringView s_SourceExtensions[] = { s_TrueTypeExtension, s_OpenTypeExtension, s_PreparedFontExtension };
    if(!PathHasListedExtension(options.source, s_SourceExtensions)){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: --font must name a .ttf, .otf, or prepared .font source"));
        return false;
    }
    return true;
}

Expected<Impl::FontAtlasPayload> Bake(const BakeOptions& options, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratch){
    Impl::FontAtlasPayload candidate(arena);
    FontSource source(candidate.glyphs.get_allocator().arena());
    if(!source.open(options, candidate, scratch))
        return MakeUnexpected(Failure{});
    auto glyphs = Rasterize(source, options);
    if(!glyphs || !PackGlyphs(options, *glyphs, scratch, candidate) || !ExportPositioning(source, candidate))
        return MakeUnexpected(Failure{});
    if(!Impl::ValidateFontAtlasPayload(candidate, scratch))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

