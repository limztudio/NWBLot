// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>
#include <global/environment.h>
#include <global/name.h>
#include <global/text_utils.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_SmokeEnvironmentArena("tests/smoke/environment");

using SmokeEnvironmentString = AString<Core::Alloc::GlobalArena>;

[[nodiscard]] inline Expected<SmokeEnvironmentString> ReadSmokeEnvironmentText(
    Core::Alloc::GlobalArena& arena,
    const AStringView variableName
){
    auto value = ReadEnvironmentVariable(arena, variableName);
    if(!value || value->empty())
        return MakeUnexpected(Failure{});
    return value;
}

[[nodiscard]] inline Expected<f32> ReadSmokeEnvironmentF32(const AStringView variableName){
    Core::Alloc::GlobalArena arena(s_SmokeEnvironmentArena);
    const auto value = ReadEnvironmentVariable(arena, variableName);
    if(!value || value->empty())
        return MakeUnexpected(Failure{});
    return ParseF32FromChars(AStringView(value->data(), value->size()));
}

// Truthy env reader for on/off flags: absent/empty/"0" -> false, anything else -> true.
[[nodiscard]] inline bool ReadSmokeEnvironmentFlag(const AStringView variableName){
    Core::Alloc::GlobalArena arena(s_SmokeEnvironmentArena);
    const auto value = ReadSmokeEnvironmentText(arena, variableName);
    if(!value)
        return false;
    return (*value)[0] != '\0' && (*value)[0] != '0';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

