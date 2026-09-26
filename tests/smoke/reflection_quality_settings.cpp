// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_quality_settings.h"

#include "smoke_environment.h"

#include <core/common/log.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ApplyReflectionQualitySmokeSettings(
    Impl::RendererSystem& renderer, const Impl::ReflectionSettings& baseSettings, Core::Alloc::GlobalArena& arena){
    Impl::ReflectionSettings settings = baseSettings;
    SmokeEnvironmentString value(arena);
    if(ReadSmokeEnvironmentText("NWB_REFLECTION_SCREEN_STEPS", value)){
        u64 parsed = 0u;
        if(!ParseU64FromChars(value.data(), value.data() + value.size(), parsed) || parsed > Limit<u32>::s_Max)
            return false;
        settings.screenMaxSteps = static_cast<u32>(parsed);
    }
    if(!renderer.setReflectionSettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionQualitySmoke: requested screen_max_steps={}"), settings.screenMaxSteps);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

