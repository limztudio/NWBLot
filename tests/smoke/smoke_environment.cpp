// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_environment.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<f32> ReadSmokeEnvironmentF32(const AStringView variableName){
    Core::Alloc::ScratchArena arena(s_SmokeEnvironmentArena);
    const auto value = ReadEnvironmentVariable(arena, variableName);
    if(!value){
        NWB_LOGGER_ERROR(NWB_TEXT("Smoke: failed to read environment '{}': {}"), ConvertString(variableName), ConvertString(value.error().message()));
        return MakeUnexpected(Failure{});
    }
    if(!*value || (*value)->empty())
        return MakeUnexpected(Failure{});
    return ParseF32FromChars(AStringView((*value)->data(), (*value)->size()));
}

b8 ReadSmokeEnvironmentFlag(const AStringView variableName){
    Core::Alloc::ScratchArena arena(s_SmokeEnvironmentArena);
    const auto value = ReadSmokeEnvironmentText(arena, variableName);
    if(!value)
        return false;
    return (*value)[0] != '\0' && (*value)[0] != '0';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

