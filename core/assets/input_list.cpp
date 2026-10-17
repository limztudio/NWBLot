// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "input_list.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ReadAssetInputList(const Path& path, AssetVector<AssetString>& inputs, Alloc::ScratchArena& scratchArena, const bool resolveRelativePaths){
    AString<Alloc::ScratchArena> text(scratchArena);
    if(!ReadTextFile(path, text) || HasEmbeddedNull(AStringView(text))){
        NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: failed to read input list '{}'"), PathToString<tchar>(path));
        return false;
    }
    StripUtf8Bom(text);

    AssetArena& arena = inputs.get_allocator().arena();
    usize cursor = 0u;
    while(cursor < text.size()){
        usize end = text.find('\n', cursor);
        if(end == AStringView::npos)
            end = text.size();
        usize length = end - cursor;
        if(length > 0u && text[cursor + length - 1u] == '\r')
            --length;
        if(length == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Pipeline: input list contains an empty path '{}'"), PathToString<tchar>(path));
            return false;
        }
        const AStringView value(text.data() + cursor, length);
        if(resolveRelativePaths){
            ::Path<Alloc::ScratchArena> resolved(scratchArena, value);
            if(!resolved.isAbsolute())
                resolved = ::Path<Alloc::ScratchArena>(scratchArena, path).parentPath() / resolved;
            inputs.emplace_back(PathToString(arena, resolved.lexicallyNormal()));
        }
        else
            inputs.emplace_back(value, arena);
        cursor = end + 1u;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

