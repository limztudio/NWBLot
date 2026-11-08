// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_smoke_configuration.h"
#include "smoke_environment.h"

#include <impl/ecs_render/module.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simd/constant.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_smoke_configuration{
inline constexpr AStringView s_HardwareRoute = "hardware";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ReflectionSmokeConfiguration::ReadFlag(const AStringView name, NWB::Core::Alloc::ScratchArena& scratchArena, b8& value){
    const auto text = ReadSmokeEnvironmentText(scratchArena, name);
    if(!text)
        return true;
    if(*text == "0")
        value = false;
    else if(*text == "1")
        value = true;
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: {} must be 0 or 1"), ConvertString(name));
        return false;
    }
    return true;
}

b8 ReflectionSmokeConfiguration::ReadU32(const AStringView name, const u32 minimum, const u32 maximum, NWB::Core::Alloc::ScratchArena& scratchArena, u32& value){
    const auto text = ReadSmokeEnvironmentText(scratchArena, name);
    if(!text)
        return true;
    const auto parsed = ParseU64(AStringView(text->data(), text->size()));
    if(!parsed || *parsed < minimum || *parsed > maximum){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: {} must be in [{}, {}]"), ConvertString(name), minimum, maximum);
        return false;
    }
    value = static_cast<u32>(*parsed);
    return true;
}


b8 ReflectionSmokeConfiguration::configure(Impl::RendererSystem& renderer, Core::GraphicsRuntime& graphics, const b8 extendedCase, const b8 feedbackCase, const b8 opticalCase, const b8 furnace, const b8 opticalTir, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_reflection_smoke_configuration;
    auto& settings = m_settings;
    settings.traceMode = NWB::Impl::ReflectionTraceMode::Hardware;
    settings.environmentTop = Float3U(0.0f, 0.0f, 0.0f);
    settings.environmentBottom = Float3U(0.0f, 0.0f, 0.0f);
    settings.maxHardwareRaysPerFrame = 2u * 960u * 720u;
    settings.diagnosticsEnabled = true;
    settings.temporalEnabled = extendedCase && !opticalCase && !feedbackCase;
    settings.spatialFilterEnabled = false;
    m_targetSamples = extendedCase && !opticalCase ? 64u : 16u;
    if(
        !ReadFlag("NWB_REFLECTION_SMOKE_DIAGNOSTICS", scratchArena, settings.diagnosticsEnabled)
        || !ReadFlag("NWB_REFLECTION_SMOKE_FEEDBACK", scratchArena, settings.screenFeedbackEnabled)
        || !ReadFlag("NWB_REFLECTION_SMOKE_FEEDBACK_CAPTURE", scratchArena, m_feedbackCapture)
        || !ReadFlag("NWB_REFLECTION_SMOKE_TEMPORAL", scratchArena, settings.temporalEnabled)
        || !ReadFlag("NWB_REFLECTION_SMOKE_SPATIAL", scratchArena, settings.spatialFilterEnabled)
        || !ReadFlag("NWB_REFLECTION_SMOKE_FINAL_STATE", scratchArena, m_freshFinalState)
        || !ReadU32("NWB_REFLECTION_SMOKE_HISTORY_SAMPLES", 1u, 256u, scratchArena, m_targetSamples)
        || !ReadU32("NWB_REFLECTION_SMOKE_POST_RESET_SAMPLES", 1u, 256u, scratchArena, m_postResetSamples)
        || !ReadU32("NWB_REFLECTION_SMOKE_SEED", 0u, Limit<u32>::s_Max, scratchArena, m_requestedSeed)
        || !ReadU32("NWB_REFLECTION_SMOKE_OPTICAL_QUERIES", 1u, 16u, scratchArena, settings.maxOpticalQueries)
        || !ReadU32("NWB_REFLECTION_SMOKE_SCREEN_STEPS", 1u, 256u, scratchArena, settings.screenMaxSteps)
    )
        return false;
    settings.temporalMaxSamples = m_targetSamples;
    settings.samplingSeed = m_freshFinalState ? m_requestedSeed + 1u : m_requestedSeed;
    if(extendedCase && !feedbackCase){
        m_authoredRoughness = 0.4f;
        const auto roughnessText = ReadSmokeEnvironmentText(scratchArena, "NWB_REFLECTION_SMOKE_ROUGHNESS");
        if(roughnessText){
            const auto roughness = ParseF32FromChars(AStringView(roughnessText->data(), roughnessText->size()));
            if(!roughness || !IsFinite(*roughness) || *roughness < 0.f || *roughness > 1.f)
                return false;
            m_authoredRoughness = *roughness;
        }
        NWB::Impl::PresentationSettings presentation;
        presentation.shoulder = 1.f;
        if(!renderer.setPresentationSettings(presentation))
            return false;
    }
    if(furnace || opticalTir){
        settings.environmentTop = Float3U(1.f, 1.f, 1.f);
        settings.environmentBottom = Float3U(1.f, 1.f, 1.f);
        settings.roughnessCutoff = 1.f;
    }
    const auto budgetText = ReadSmokeEnvironmentText(scratchArena, "NWB_REFLECTION_SMOKE_RAY_BUDGET");
    if(budgetText){
        const auto parsed = ParseU64(AStringView(budgetText->data(), budgetText->size()));
        if(!parsed || *parsed > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: ray budget must be a nonnegative u32"));
            return false;
        }
        settings.maxHardwareRaysPerFrame = static_cast<u32>(*parsed);
    }
    const auto routeText = ReadSmokeEnvironmentText(scratchArena, "NWB_REFLECTION_SMOKE_MODE");
    const b8 hasRoute = routeText.has_value();
    const AStringView route = hasRoute ? AStringView(routeText->data(), routeText->size()) : AStringView(s_HardwareRoute);
    if(route == "disabled")
        settings.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
    else if(route == "screen")
        settings.traceMode = NWB::Impl::ReflectionTraceMode::ScreenSpace;
    else if(route == "hybrid")
        settings.traceMode = NWB::Impl::ReflectionTraceMode::Hybrid;
    else if(route != s_HardwareRoute){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: unknown reflection mode '{}'"), ConvertString(route));
        return false;
    }
    const auto debugText = ReadSmokeEnvironmentText(scratchArena, "NWB_REFLECTION_SMOKE_DEBUG");
    if(debugText){
        const AStringView debugView(debugText->data(), debugText->size());
        if(debugView == "source")
            settings.debugView = NWB::Impl::ReflectionDebugView::TraceSource;
        else if(debugView == "confidence")
            settings.debugView = NWB::Impl::ReflectionDebugView::Confidence;
        else if(debugView != "none"){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: unknown debug view '{}'"), ConvertString(debugView));
            return false;
        }
    }
    if(!renderer.setReflectionSettings(settings))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: screen feedback {}"), settings.screenFeedbackEnabled ? 1u : 0u);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: screen steps {}"), settings.screenMaxSteps);
    renderer.setRefractionEnabled(true);
    renderer.setRefractionHardwareTracingEnabled(true);
    const b8 hardwareAvailable = graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct) && graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: hardware {}"), hardwareAvailable ? NWB_TEXT("available") : NWB_TEXT("unavailable"));
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: reflection mode {}"), ConvertString(route));
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: hardware ray budget {}"), settings.maxHardwareRaysPerFrame);
    if(opticalCase)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: optical query limit {}"), settings.maxOpticalQueries);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

