// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/scratch.h>
#include <core/alloc/general.h>
#include <core/common/log.h>

#include <global/environment.h>
#include <global/name.h>
#include <global/text_utils.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_SmokeEnvironmentArena("tests/smoke/environment");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeEnvironmentString = AString<Core::Alloc::GlobalArena>;

template<typename ArenaT>
[[nodiscard]] inline Expected<AString<ArenaT>, ErrorCode> ReadSmokeEnvironmentText(ArenaT& arena, const AStringView variableName){
    auto value = ReadEnvironmentVariable(arena, variableName);
    if(!value){
        NWB_LOGGER_ERROR(NWB_TEXT("Smoke: failed to read environment '{}': {}"), StringConvert(variableName), StringConvert(value.error().message()));
        return MakeUnexpected(value.error());
    }
    if(!*value || (*value)->empty())
        return MakeUnexpected(MakeErrorCode(ErrorCondition::no_such_file_or_directory));
    return Move(**value);
}

[[nodiscard]] inline Expected<f32> ReadSmokeEnvironmentF32(const AStringView variableName){
    Core::Alloc::ScratchArena arena(s_SmokeEnvironmentArena);
    const auto value = ReadEnvironmentVariable(arena, variableName);
    if(!value){
        NWB_LOGGER_ERROR(NWB_TEXT("Smoke: failed to read environment '{}': {}"), StringConvert(variableName), StringConvert(value.error().message()));
        return MakeUnexpected(Failure{});
    }
    if(!*value || (*value)->empty())
        return MakeUnexpected(Failure{});
    return ParseF32FromChars(AStringView((*value)->data(), (*value)->size()));
}

// Truthy env reader for on/off flags: absent/empty/"0" -> false, anything else -> true.
[[nodiscard]] inline bool ReadSmokeEnvironmentFlag(const AStringView variableName){
    Core::Alloc::ScratchArena arena(s_SmokeEnvironmentArena);
    const auto value = ReadSmokeEnvironmentText(arena, variableName);
    if(!value)
        return false;
    return (*value)[0] != '\0' && (*value)[0] != '0';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

