// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_font.h"
#include "source_input.h"

#include <impl/assets_font/binary_payload.h>
#include <global/binary.h>
#include <global/sha256.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildPreparedFont(const BakeOptions& options, const Impl::FontAtlasPayload& atlas, Core::Assets::AssetBytes& outBinary){
    Core::Assets::AssetBytes source(outBinary.get_allocator().arena());
    if(!ReadFontSourceInput(options.source, source))
        return false;
    if(ComputeSha256({ source.data(), source.size() }) != atlas.fontSha256){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: source changed during bake"));
        return false;
    }

    Impl::FontBinaryPayload::HeaderBinary header;
    header.byteCount = source.size();
    Core::Assets::AssetBytes binary(outBinary.get_allocator().arena());
    binary.reserve(sizeof(header) + source.size());
    AppendPOD(binary, header);
    binary.insert(binary.end(), source.begin(), source.end());
    outBinary = Move(binary);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

