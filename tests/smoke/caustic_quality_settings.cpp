// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_quality_settings.h"

#include "smoke_environment.h"

#include <core/common/log.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ApplyCausticQualitySmokeSettings(
    Impl::RendererSystem& renderer,
    Core::Alloc::GlobalArena& arena,
    const Impl::CausticQualitySettings& baseSettings){
    Impl::CausticQualitySettings settings = baseSettings;
    SmokeEnvironmentString value(arena);
    if(ReadSmokeEnvironmentText("NWB_CAUSTIC_PHOTON_GRID_DIVISOR", value)){
        u64 parsed = 0u;
        if(!ParseU64FromChars(value.data(), value.data() + value.size(), parsed) || parsed > Limit<u32>::s_Max)
            return false;
        settings.photonGridDivisor = static_cast<u32>(parsed);
    }
    if(!renderer.setCausticQualitySettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticQualitySmoke: requested photon_grid_divisor={}"), settings.photonGridDivisor);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

