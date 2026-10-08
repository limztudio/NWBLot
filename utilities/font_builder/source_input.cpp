// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_input.h"

#include <impl/assets_font/asset.h>
#include <impl/assets_font/prepared_source.h>
#include <logger/client/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::Assets::AssetBytes, AStringView> ReadFontSourceInput(const Path& path, Core::Assets::AssetArena& arena){
    if(LowerPathExtension<AString>(path) == ".font"){
        auto prepared = Impl::ReadPreparedFontSource(path, arena, false);
        if(!prepared)
            return MakeUnexpected(prepared.error());
        if(prepared->faceIndex != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: malformed prepared .font input"));
            return MakeUnexpected(AStringView("prepared font face index must be zero"));
        }
        return Move(prepared->fontBytes);
    }
    const auto sourceSize = FileSize(path);
    if(!sourceSize || *sourceSize == 0u || *sourceSize > Impl::s_FontMaxSourceBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: source size is invalid or unreadable"));
        return MakeUnexpected(AStringView("source size is invalid or unreadable"));
    }
    InputFileStream stream(path, InputFileStream::binary);
    if(!stream.is_open())
        return MakeUnexpected(AStringView("failed to open font source"));
    Core::Assets::AssetBytes input(arena);
    input.resize(static_cast<usize>(*sourceSize));
    stream.read(reinterpret_cast<char*>(input.data()), static_cast<StreamSize>(*sourceSize));
    if(stream.gcount() != static_cast<StreamSize>(*sourceSize))
        return MakeUnexpected(AStringView("failed to read complete font source"));
    char extra = 0;
    stream.read(&extra, 1);
    if(stream.gcount() != 0 || !stream.eof())
        return MakeUnexpected(AStringView("font source changed while reading"));
    return input;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

