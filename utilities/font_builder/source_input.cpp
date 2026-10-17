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
    const ScratchPath source(scratch, path);
    if(ToAsciiLowerCopy(PathToGenericString(scratch, source.extension())) == ".font"){
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
    const GlobalFilesystemDetail::StreamOffset sourceSize = stream.tellg();
    if(!stream || sourceSize <= 0 || static_cast<u64>(sourceSize) > Impl::s_FontMaxSourceBytes || !GlobalFilesystemDetail::CanRepresentStreamSize(static_cast<u64>(sourceSize))){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: source size is invalid or unreadable"));
        return MakeUnexpected(AStringView("source size is invalid or unreadable"));
    }
    stream.seekg(0, InputFileStream::beg);
    if(!stream)
        return MakeUnexpected(AStringView("failed to seek font source"));
    Core::Assets::AssetBytes input(arena);
    input.resize(static_cast<usize>(sourceSize));
    stream.read(reinterpret_cast<char*>(input.data()), static_cast<StreamSize>(sourceSize));
    if(stream.gcount() != static_cast<StreamSize>(sourceSize) || stream.bad())
        return MakeUnexpected(AStringView("failed to read complete font source"));
    char extra = 0;
    stream.read(&extra, 1);
    if(stream.gcount() != 0 || !stream.eof() || stream.bad())
        return MakeUnexpected(AStringView("font source changed while reading"));
    return input;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

