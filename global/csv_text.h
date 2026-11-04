// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "string_view.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
void AppendCsvCell(const AStringView text, StringT& out)noexcept(noexcept(out.append(text.data(), text.size())) && noexcept(out += '"') && noexcept(out += "\"\"")){
    b8 quote = false;
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

