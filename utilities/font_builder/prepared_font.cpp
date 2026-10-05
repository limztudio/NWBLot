// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_font.h"
#include "source_input.h"

#include <impl/assets_font/prepared_source.h>
#include <global/sha256.h>
#include <logger/client/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildPreparedFont(const BakeOptions& options, const Impl::FontAtlasPayload& atlas, Core::Assets::AssetBytes& outBinary){
    Core::Assets::AssetBytes source(outBinary.get_allocator().arena());
    if(!ReadFontSourceInput(options.source, source))
        return false;
    if(ComputeSha256({ source.data(), source.size() }) != atlas.fontSha256){
        NWB_LOGGER_ERROR(GLB_TEXT("font_builder: source changed during bake"));
        return false;
    }

    Core::Assets::AssetVector<Impl::PreparedFontImageView> images(outBinary.get_allocator().arena());
    images.reserve(atlas.groups.size());
    for(const auto& group : atlas.groups){
        images.push_back(Impl::PreparedFontImageView{
            .width = group.width,
            .height = group.height,
            .channelCount = group.channelCount,
            .pixels = { group.pixels.data(), group.pixels.size() },
        });
    }
    return Impl::SerializePreparedFontSource(
        { source.data(), source.size() }, atlas.faceIndex, images.data(), images.size(), outBinary
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

