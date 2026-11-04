// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expected.h"
#include "text_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CallTextParts{
    AStringView name;
    AStringView arguments;
};

// Returned views borrow from text.
[[nodiscard]] inline Expected<CallTextParts> SplitNonemptyCallText(const AStringView text)noexcept{
    const AStringView trimmed = TrimView(text);
    usize openParen = trimmed.size();
    for(usize i = 0u; i < trimmed.size(); ++i){
        if(trimmed[i] == '('){
            openParen = i;
            break;
        }
    }
    if(openParen >= trimmed.size() || trimmed.empty() || trimmed[trimmed.size() - 1u] != ')')
        return MakeUnexpected(Failure{});

    const AStringView name = TrimView(trimmed.substr(0u, openParen));
    const AStringView arguments = TrimView(trimmed.substr(openParen + 1u, trimmed.size() - openParen - 2u));
    if(name.empty() || arguments.empty())
        return MakeUnexpected(Failure{});
    return CallTextParts{ name, arguments };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

