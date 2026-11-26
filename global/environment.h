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


namespace EnvironmentDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename OnPresent, typename OnMissing>
[[nodiscard]] inline auto VisitEnvironmentVariable(
    ArenaT& arena,
    const AStringView name,
    OnPresent onPresent,
    OnMissing onMissing
) -> decltype(onPresent(AStringView{})){
    using ResultT = decltype(onPresent(AStringView{}));
    if(name.empty() || name.find('\0') != AStringView::npos || name.find('=') != AStringView::npos)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    const AString<ArenaT> nativeName(name, arena);
#if defined(_MSC_VER)
    char* value = nullptr;
    size_t valueSize = 0u;
    const int result = ::_dupenv_s(&value, &valueSize, nativeName.c_str());
    ScopeExit freeValue([&]()noexcept{ ::free(value); });
    if(result != 0)
        return MakeUnexpected(ErrorCode(result, GenericCategory()));
    if(!value)
        return ResultT(onMissing());
    const usize valueLength = valueSize > 0u ? static_cast<usize>(valueSize - 1u) : static_cast<usize>(FOUNDATION_STRLEN(value));
    return onPresent(AStringView(value, valueLength));
#else
    const char* const value = ::std::getenv(nativeName.c_str());
    if(!value)
        return ResultT(onMissing());
    return onPresent(AStringView(value));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Optional<AString<ArenaT>>, ErrorCode> ReadEnvironmentVariable(ArenaT& arena, const AStringView name){
    return EnvironmentDetail::VisitEnvironmentVariable(
        arena,
        name,
        [&arena](const AStringView value){
            return Expected<Optional<AString<ArenaT>>, ErrorCode>(Optional<AString<ArenaT>>(AString<ArenaT>(value, arena)));
        },
        [](){ return Optional<AString<ArenaT>>{}; }
    );
}

template<typename ArenaT, typename Predicate>
[[nodiscard]] inline Expected<b8, ErrorCode> EvaluateEnvironmentVariable(ArenaT& arena, const AStringView name, Predicate predicate){
    return EnvironmentDetail::VisitEnvironmentVariable(
        arena,
        name,
        [&predicate](const AStringView value){
            return Expected<b8, ErrorCode>(static_cast<b8>(predicate(value)));
        },
        [](){ return false; }
    );
}

template<typename ArenaT>
[[nodiscard]] inline Expected<b8, ErrorCode> EnvironmentVariableEquals(ArenaT& arena, const AStringView name, const AStringView expectedValue){
    return EvaluateEnvironmentVariable(arena, name, [expectedValue](const AStringView value){ return value == expectedValue; });
}

template<typename ArenaT>
[[nodiscard]] inline Expected<b8, ErrorCode> HasEnvironmentValue(ArenaT& arena, const AStringView name){
    return EvaluateEnvironmentVariable(arena, name, [](const AStringView value){ return !value.empty(); });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

