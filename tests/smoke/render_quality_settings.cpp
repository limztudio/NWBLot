// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "render_quality_settings.h"

#include "smoke_environment.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>

#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ApplyCausticQualitySmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::CausticQualitySettings& baseSettings){
    Impl::CausticQualitySettings settings = baseSettings;
    const auto value = ReadSmokeEnvironmentText(arena, "NWB_CAUSTIC_PHOTON_GRID_DIVISOR");
    if(value){
        const auto parsed = ParseU64(AStringView(value->data(), value->size()));
        if(!parsed || *parsed > Limit<u32>::s_Max)
            return false;
        settings.photonGridDivisor = static_cast<u32>(*parsed);
    }
    if(!renderer.setCausticQualitySettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticQualitySmoke: requested photon_grid_divisor={}"), settings.photonGridDivisor);
    return true;
}

b8 ApplySurfelGiQualitySmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::SurfelGiQualitySettings& baseSettings){
    Impl::SurfelGiQualitySettings settings = baseSettings;
    const auto value = ReadSmokeEnvironmentText(arena, "NWB_SURFEL_GI_RESOLVE_RESOLUTION");
    if(value){
        const AStringView resolution(value->data(), value->size());
        if(resolution == "half")
            settings.resolveResolution = Impl::SurfelGiResolveResolution::Half;
        else if(resolution == "quarter")
            settings.resolveResolution = Impl::SurfelGiResolveResolution::Quarter;
        else
            return false;
    }
    if(!renderer.setSurfelGiQualitySettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SurfelGiQualitySmoke: requested resolve_factor={}"), static_cast<u32>(settings.resolveResolution));
    return true;
}

b8 ApplyShadowQualitySmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::ShadowQualitySettings& baseSettings){
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
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowQualitySmoke: requested transparent_sampling={} receiver_factor={}"), static_cast<u32>(settings.transparentSampling), static_cast<u32>(settings.receiverResolution));
    return true;
}

b8 ApplySoftwareShadowSmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena, const Impl::SoftwareShadowSettings& baseSettings){
    Impl::SoftwareShadowSettings settings = baseSettings;
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SOFTWARE_SHADOW_BACKEND")){
        const AStringView mode(value->data(), value->size());
        if(mode == "automatic")
            settings.backend = Impl::SoftwareShadowBackend::Automatic;
        else if(mode == "trace")
            settings.backend = Impl::SoftwareShadowBackend::SoftwareTrace;
        else if(mode == "light_space")
            settings.backend = Impl::SoftwareShadowBackend::LightSpace;
        else
            return false;
    }
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SOFTWARE_SHADOW_COVERAGE")){
        const AStringView coverage(value->data(), value->size());
        if(coverage == "reference")
            settings.coverage = Impl::SoftwareShadowCoverage::Reference;
        else if(coverage == "fitted_volume")
            settings.coverage = Impl::SoftwareShadowCoverage::FittedVolume;
        else
            return false;
    }
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH")){
        const AStringView search(value->data(), value->size());
        if(search == "reference_grid9")
            settings.blockerSearch = Impl::SoftwareShadowBlockerSearch::ReferenceGrid9;
        else if(search == "compact_cross5")
            settings.blockerSearch = Impl::SoftwareShadowBlockerSearch::CompactCross5;
        else if(search == "center1")
            settings.blockerSearch = Impl::SoftwareShadowBlockerSearch::Center1;
        else
            return false;
    }
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE")){
        const AStringView cadence(value->data(), value->size());
        if(cadence == "every_frame")
            settings.captureCadence = Impl::SoftwareShadowCaptureCadence::EveryFrame;
        else if(cadence == "reuse_one_frame")
            settings.captureCadence = Impl::SoftwareShadowCaptureCadence::ReuseOneFrame;
        else if(cadence == "reuse_two_frames")
            settings.captureCadence = Impl::SoftwareShadowCaptureCadence::ReuseTwoFrames;
        else
            return false;
    }
    struct ResolutionOverride{ AStringView name; u32& destination; };
    const Array<ResolutionOverride, 2u> overrides = {
        ResolutionOverride{ "NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION", settings.directionalResolution },
        ResolutionOverride{ "NWB_SOFTWARE_SHADOW_POINT_RESOLUTION", settings.pointResolution },
    };
    for(const auto& setting : overrides){
        const auto value = ReadSmokeEnvironmentText(arena, setting.name);
        if(!value)
            continue;
        const auto parsed = ParseU64(AStringView(value->data(), value->size()));
        if(!parsed || *parsed > Limit<u32>::s_Max)
            return false;
        setting.destination = static_cast<u32>(*parsed);
    }
    if(const auto value = ReadSmokeEnvironmentText(arena, "NWB_SOFTWARE_SHADOW_BUDGET_MIB")){
        const auto parsed = ParseU64(AStringView(value->data(), value->size()));
        if(!parsed || *parsed > Limit<u32>::s_Max / (1024u * 1024u))
            return false;
        settings.memoryBudgetBytes = *parsed * 1024u * 1024u;
    }
    if(!renderer.setSoftwareShadowSettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SoftwareShadowSmoke: requested backend={} directional_resolution={} point_resolution={} budget_bytes={} coverage={} blocker_search={} capture_cadence={}")
        , static_cast<u32>(settings.backend)
        , settings.directionalResolution
        , settings.pointResolution
        , settings.memoryBudgetBytes
        , static_cast<u32>(settings.coverage)
        , static_cast<u32>(settings.blockerSearch)
        , static_cast<u32>(settings.captureCadence)
    );
    return true;
}

b8 ApplyReflectionQualitySmokeSettings(const Impl::ReflectionSettings& baseSettings, Impl::RendererSystem& renderer, Core::Alloc::ScratchArena& arena){
    Impl::ReflectionSettings settings = baseSettings;
    const auto value = ReadSmokeEnvironmentText(arena, "NWB_REFLECTION_SCREEN_STEPS");
    if(value){
        const auto parsed = ParseU64(AStringView(value->data(), value->size()));
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

