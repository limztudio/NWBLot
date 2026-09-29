// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "font_source.h"

#include <logger/client/logger.h>

#include FT_TRUETYPE_TABLES_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ExportPositioning(const FontSource& font, Impl::FontAtlasPayload& payload){
    constexpr u32 tags[] = { Impl::s_FontAtlasGdefTag, Impl::s_FontAtlasGposTag, Impl::s_FontAtlasKernTag };
    u64 totalBytes = 0u;
    for(const u32 tag : tags){
        FT_ULong size = 0u;
        const FT_Error probe = FT_Load_Sfnt_Table(font.face(), tag, 0, nullptr, &size);
        if(probe == FT_Err_Table_Missing)
            continue;
        if(probe != 0 || size == 0u || size > Impl::s_FontAtlasMaxPositioningBytes - totalBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: cannot admit source positioning table {}"), tag);
            return false;
        }
        Impl::FontAtlasPositioningTable table(payload.positioningTables.get_allocator().arena());
        table.tag = tag;
        table.bytes.resize(size);
        if(FT_Load_Sfnt_Table(font.face(), tag, 0, table.bytes.data(), &size) != 0 || size != table.bytes.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: failed to copy source positioning table {}"), tag);
            return false;
        }
        table.sha256 = ComputeSha256(BinaryByteView{ .bytes = table.bytes.data(), .byteCount = table.bytes.size() });
        if(!Impl::ValidateFontAtlasPositioningTable(table, payload.sourceGlyphCount))
            return false;
        totalBytes += size;
        NWB_LOGGER_INFO(NWB_TEXT("font_builder: retained {} source positioning bytes for tag {}; raw table inspection scope"), size, tag);
        payload.positioningTables.push_back(Move(table));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

