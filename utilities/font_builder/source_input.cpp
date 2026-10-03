// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_input.h"

#include <impl/assets_font/asset.h>
#include <impl/assets_font/prepared_source.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ReadFontSourceInput(const Path& path, Core::Assets::AssetBytes& outSfnt){
    if(LowerPathExtension<AString>(path) == ".font"){
        Impl::PreparedFontSource prepared(outSfnt.get_allocator().arena());
        if(!Impl::ReadPreparedFontSource(path, prepared, false) || prepared.faceIndex != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: malformed prepared .font input"));
            return false;
        }
        outSfnt = Move(prepared.fontBytes);
        return true;
    }
    ErrorCode error;
    const u64 sourceSize = FileSize(path, error);
    if(error || sourceSize == 0u || sourceSize > Impl::s_FontMaxSourceBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: source size is invalid or unreadable"));
        return false;
    }
    InputFileStream stream(path, InputFileStream::binary);
    if(!stream.is_open())
        return false;
    Core::Assets::AssetBytes input(outSfnt.get_allocator().arena());
    input.resize(static_cast<usize>(sourceSize));
    stream.read(reinterpret_cast<char*>(input.data()), static_cast<StreamSize>(sourceSize));
    if(stream.gcount() != static_cast<StreamSize>(sourceSize))
        return false;
    char extra = 0;
    stream.read(&extra, 1);
    if(stream.gcount() != 0 || !stream.eof())
        return false;

    outSfnt = Move(input);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

