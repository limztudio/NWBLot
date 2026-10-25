// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_input.h"

#include <impl/assets_font/asset.h>
#include <impl/assets_font/prepared_source.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetBytes, AStringView> ReadFontSourceInput(const Path& path, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratch){
    static_cast<void>(scratch);
    constexpr AStringView s_PreparedFontExtensions[] = { s_PreparedFontExtension };
    if(PathHasListedExtension(path, s_PreparedFontExtensions)){
        auto prepared = Impl::ReadPreparedFontSource(path, arena, false);
        if(!prepared)
            return MakeUnexpected(prepared.error());
        if(prepared->faceIndex != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: malformed prepared .font input"));
            return MakeUnexpected(AStringView("prepared font face index must be zero"));
        }
        return Move(prepared->fontBytes);
    }
    InputFileStream stream(path, InputFileStream::binary | InputFileStream::ate);
    if(!stream.is_open())
        return MakeUnexpected(AStringView("failed to open font source"));
    const StreamOffset sourceSize = stream.tellg();
    if(!stream || sourceSize <= 0 || static_cast<u64>(sourceSize) > Impl::s_FontMaxSourceBytes || !CanRepresentStreamSize(static_cast<u64>(sourceSize))){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: source size is invalid or unreadable"));
        return MakeUnexpected(AStringView("source size is invalid or unreadable"));
    }
    stream.seekg(0, InputFileStream::beg);
    if(!stream)
        return MakeUnexpected(AStringView("failed to seek font source"));
    Core::Assets::AssetBytes input(arena);
    input.resize(static_cast<usize>(sourceSize));
    if(!ReadStreamExact(stream, NotNull<void*>(input.data()), input.size()))
        return MakeUnexpected(AStringView("failed to read complete font source"));
    if(!VerifyStreamAtEnd(stream))
        return MakeUnexpected(AStringView("font source changed while reading"));
    return input;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

