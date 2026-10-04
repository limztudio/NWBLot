// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "surfel_gi_quality_settings.h"

#include "smoke_environment.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ApplySurfelGiQualitySmokeSettings(
    Impl::RendererSystem& renderer,
    Core::Alloc::GlobalArena& arena,
    const Impl::SurfelGiQualitySettings& baseSettings){
    Impl::SurfelGiQualitySettings settings = baseSettings;
    SmokeEnvironmentString value(arena);
    if(ReadSmokeEnvironmentText("NWB_SURFEL_GI_RESOLVE_RESOLUTION", value)){
        const AStringView resolution(value.data(), value.size());
        if(resolution == "half")
            settings.resolveResolution = Impl::SurfelGiResolveResolution::Half;
        else if(resolution == "quarter")
            settings.resolveResolution = Impl::SurfelGiResolveResolution::Quarter;
        else
            return false;
    }
    if(!renderer.setSurfelGiQualitySettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("SurfelGiQualitySmoke: requested resolve_factor={}"), static_cast<u32>(settings.resolveResolution));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

