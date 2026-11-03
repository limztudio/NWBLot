// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "input_list.h"

#include <core/common/log.h>

#include <global/filesystem/utility.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ScratchString> EncodeAssetInputList(const Span<const AssetString> inputs, Alloc::ScratchArena& scratchArena){
    ScratchString text(scratchArena);
    usize size = 0u;
    for(const auto& input : inputs){
        if(!IsSingleLinePathText(AStringView(input)) || size >= text.max_size() || input.size() > text.max_size() - size - 1u)
            return MakeUnexpected(Failure{});
        size += input.size() + 1u;
    }
    text.reserve(size);
    for(const auto& input : inputs){
        text += input;
        text += '\n';
    }
    return text;
}


b8 ReadAssetInputList(const Path& path, Alloc::ScratchArena& scratchArena, AssetVector<AssetString>& inputs, const b8 resolveRelativePaths){
    AString<Alloc::ScratchArena> text(scratchArena);
    if(!ReadTextFile(path, text) || HasEmbeddedNull(AStringView(text))){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetInputList: failed to read input list '{}'"), PathToString<tchar>(path));
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
            NWB_LOGGER_ERROR(NWB_TEXT("AssetInputList: input list contains an empty path '{}'"), PathToString<tchar>(path));
            return false;
        }
        const AStringView value(text.data() + cursor, length);
        if(resolveRelativePaths){
            ::Path<Alloc::ScratchArena> resolved(scratchArena, value);
            if(!resolved.isAbsolute())
                resolved = ::Path<Alloc::ScratchArena>(scratchArena, path.parentPathView()) / resolved;
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

