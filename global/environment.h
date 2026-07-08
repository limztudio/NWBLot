// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "compile.h"
#include "type.h"

#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool ReadEnvironmentVariable(const char* name, AString<ArenaT>& outValue){
    outValue.clear();
    if(!name || name[0] == 0)
        return false;

#if defined(_MSC_VER)
    char* value = nullptr;
    size_t valueSize = 0u;
    if(::_dupenv_s(&value, &valueSize, name) != 0 || !value)
        return false;

    const usize valueLength = valueSize > 0u ? static_cast<usize>(valueSize - 1u) : static_cast<usize>(NWB_STRLEN(value));
    outValue.assign(value, valueLength);
    ::free(value);
    return true;
#else
    const char* const value = ::std::getenv(name);
    if(!value)
        return false;

    outValue.assign(value);
    return true;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
