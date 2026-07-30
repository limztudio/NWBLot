// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"
#include "../thread.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool TextFileContains(const ::Path<ArenaT>& path, const AStringView needle){
    AString<ArenaT> text(path.arena());
    if(!ReadTextFile(path, text))
        return false;

    return AStringView(text.data(), text.size()).find(needle) != AStringView::npos;
}

template<typename ArenaT>
[[nodiscard]] inline bool WaitForDirectory(const ::Path<ArenaT>& path, const u32 timeoutMilliseconds, const u32 pollMilliseconds = 10u){
    const u32 stepMilliseconds = pollMilliseconds == 0u ? 1u : pollMilliseconds;
    for(u32 elapsedMilliseconds = 0u; elapsedMilliseconds <= timeoutMilliseconds; elapsedMilliseconds += stepMilliseconds){
        if(PathIsDirectory(path))
            return true;
        SleepMS(stepMilliseconds);
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

