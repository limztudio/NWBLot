// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"
#include "font_source.h"

#include <impl/assets_font_atlas/asset.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateOptions(const BakeOptions& options){
    if(
        options.ppem < Impl::s_FontAtlasMinBakePpem
        || options.ppem > Impl::s_FontAtlasMaxBakePpem
        || options.spread < Impl::s_FontAtlasMinSpreadPixels
        || options.spread > Impl::s_FontAtlasMaxSpreadPixels
        || options.extent < 32u
        || options.extent > Impl::s_FontAtlasMaxExtent
        || options.maxGroups == 0u
        || options.maxGroups > Impl::s_FontAtlasMaxGroupCount
        || static_cast<u64>(options.extent) * options.extent * 4u * options.maxGroups > Impl::s_FontAtlasMaxPixelBytes
        || options.source.empty()
        || options.output.empty()
    ){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("font_builder: invalid options; ppem16..256, spread2..32, extent32..2048, groups1..8, total capacity<=128MiB"));
        return false;
    }
    if(PathToGenericString<AString>(options.output.extension()) != ".nwb"){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("font_builder: --output must name a .nwb font asset bunch"));
        return false;
    }
    const AString stem = PathToGenericString<AString>(options.output.stem());
    if(stem.empty() || stem == "." || stem == ".."){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("font_builder: output must have a nonempty asset stem"));
        return false;
    }
    const AString sourceExtension = LowerPathExtension<AString>(options.source);
    if(sourceExtension != ".ttf" && sourceExtension != ".otf" && sourceExtension != ".font"){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("font_builder: --font must name a .ttf, .otf, or prepared .font source"));
        return false;
    }
    return true;
}

bool Bake(const BakeOptions& options, Impl::FontAtlasPayload& outPayload, Core::Alloc::ScratchArena& scratch){
    if(!ValidateOptions(options))
        return false;
    Impl::FontAtlasPayload candidate(outPayload.glyphs.get_allocator().arena());
    FontSource source(candidate.glyphs.get_allocator().arena());
    if(!source.open(options, candidate))
        return false;
    RasterGlyphs glyphs(candidate.glyphs.get_allocator().arena());
    if(
        !Rasterize(source, options, glyphs)
        || !PackGlyphs(options, glyphs, candidate, scratch)
        || !ExportPositioning(source, candidate)
    )
        return false;
    if(!Impl::ValidateFontAtlasPayload(candidate))
        return false;
    outPayload = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

