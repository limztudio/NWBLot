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
    Impl::RendererSystem& renderer, const Impl::ReflectionSettings& baseSettings, Core::Alloc::GlobalArena& arena
){
    Impl::ReflectionSettings settings = baseSettings;
    const auto value = ReadSmokeEnvironmentText(arena, "NWB_REFLECTION_SCREEN_STEPS");
    if(value){
        const auto parsed = ParseU64FromChars(AStringView(value->data(), value->size()));
        if(!parsed || *parsed > Limit<u32>::s_Max)
            return false;
        settings.screenMaxSteps = static_cast<u32>(*parsed);
    }
    if(!renderer.setReflectionSettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionQualitySmoke: requested screen_max_steps={}"), settings.screenMaxSteps);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

