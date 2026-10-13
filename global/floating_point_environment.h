// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expected.h"
#include "type.h"

#include <cfenv>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using FloatingPointEnvironment = std::fenv_t;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<FloatingPointEnvironment> GetFloatingPointEnvironment()noexcept{
    FloatingPointEnvironment environment{};
    if(std::fegetenv(&environment) != 0)
        return MakeUnexpected(Failure{});
    return environment;
}

[[nodiscard]] inline Expected<FloatingPointEnvironment> HoldFloatingPointExceptions()noexcept{
    FloatingPointEnvironment environment{};
    if(std::feholdexcept(&environment) != 0)
        return MakeUnexpected(Failure{});
    return environment;
}

[[nodiscard]] inline Expected<void> SetFloatingPointEnvironment(const FloatingPointEnvironment& environment)noexcept{
    if(std::fesetenv(&environment) != 0)
        return MakeUnexpected(Failure{});
    return {};
}

[[nodiscard]] inline Expected<void> ClearFloatingPointExceptions(const i32 exceptions)noexcept{
    if(std::feclearexcept(exceptions) != 0)
        return MakeUnexpected(Failure{});
    return {};
}

[[nodiscard]] inline Expected<void> RaiseFloatingPointExceptions(const i32 exceptions)noexcept{
    if(std::feraiseexcept(exceptions) != 0)
        return MakeUnexpected(Failure{});
    return {};
}

[[nodiscard]] inline Expected<void> SetFloatingPointRoundingMode(const i32 mode)noexcept{
    if(std::fesetround(mode) != 0)
        return MakeUnexpected(Failure{});
    return {};
}

[[nodiscard]] inline i32 TestFloatingPointExceptions(const i32 exceptions)noexcept{
    return std::fetestexcept(exceptions);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

