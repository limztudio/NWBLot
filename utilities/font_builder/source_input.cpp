// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_input.h"

#include <impl/assets_font/binary_payload.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ReadFontSourceInput(const Path& path, Core::Assets::AssetBytes& outSfnt){
    const bool prepared = LowerPathExtension<AString>(path) == ".font";
    const u64 headerBytes = prepared ? sizeof(Impl::FontBinaryPayload::HeaderBinary) : 0u;
    ErrorCode error;
    const u64 sourceSize = FileSize(path, error);
    if(error || sourceSize <= headerBytes || sourceSize > Impl::s_FontMaxSourceBytes + headerBytes){
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

    if(prepared){
        Impl::FontBinaryPayload::HeaderBinary header;
        NWB_MEMCPY(&header, sizeof(header), input.data(), sizeof(header));
        if(
            header.magic != Impl::FontBinaryPayload::s_FontMagic
            || header.version != Impl::FontBinaryPayload::s_FontVersion
            || header.faceIndex != 0u || header.reserved != 0u
            || header.byteCount != sourceSize - sizeof(header)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: malformed prepared .font input"));
            return false;
        }
        outSfnt.assign(input.begin() + sizeof(header), input.end());
    }
    else
        outSfnt = Move(input);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

