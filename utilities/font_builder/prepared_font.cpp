// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_font.h"
#include "source_input.h"

#include <impl/assets_font/prepared_source.h>

#include <core/common/log.h>

#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetBytes, AStringView> BuildPreparedFont(const BakeOptions& options, const Impl::FontAtlasPayload& atlas){
    Core::Assets::AssetArena& arena = atlas.glyphs.get_allocator().arena();
    auto source = ReadFontSourceInput(options.source, arena);
    if(!source)
        return MakeUnexpected(source.error());
    if(ComputeSha256({ source->data(), source->size() }) != atlas.fontSha256){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: source changed during bake"));
        return MakeUnexpected(AStringView("source changed during bake"));
    }

    Core::Assets::AssetVector<Impl::PreparedFontImageView> images(arena);
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
        { source->data(), source->size() }, atlas.faceIndex, images.data(), images.size(), arena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

