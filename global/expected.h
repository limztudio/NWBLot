// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <expected>

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "NWB requires C++23 std::expected support."
#endif

#include "type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Failure final{};

template<typename T, typename E = Failure>
using Expected = std::expected<T, E>;

template<typename E>
using Unexpected = std::unexpected<E>;

template<typename E>
[[nodiscard]] constexpr Unexpected<Decay_T<E>> MakeUnexpected(E&& error)noexcept(
    noexcept(Unexpected<Decay_T<E>>(Forward<E>(error)))
){
    return Unexpected<Decay_T<E>>(Forward<E>(error));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

