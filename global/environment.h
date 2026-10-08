// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "compile.h"
#include "expected.h"
#include "scope_exit.h"
#include "type.h"

#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<AString<ArenaT>> ReadEnvironmentVariable(ArenaT& arena, const AStringView name){
    if(name.empty() || name.find('\0') != AStringView::npos)
        return MakeUnexpected(Failure{});
    const AString<ArenaT> nativeName(name, arena);
#if defined(_MSC_VER)
    char* value = nullptr;
    size_t valueSize = 0u;
    const int result = ::_dupenv_s(&value, &valueSize, nativeName.c_str());
    ScopeExit freeValue([&]()noexcept{ ::free(value); });
    if(result != 0 || !value)
        return MakeUnexpected(Failure{});
    const usize valueLength = valueSize > 0u ? static_cast<usize>(valueSize - 1u) : static_cast<usize>(NWB_STRLEN(value));
    return AString<ArenaT>(value, valueLength, arena);
#else
    const char* const value = ::std::getenv(nativeName.c_str());
    if(!value)
        return MakeUnexpected(Failure{});
    return AString<ArenaT>(value, arena);
#endif
}

template<typename ArenaT>
[[nodiscard]] inline bool EnvironmentVariableEquals(ArenaT& arena, const AStringView name, const AStringView expectedValue){
    const auto current = ReadEnvironmentVariable(arena, name);
    return current && AStringView(current->data(), current->size()) == expectedValue;
}

template<typename ArenaT>
[[nodiscard]] inline bool HasEnvironmentValue(ArenaT& arena, const AStringView name){
    const auto current = ReadEnvironmentVariable(arena, name);
    return current && !current->empty();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

