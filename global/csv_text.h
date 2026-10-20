// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "string_view.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
inline void AppendCsvCell(StringT& out, const AStringView text)noexcept(noexcept(out.append(text.data(), text.size())) && noexcept(out += '"') && noexcept(out += "\"\"")){
    bool quote = false;
    for(const char ch : text){
        if(ch == ',' || ch == '"' || ch == '\n' || ch == '\r'){
            quote = true;
            break;
        }
    }

    if(!quote){
        out.append(text.data(), text.size());
        return;
    }

    out += '"';
    for(const char ch : text){
        if(ch == '"')
            out += "\"\"";
        else
            out += ch;
    }
    out += '"';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

