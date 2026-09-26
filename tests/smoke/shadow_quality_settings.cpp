// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_quality_settings.h"

#include "smoke_environment.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ApplyShadowQualitySmokeSettings(
    Impl::RendererSystem& renderer,
    Core::Alloc::GlobalArena& arena,
    const Impl::ShadowQualitySettings& baseSettings){
    Impl::ShadowQualitySettings settings = baseSettings;
    SmokeEnvironmentString value(arena);
    if(ReadSmokeEnvironmentText("NWB_SHADOW_TRANSPARENT_SAMPLING", value)){
        const AStringView sampling(value.data(), value.size());
        if(sampling == "reference_three")
            settings.transparentSampling = Impl::TransparentShadowSampling::ReferenceThree;
        else if(sampling == "temporal_one")
            settings.transparentSampling = Impl::TransparentShadowSampling::TemporalOne;
        else
            return false;
    }
    if(!renderer.setShadowQualitySettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowQualitySmoke: requested transparent_sampling={}")
        , static_cast<u32>(settings.transparentSampling)
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

