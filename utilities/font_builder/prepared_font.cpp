// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"

#include <impl/assets_font/prepared_source.h>

#include <core/common/log.h>

#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

Expected<Core::Assets::AssetBytes, AStringView> BuildPreparedFont(const BakeOptions& options, const Impl::FontAtlasPayload& atlas, Core::Alloc::ScratchArena& scratch){
    Core::Assets::AssetArena& arena = atlas.glyphs.get_allocator().arena();
    auto source = ReadFontSourceInput(options.source, arena);
    if(!source)
        return MakeUnexpected(source.error());
    return BuildPreparedFont(Span<const u8>(*source), atlas, scratch);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

