// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "refraction_query_diagnostics.h"
#include "smoke_environment.h"

#include <tests/smoke/scene_types.adhd.h>

#include <impl/ecs_render/module.h>
#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RefractionQueryDiagnostics::configure(Core::Alloc::ScratchArena& arena, const b8 densityEnabled, const b8 lifecycleEnabled, const b8 timingEnabled){
    const auto configured = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_DIAGNOSTICS");
    if(!configured || (*configured && **configured != "0" && **configured != "1")){
        NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: diagnostics must be absent, 0, or 1"));
        return false;
    }
    m_enabled = *configured && **configured == "1";
    if(!m_enabled)
        return true;
    const auto freeze = ReadEnvironmentVariable(arena, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME");
    const auto readback = ReadSmokeEnvironmentText(arena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
    const auto hardware = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_HARDWARE");
    if(!freeze || !hardware || *freeze || !readback || !densityEnabled || lifecycleEnabled || timingEnabled || (*hardware && **hardware != "1")){
        NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: query diagnostics require a hardware density framebuffer capture with timing, lifecycle, and frame freeze disabled"));
        return false;
    }
    auto& renderer = *MakeNotNull(m_world.getSystem<NWB::Impl::RendererSystem>());
    renderer.setRefractionDiagnosticsEnabled(true);
    return true;
}


void RefractionQueryDiagnostics::update(){
    if(!m_enabled || m_reported)
        return;
    i32 width = 0;
    i32 height = 0;
    m_context.graphics.getWindowDimensions(width, height);
    const auto target = NWB::QueryProjectFrameClientSize();
    const u64 currentFrame = m_context.graphics.getFrameIndex();
    if(width != target.width || height != target.height || (m_targetStart && currentFrame < *m_targetStart)){
        m_targetStart.reset();
        return;
    }
    if(!m_targetStart){
        m_targetStart = currentFrame;
        return;
    }
    constexpr u64 s_RequiredTargetFrames = s_RefractionDiagnosticsRequiredTargetFrames;
    if(currentFrame - *m_targetStart < s_RequiredTargetFrames)
        return;
    const auto& renderer = *MakeNotNull(m_world.getSystem<NWB::Impl::RendererSystem>());
    const auto result = renderer.tryGetLatestRefractionStatistics();
    if(!result)
        return;
    const auto& statistics = *result;
    if(
        statistics.width != target.width
        || statistics.height != target.height
        || statistics.graphicsFrameIndex < *m_targetStart
        || statistics.graphicsFrameIndex - *m_targetStart < s_RequiredTargetFrames
        || statistics.graphicsFrameIndex > currentFrame
    )
        return;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionQueryDiagnostics: sequence={} generation={} graphics_frame={} observed_frame={} target_start_frame={} completed_target_frames={} frame_index={} width={} height={}")
        NWB_TEXT(" hardware_requested={} hardware_available={} hardware_ready={} csg_enabled={} token_valid={} physical_valid={} token_queue={} token_value={} physical_queue={} device_generation={}")
        , statistics.sequence
        , statistics.generation
        , statistics.graphicsFrameIndex
        , currentFrame
        , *m_targetStart
        , statistics.graphicsFrameIndex - *m_targetStart
        , statistics.frameIndex
        , statistics.width
        , statistics.height
        , statistics.hardwareRequested ? 1u : 0u
        , statistics.hardwareAvailable ? 1u : 0u
        , statistics.hardwareReady ? 1u : 0u
        , statistics.csgHardwareEnabled ? 1u : 0u
        , statistics.acceptedToken.valid() ? 1u : 0u
        , statistics.acceptedToken.hasPhysicalQueueIdentity() ? 1u : 0u
        , static_cast<u32>(statistics.acceptedToken.queue)
        , statistics.acceptedToken.value
        , statistics.acceptedToken.physicalQueueIndex
        , statistics.acceptedToken.deviceGeneration
    );
#define NWB_SMOKE_REFRACTION_LOG_COUNTER(name, offset) \
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionQueryCounter: sequence={} generation={} name={} value={}"), statistics.sequence, statistics.generation, NWB_TEXT(#name), statistics.name);
    NWB_REFRACTION_DIAGNOSTIC_COUNTER_FIELDS(NWB_SMOKE_REFRACTION_LOG_COUNTER)
#undef NWB_SMOKE_REFRACTION_LOG_COUNTER
#define NWB_SMOKE_REFRACTION_COUNT_COUNTER(name, offset) + 1u
    constexpr u32 s_CounterCount = 0u NWB_REFRACTION_DIAGNOSTIC_COUNTER_FIELDS(NWB_SMOKE_REFRACTION_COUNT_COUNTER);
#undef NWB_SMOKE_REFRACTION_COUNT_COUNTER
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionQueryDiagnostics: complete sequence={} generation={} counters={}"), statistics.sequence, statistics.generation, s_CounterCount);
    m_sourceFrame = statistics.graphicsFrameIndex;
    m_reported = true;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

