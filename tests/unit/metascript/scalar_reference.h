// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include <engine_namespace.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::Metascript{

template<typename T>
[[nodiscard]] FOUNDATION_NOINLINE T ScalarMultiplyReference(T a, T b)noexcept{
#pragma STDC FENV_ACCESS ON
    return a * b;
}

template<typename T>
[[nodiscard]] FOUNDATION_NOINLINE T ScalarNegateReference(T a)noexcept{
#pragma STDC FENV_ACCESS ON
    return -a;
}

template<typename T>
[[nodiscard]] FOUNDATION_NOINLINE T ScalarPostIncrementReference(T a)noexcept{
#pragma STDC FENV_ACCESS ON
    return a++;
}

template<typename T>
[[nodiscard]] FOUNDATION_NOINLINE T ScalarPreIncrementReference(T a)noexcept{
#pragma STDC FENV_ACCESS ON
    return ++a;
}

template<typename T>
[[nodiscard]] FOUNDATION_NOINLINE T ScalarPostDecrementReference(T a)noexcept{
#pragma STDC FENV_ACCESS ON
    return a--;
}

template<typename T>
[[nodiscard]] FOUNDATION_NOINLINE T ScalarPreDecrementReference(T a)noexcept{
#pragma STDC FENV_ACCESS ON
    return --a;
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

