// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "compile.h"
#include "type.h"

#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool EnvironmentVariableEquals(ArenaT& arena, const AStringView name, const AStringView expectedValue){
    AString<ArenaT> current(arena);
    if(!ReadEnvironmentVariable(name, current))
        return false;
    return AStringView(current.data(), current.size()) == expectedValue;
}

template<typename ArenaT>
[[nodiscard]] inline bool HasEnvironmentValue(ArenaT& arena, const AStringView name){
    AString<ArenaT> current(arena);
    return ReadEnvironmentVariable(name, current) && !current.empty();
}

template<typename ArenaT>
[[nodiscard]] inline bool ReadEnvironmentVariable(const AStringView name, AString<ArenaT>& outValue){
    if(name.empty() || name.find('\0') != AStringView::npos){
        outValue.clear();
        return false;
    }
    const AString<ArenaT> nativeName(name, outValue.get_allocator());
    outValue.clear();

#if defined(_MSC_VER)
    char* value = nullptr;
    size_t valueSize = 0u;
    if(::_dupenv_s(&value, &valueSize, nativeName.c_str()) != 0 || !value)
        return false;

    const usize valueLength = valueSize > 0u ? static_cast<usize>(valueSize - 1u) : static_cast<usize>(GLB_STRLEN(value));
    outValue.assign(value, valueLength);
    ::free(value);
    return true;
#else
    const char* const value = ::std::getenv(nativeName.c_str());
    if(!value)
        return false;

    outValue.assign(value);
    return true;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

