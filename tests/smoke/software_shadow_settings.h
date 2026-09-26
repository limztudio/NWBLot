// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_environment.h"

#include <impl/ecs_render/module.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ApplySoftwareShadowSmokeSettings(Impl::RendererSystem& renderer, Core::Alloc::GlobalArena& arena){
    Impl::SoftwareShadowSettings settings;
    SmokeEnvironmentString value(arena);
    if(ReadSmokeEnvironmentText("NWB_SOFTWARE_SHADOW_BACKEND", value)){
        const AStringView mode(value.data(), value.size());
        if(mode == "automatic")
            settings.backend = Impl::SoftwareShadowBackend::Automatic;
        else if(mode == "trace")
            settings.backend = Impl::SoftwareShadowBackend::SoftwareTrace;
        else if(mode == "light_space")
            settings.backend = Impl::SoftwareShadowBackend::LightSpace;
        else
            return false;
    }
    if(ReadSmokeEnvironmentText("NWB_SOFTWARE_SHADOW_COVERAGE", value)){
        const AStringView coverage(value.data(), value.size());
        if(coverage == "reference")
            settings.coverage = Impl::SoftwareShadowCoverage::Reference;
        else if(coverage == "fitted_volume")
            settings.coverage = Impl::SoftwareShadowCoverage::FittedVolume;
        else
            return false;
    }
    if(ReadSmokeEnvironmentText("NWB_SOFTWARE_SHADOW_BLOCKER_SEARCH", value)){
        const AStringView search(value.data(), value.size());
        if(search == "reference_grid9")
            settings.blockerSearch = Impl::SoftwareShadowBlockerSearch::ReferenceGrid9;
        else if(search == "compact_cross5")
            settings.blockerSearch = Impl::SoftwareShadowBlockerSearch::CompactCross5;
        else
            return false;
    }
    if(ReadSmokeEnvironmentText("NWB_SOFTWARE_SHADOW_CAPTURE_CADENCE", value)){
        const AStringView cadence(value.data(), value.size());
        if(cadence == "every_frame")
            settings.captureCadence = Impl::SoftwareShadowCaptureCadence::EveryFrame;
        else if(cadence == "reuse_one_frame")
            settings.captureCadence = Impl::SoftwareShadowCaptureCadence::ReuseOneFrame;
        else
            return false;
    }
    struct ResolutionOverride{ const char* name; u32* destination; };
    const ResolutionOverride overrides[] = {
        { "NWB_SOFTWARE_SHADOW_DIRECTIONAL_RESOLUTION", &settings.directionalResolution },
        { "NWB_SOFTWARE_SHADOW_POINT_RESOLUTION", &settings.pointResolution },
    };
    for(const auto& setting : overrides){
        if(!ReadSmokeEnvironmentText(setting.name, value))
            continue;
        u64 parsed = 0u;
        if(!ParseU64FromChars(value.data(), value.data() + value.size(), parsed) || parsed > Limit<u32>::s_Max)
            return false;
        *setting.destination = static_cast<u32>(parsed);
    }
    if(ReadSmokeEnvironmentText("NWB_SOFTWARE_SHADOW_BUDGET_MIB", value)){
        u64 parsed = 0u;
        if(!ParseU64FromChars(value.data(), value.data() + value.size(), parsed) || parsed > Limit<u32>::s_Max / (1024u * 1024u))
            return false;
        settings.memoryBudgetBytes = parsed * 1024u * 1024u;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

