// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "compile.h"
#include "expected.h"
#include "error_code.h"
#include "optional.h"
#include "scope_exit.h"
#include "type.h"

#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Optional<AString<ArenaT>>, ErrorCode> ReadEnvironmentVariable(ArenaT& arena, const AStringView name){
    if(name.empty() || name.find('\0') != AStringView::npos || name.find('=') != AStringView::npos)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    const AString<ArenaT> nativeName(name, arena);
#if defined(_MSC_VER)
    char* value = nullptr;
    size_t valueSize = 0u;
    const int result = ::_dupenv_s(&value, &valueSize, nativeName.c_str());
    ScopeExit freeValue([&]()noexcept{ ::free(value); });
    if(result != 0)
        return MakeUnexpected(ErrorCode(result, std::generic_category()));
    if(!value)
        return Optional<AString<ArenaT>>{};
    const usize valueLength = valueSize > 0u ? static_cast<usize>(valueSize - 1u) : static_cast<usize>(NWB_STRLEN(value));
    return Optional<AString<ArenaT>>(AString<ArenaT>(value, valueLength, arena));
#else
    const char* const value = ::std::getenv(nativeName.c_str());
    if(!value)
        return Optional<AString<ArenaT>>{};
    return Optional<AString<ArenaT>>(AString<ArenaT>(value, arena));
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> EnvironmentVariableEquals(ArenaT& arena, const AStringView name, const AStringView expectedValue){
    const auto current = ReadEnvironmentVariable(arena, name);
    if(!current)
        return MakeUnexpected(current.error());
    return *current && AStringView((*current)->data(), (*current)->size()) == expectedValue;
}

template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> HasEnvironmentValue(ArenaT& arena, const AStringView name){
    const auto current = ReadEnvironmentVariable(arena, name);
    if(!current)
        return MakeUnexpected(current.error());
    return *current && !(*current)->empty();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

