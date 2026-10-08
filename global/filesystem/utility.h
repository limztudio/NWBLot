// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"
#include "../text_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

