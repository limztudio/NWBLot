// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "transparent_multi_renderer_setup.h"
#include "smoke_scene_helpers.h"

#include <loader/project_entry.h>

#include <impl/ecs_render/module.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>

#include <global/assert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TransparentMultiRendererSetup::Configure(Core::ECS::World& world, ProjectRuntimeContext& context){
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    static constexpr AStringView s_CausticTimingEnvironment = "NWB_CAUSTIC_SMOKE_TIMING";
#endif
    const b8 rayQueryHardwareAvailable = context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct) && context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery);
    if(rayQueryHardwareAvailable)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: natural hardware shadow route selected on RayQuery-capable hardware"));
    else
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: natural software-only shadow route selected because RayQuery-capable hardware is unavailable"));

#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
    auto& rendererSystem = AddSmokeRenderSystems(world, context);
    rendererSystem.setFrameLaggedAsyncLightingEnabled(true);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FrameLaggedAsyncLightingSmoke: requested frame-lagged async lighting; F1 toggles the current-frame path"));
#elif defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
    auto& rendererSystem = AddSmokeRenderSystems(world, context);
    const auto refractionSetting = ReadSmokeEnvironmentF32("NWB_REFRACTION_SMOKE_ENABLED");
    const b8 refractionEnabled = !refractionSetting || *refractionSetting != 0.0f;
    const auto hardwareSetting = ReadSmokeEnvironmentF32("NWB_REFRACTION_SMOKE_HARDWARE");
    const b8 hardwareEnabled = !hardwareSetting || *hardwareSetting != 0.0f;
    rendererSystem.setRefractionEnabled(refractionEnabled);
    rendererSystem.setRefractionHardwareTracingEnabled(hardwareEnabled);
    NWB::Impl::ReflectionSettings reflectionSettings;
    const auto reflectionModeText = NWB::Tests::Smoke::ReadSmokeEnvironmentText(scratchArena, "NWB_REFLECTION_SMOKE_MODE");
    if(reflectionModeText){
        const AStringView mode(reflectionModeText->data(), reflectionModeText->size());
        if(mode == "disabled")
            reflectionSettings.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
        else if(mode == "hardware")
            reflectionSettings.traceMode = NWB::Impl::ReflectionTraceMode::Hardware;
        else if(mode == "screen")
            reflectionSettings.traceMode = NWB::Impl::ReflectionTraceMode::ScreenSpace;
        else
            NWB_FATAL_ASSERT_MSG(mode == "hybrid", NWB_TEXT("CausticSphereSmokeProject: invalid reflection mode"));
    }
    if(NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_CAUSTIC_SMOKE_REFLECTION_COMPARISON")){
        reflectionSettings.environmentTop = Float3U(0.6f, 0.7f, 1.0f);
        reflectionSettings.environmentBottom = reflectionSettings.environmentTop;
        reflectionSettings.maxHardwareRaysPerFrame = 2u * 1280u * 900u;
        reflectionSettings.temporalEnabled = false;
        reflectionSettings.spatialFilterEnabled = false;
    }
    if(NWB::Tests::Smoke::ReadSmokeEnvironmentFlag(s_CausticTimingEnvironment)){
        reflectionSettings.diagnosticsEnabled = false;
        reflectionSettings.temporalEnabled = false;
        reflectionSettings.spatialFilterEnabled = false;
        reflectionSettings.screenFeedbackEnabled = false;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticTimingProbe: reflection diagnostics {} temporal {} spatial {} feedback {}")
            , reflectionSettings.diagnosticsEnabled
            , reflectionSettings.temporalEnabled
            , reflectionSettings.spatialFilterEnabled
            , reflectionSettings.screenFeedbackEnabled
        );
    }
    NWB_FATAL_ASSERT_MSG(rendererSystem.setReflectionSettings(reflectionSettings), NWB_TEXT("CausticSphereSmokeProject: invalid reflection settings"));
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticSphereSmokeProject: reflection mode {}"), static_cast<u32>(reflectionSettings.traceMode));
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticSphereSmokeProject: camera refraction {}"), refractionEnabled ? NWB_TEXT("enabled") : NWB_TEXT("disabled"));
#elif defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    // This clipping/coverage oracle measures the authored AVBOIT surface appearance.
    auto& rendererSystem = AddSmokeRenderSystems(world, context);
    rendererSystem.setRefractionEnabled(false);
    NWB::Impl::ReflectionSettings reflectionSettings;
    reflectionSettings.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
    NWB_FATAL_ASSERT_MSG(rendererSystem.setReflectionSettings(reflectionSettings), NWB_TEXT("TransparentCsgSmokeProject: invalid reflection settings"));
#else
    AddSmokeRenderSystems(world, context);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

