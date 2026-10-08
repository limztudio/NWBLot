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
    const Impl::ShadowQualitySettings& baseSettings
){
    Impl::ShadowQualitySettings settings = baseSettings;
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SHADOW_TRANSPARENT_SAMPLING")){
        const AStringView sampling(value->data(), value->size());
        if(sampling == "reference_three")
            settings.transparentSampling = Impl::TransparentShadowSampling::ReferenceThree;
        else if(sampling == "temporal_one")
            settings.transparentSampling = Impl::TransparentShadowSampling::TemporalOne;
        else
            return false;
    }
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SHADOW_RECEIVER_RESOLUTION")){
        const AStringView resolution(value->data(), value->size());
        if(resolution == "half")
            settings.receiverResolution = Impl::ShadowReceiverResolution::Half;
        else if(resolution == "quarter")
            settings.receiverResolution = Impl::ShadowReceiverResolution::Quarter;
        else
            return false;
    }
    if(!renderer.setShadowQualitySettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowQualitySmoke: requested transparent_sampling={} receiver_factor={}")
        , static_cast<u32>(settings.transparentSampling)
        , static_cast<u32>(settings.receiverResolution)
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

