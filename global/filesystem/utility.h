// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"
#include "../text_utils.h"
#include "../thread.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_DefaultDirectoryPollMilliseconds = 10u;
inline constexpr u32 s_MinimumDirectoryPollMilliseconds = 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool TextFileContains(const ::Path<ArenaT>& path, const AStringView needle){
    AString<ArenaT> text(path.arena());
    if(!ReadTextFile(path, text))
        return false;

    return AStringView(text.data(), text.size()).find(needle) != AStringView::npos;
}

template<typename ArenaT>
[[nodiscard]] inline bool WaitForDirectory(
    const ::Path<ArenaT>& path,
    const u32 timeoutMilliseconds,
    const u32 pollMilliseconds = GlobalFilesystemDetail::s_DefaultDirectoryPollMilliseconds
){
    const u32 stepMilliseconds = pollMilliseconds == 0u
        ? GlobalFilesystemDetail::s_MinimumDirectoryPollMilliseconds
        : pollMilliseconds;
    for(u32 elapsedMilliseconds = 0u; elapsedMilliseconds <= timeoutMilliseconds; elapsedMilliseconds += stepMilliseconds){
        if(PathIsDirectory(path))
            return true;
        SleepMS(stepMilliseconds);
    }
    return false;
}

template<typename StringT, typename PathT>
[[nodiscard]] inline StringT LowerPathExtension(const PathT& path){
    return ToAsciiLowerCopy(PathToGenericString<StringT>(path.extension()));
}

template<typename PathT, typename ExtensionArray>
[[nodiscard]] inline bool PathHasListedExtension(const PathT& path, const ExtensionArray& extensions){
    using PathChar = typename PathT::value_type;
    const auto extensionPath = path.extension();
    const auto extension = extensionPath.native();
    for(usize i = 0u; i < LengthOf(extensions); ++i){
        const AStringView listed(extensions[i].data(), extensions[i].size());
        if(extension.size() != listed.size())
            continue;
        bool matched = true;
        for(usize c = 0u; c < extension.size(); ++c){
            if(ToAsciiLower(extension[c]) != static_cast<PathChar>(ToAsciiLower(listed[c]))){
                matched = false;
                break;
            }
        }
        if(matched)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

