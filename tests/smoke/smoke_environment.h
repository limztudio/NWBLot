
#pragma once

#ifndef NWB_TESTS_SMOKE_ENVIRONMENT_H
#define NWB_TESTS_SMOKE_ENVIRONMENT_H

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

[[nodiscard]] inline bool ReadSmokeEnvironmentText(const char* const variableName, SmokeEnvironmentString& outValue){
    return ReadEnvironmentVariable(variableName, outValue) && !outValue.empty();
}

[[nodiscard]] inline bool ReadSmokeEnvironmentF32(const char* const variableName, f32& outValue){
    Core::Alloc::GlobalArena arena(s_SmokeEnvironmentArena);
    SmokeEnvironmentString value(arena);
    return ReadSmokeEnvironmentText(variableName, value) && ParseF32FromChars(value.data(), value.data() + value.size(), outValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

