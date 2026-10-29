// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime.h"
#include "runtime_internal.h"
#include "runtime_standalone_graph.h"
#include "profile_names.h"

#include <core/graphics/backend_selection.h>
#include <core/common/log.h>
#include <core/task/gpu/scheduler.h>
#include <core/telemetry/session.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_graphics_runtime_frame{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ScopedAcquiredPresentationFrameReset final : NoCopy{
public:
    explicit ScopedAcquiredPresentationFrameReset(AcquiredPresentationFrame& frame)noexcept
        : m_frame(frame)
    {}
    ~ScopedAcquiredPresentationFrameReset()noexcept{ m_frame = {}; }


private:
    AcquiredPresentationFrame& m_frame;
};


inline constexpr Name s_GraphicsFrameCpuTimingScope("graphics.frame");
inline constexpr Name s_GraphicsAnimateCpuTimingScope("graphics.animate");
inline constexpr Name s_GraphicsBeginFrameCpuTimingScope("graphics.begin_frame");
inline constexpr Name s_GraphicsFramePreambleCpuTimingScope("graphics.frame_preamble");
inline constexpr Name s_GraphicsRenderCpuTimingScope("graphics.render");
inline constexpr Name s_GraphicsPrepareResourcesCpuTimingScope("graphics.prepare_resources");
inline constexpr Name s_GraphicsRenderPassesCpuTimingScope("graphics.render_passes");
inline constexpr Name s_GraphicsPrepareResourcesFailedCpuTimingScope("graphics.prepare_resources_failed");
inline constexpr Name s_GraphicsPresentCpuTimingScope("graphics.present");
inline constexpr Name s_GraphicsGarbageCollectCpuTimingScope("graphics.garbage_collect");
inline constexpr usize s_MaxBeginFrameResizeAttempts = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GraphicsRuntime::CpuTimingPhaseBatch final : NoCopy{
private:
    struct PhaseTiming{
        Perf::TimingScopeId scope;
        f64 seconds = 0.0;
    };

    static constexpr u32 s_MaxPhaseCount = 9u;
    Array<PhaseTiming, s_MaxPhaseCount> m_phases = {};
    u32 m_phaseCount = 0u;


public:
    void stage(const Perf::TimingScopeId scope, const Timer begin)noexcept{
        accumulate(scope, DurationInSeconds<f64>(TimerNow(), begin));
    }

    // Render passes execute on the main thread.
    // One frame sample sums each callback without counting queue waits twice.
    void accumulate(const Perf::TimingScopeId scope, const f64 seconds)noexcept{
        if(!scope.valid())
            return;

        for(u32 index = 0u; index < m_phaseCount; ++index){
            if(m_phases[index].scope.index == scope.index && m_phases[index].scope.generation == scope.generation){
                m_phases[index].seconds = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(m_phases[index].seconds), VectorDoubleReplicate(seconds)));
                return;
            }
        }
        if(m_phaseCount >= s_MaxPhaseCount)
            return;

        PhaseTiming& phase = m_phases[m_phaseCount];
        phase.scope = scope;
        phase.seconds = seconds;
        ++m_phaseCount;
    }

    void flush(Perf::TimingSink& timing, const u64 sampleFrameIndex)const{
        if(!timing.enabled())
            return;

        for(u32 phaseIndex = 0u; phaseIndex < m_phaseCount; ++phaseIndex){
            const PhaseTiming& phase = m_phases[phaseIndex];
            if(phase.scope.valid())
                timing.recordSample(phase.scope, phase.seconds, sampleFrameIndex);
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GraphicsRuntime::runFrame(){
    // This deliberately spans the complete logical graphics frame: normal presentation, headless no-window work, and submission-suspended maintenance.
    // Detailed phase scopes sit within this aggregate, so consumers must not sum them with it.
    // Record only a successful call so a failed frame can never be published with a later one.
    const b8 recordFrameTiming = m_cpuTiming && m_cpuTiming->enabled() && m_frameTimingScope.valid();
    Timer frameTimingBegin;
    const u64 sampleFrameIndex = m_frameIndex;
    if(recordFrameTiming){
        frameTimingBegin = TimerNow();
    }

    if(!m_frameSubmissionSuspended){
        if(!recordFrameTiming)
            return animateRenderPresent();

        CpuTimingPhaseBatch phaseTiming;
        const b8 rendered = animateRenderPresentInternal(&phaseTiming);
        if(rendered){
            m_cpuTiming->recordSample(m_frameTimingScope, DurationInSeconds<f64>(TimerNow(), frameTimingBegin), sampleFrameIndex);
            phaseTiming.flush(*m_cpuTiming, sampleFrameIndex);
        }
        return rendered;
    }

    // Do not let a capture hold hide a device-loss/recreation request.
    // No backend calls here, so the last finished frame stays on screen while the loop stays responsive.
    if(m_deviceRecreationRequested)
        return false;

    auto& device = getDevice();
    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return false;
    }

    YieldThread();
    if(recordFrameTiming)
        m_cpuTiming->recordSample(m_frameTimingScope, DurationInSeconds<f64>(TimerNow(), frameTimingBegin), sampleFrameIndex);
    return true;
}

void GraphicsRuntime::animate(f64 elapsedTime){
    for(const auto renderPass : m_renderPasses)
        renderPass->animate(static_cast<f32>(elapsedTime));
}

b8 GraphicsRuntime::prepareFramePreamble(){
    auto& device = getDevice();
    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return false;
    }

    m_gpuTiming.collect(device, m_frameIndex);
    m_gpuTiming.beginFrame(m_frameIndex);

    // Materialize and reset every declared timer-query pool before any pass preparation can record a timestamp.
    // Per-pass preparation may submit GPU packets, so this must remain ahead of it as well as every later render packet on the same GPU timeline.
    if(m_gpuTiming.collectionActive()){
        if(!m_gpuTiming.materializeRequestedQueries(device))
            NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: failed to materialize one or more requested GPU-timing query pools"));

        // Do not allow render-pass scopes to reuse a prior frame's reset if graph recording or submission fails.
        // The task's accepted callback reenables only the pools covered by the successfully submitted packet.
        m_gpuTiming.discardFrameReset();
        if(!GraphicsModuleDetail::SubmitGraphOwnedFrameTimingReset(*this, *m_standaloneGraphWorkspaces, m_gpuTiming)){
            m_gpuTiming.discardFrameReset();
            NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: failed to submit the graph-owned frame GPU-timing reset packet"));
        }
    }

    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return false;
    }

    return true;
}

Expected<void, CpuTaskAdmissionError::Enum> GraphicsRuntime::render(){
    return renderWithPhaseTiming(nullptr);
}

Expected<void, CpuTaskAdmissionError::Enum> GraphicsRuntime::renderWithPhaseTiming(CpuTimingPhaseBatch* const phaseTiming){
    Framebuffer* const framebuffer = m_acquiredPresentationFrame.framebuffer.get();
    auto& device = getDevice();
    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return {};
    }

    // Each task pairs preparation with rendering.
    // Each pass's prepared work publishes before the next pass prepares; main-thread execution keeps platform contracts.
    CpuTaskScope frameTasks(m_cpuScheduler, m_frameTaskProfileLabel);
    const CpuTaskOptions options{ .priority = CpuTaskPriority::Critical, .target = CpuTaskTarget::MainThread, };
    CpuTaskHandle previous;
    for(const auto renderPass : m_renderPasses){
        const auto admitted = frameTasks.submit([this, &device, framebuffer, renderPass, phaseTiming](){
            if(m_deviceRecreationRequested || device.requiresRecreation()){
                if(device.requiresRecreation())
                    requestDeviceRecreation();
                return;
            }
            Timer prepareBegin;
            if(phaseTiming)
                prepareBegin = TimerNow();
            const b8 resourcesPrepared = renderPass->prepareResources(framebuffer);
            if(phaseTiming)
                phaseTiming->stage(resourcesPrepared ? m_prepareResourcesTimingScope : m_prepareResourcesFailedTimingScope, prepareBegin);
            if(!resourcesPrepared){
                NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: render pass skipped after resource preparation failed"));
                return;
            }

            Timer renderBegin;
            if(phaseTiming)
                renderBegin = TimerNow();
            renderPass->render(framebuffer);
            if(phaseTiming)
                phaseTiming->stage(m_renderPassesTimingScope, renderBegin);

            // Later task callbacks observe this request before touching the invalidated device generation.
            if(device.requiresRecreation())
                requestDeviceRecreation();
        }, options, previous.valid() ? &previous : nullptr, previous.valid() ? 1u : 0u
        );
        if(!admitted){
            frameTasks.wait();
            return MakeUnexpected(admitted.error());
        }
        previous = *admitted;
    }
    frameTasks.wait();
    return {};
}

b8 GraphicsRuntime::shouldRenderUnfocused()const{
    for(auto it = m_renderPasses.crbegin(); it != m_renderPasses.crend(); ++it){
        if((*it)->shouldRenderUnfocused())
            return true;
    }
    return false;
}

b8 GraphicsRuntime::animateRenderPresent(){
    return animateRenderPresentInternal(nullptr);
}

b8 GraphicsRuntime::animateRenderPresentInternal(CpuTimingPhaseBatch* const phaseTiming){
    m_acquiredPresentationFrame = {};
    if(m_deviceRecreationRequested)
        return false;

    auto& device = getDevice();
    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return false;
    }

    Timer now = TimerNow();
    const f64 elapsedTime = DurationInSeconds<f64>(now, m_previousFrameTimestamp);
    const b8 shouldBootstrapWindowPresentation = !m_hasPresentedFrame;

    if(m_windowVisible && (m_windowIsInFocus || shouldRenderUnfocused() || shouldBootstrapWindowPresentation)){
        if(m_prevDpiScaleFactorX != m_dpiScaleFactorX || m_prevDpiScaleFactorY != m_dpiScaleFactorY){
            displayScaleChanged();
            m_prevDpiScaleFactorX = m_dpiScaleFactorX;
            m_prevDpiScaleFactorY = m_dpiScaleFactorY;
        }

        Timer animateBegin;
        if(phaseTiming)
            animateBegin = TimerNow();
        animate(elapsedTime);
        if(phaseTiming)
            phaseTiming->stage(m_animateTimingScope, animateBegin);

        Timer beginFrameBegin;
        if(phaseTiming)
            beginFrameBegin = TimerNow();
        BeginFrameResult beginFrameResult;
        for(usize attempt = 0u; attempt < __hidden_graphics_runtime_frame::s_MaxBeginFrameResizeAttempts; ++attempt){
            beginFrameResult = m_backend->beginFrame();
            if(beginFrameResult.status != BeginFrameStatus::ResizeRequired)
                break;
            if(beginFrameResult.suggestedWidth == 0u || beginFrameResult.suggestedHeight == 0u || !resizeBackBuffer(beginFrameResult.suggestedWidth, beginFrameResult.suggestedHeight, m_requestedVSync))
                break;
        }
        if(phaseTiming)
            phaseTiming->stage(m_beginFrameTimingScope, beginFrameBegin);
        if(!beginFrameResult.acquired()){
            if(beginFrameResult.status == BeginFrameStatus::ResizeRequired)
                NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: swap-chain resize retries were exhausted; requesting device recreation."));
            else
                NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: failed to acquire a presentation frame; requesting device recreation."));
            requestDeviceRecreation();
            return false;
        }
        AcquiredBackBuffer acquiredBackBuffer = Move(beginFrameResult.backBuffer);
        if(acquiredBackBuffer.valid()){
            const u32 acquiredBackBufferIndex = acquiredBackBuffer.index;
            if(acquiredBackBufferIndex >= m_swapChainFramebuffers.size() || !m_swapChainFramebuffers[acquiredBackBufferIndex]){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: acquired swap-chain image has no matching framebuffer; requesting recreation."));
                if(!m_backend->abandonAcquiredFrame())
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: failed to drain the abandoned acquired-frame wait; device teardown is required."));
                requestDeviceRecreation();
                return false;
            }

            const Framebuffer& acquiredFramebuffer = *m_swapChainFramebuffers[acquiredBackBufferIndex];
            const FramebufferDesc& acquiredFramebufferDesc = acquiredFramebuffer.getDescription();
            if(acquiredFramebufferDesc.colorAttachments.size() != 1u || acquiredFramebufferDesc.colorAttachments[0].texture != acquiredBackBuffer.texture.get()){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: acquired swap-chain image mismatches its framebuffer attachment; requesting recreation."));
                if(!m_backend->abandonAcquiredFrame())
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: failed to drain the abandoned acquired-frame wait; device teardown is required."));
                requestDeviceRecreation();
                return false;
            }

            m_acquiredPresentationFrame = { .backBuffer = Move(acquiredBackBuffer), .framebuffer = m_swapChainFramebuffers[acquiredBackBufferIndex], };
            const __hidden_graphics_runtime_frame::ScopedAcquiredPresentationFrameReset acquiredFrameReset(m_acquiredPresentationFrame);

            Timer framePreambleBegin;
            if(phaseTiming)
                framePreambleBegin = TimerNow();
            const b8 preamblePrepared = prepareFramePreamble();
            if(phaseTiming)
                phaseTiming->stage(m_framePreambleTimingScope, framePreambleBegin);
            if(!preamblePrepared){
                // prepareFramePreamble() returns false only after the device requires recreation.
                // Do not issue recovery GPU work; required device teardown owns the unresolved acquired image and synchronization.
                requestDeviceRecreation();
                return false;
            }

            Timer renderBegin;
            if(phaseTiming)
                renderBegin = TimerNow();
            const auto rendered = renderWithPhaseTiming(phaseTiming);
            if(!rendered){
                NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: render pass CPU task admission failed: {}"), static_cast<u32>(rendered.error()));
                if(!device.requiresRecreation() && !m_backend->abandonAcquiredFrame()){
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: failed to quarantine the acquired frame after CPU task rejection"));
                    requestDeviceRecreation();
                }
                return false;
            }
            if(phaseTiming)
                phaseTiming->stage(m_renderTimingScope, renderBegin);

            if(m_deviceRecreationRequested || device.requiresRecreation()){
                if(device.requiresRecreation())
                    requestDeviceRecreation();
                else if(!m_backend->abandonAcquiredFrame())
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: failed to quarantine the aborted acquired frame; device teardown is required."));
                return false;
            }

            Timer presentBegin;
            if(phaseTiming)
                presentBegin = TimerNow();
            const auto presented = m_backend->present();
            const b8 presentationAccepted = presented ? *presented : presented.error().presentationAccepted;
            m_lastPresentationReceipt.record(m_acquiredPresentationFrame.backBuffer, presentationAccepted);
            if(presentationAccepted)
                ++m_successfulPresentationCount;
            if(phaseTiming)
                phaseTiming->stage(m_presentTimingScope, presentBegin);
            if(!presented){
                // A consumed presentation already cleared acquisition and makes abandonment a no-op.
                // Every healthy unconsumed failure is drained and quarantined before recreation.
                if(!device.requiresRecreation() && !m_backend->abandonAcquiredFrame())
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("GraphicsRuntime: failed to quarantine the unpresented acquired frame; device teardown is required."));
                requestDeviceRecreation();
                return false;
            }

            if(device.requiresRecreation()){
                requestDeviceRecreation();
                return false;
            }

            m_hasPresentedFrame = true;
        }
    }

    YieldThread();

    Timer garbageCollectionBegin;
    if(phaseTiming)
        garbageCollectionBegin = TimerNow();
    device.runGarbageCollection();
    if(phaseTiming)
        phaseTiming->stage(m_garbageCollectTimingScope, garbageCollectionBegin);
    if(device.requiresRecreation()){
        requestDeviceRecreation();
        return false;
    }

    m_previousFrameTimestamp = now;

    ++m_frameIndex;
    return true;
}

b8 GraphicsRuntime::registerCpuTimingScopes(){
    if(!m_cpuTiming)
        return true;

    struct ScopeRegistration{
        const Name& name;
        Perf::TimingScopeId& id;
    };
    const ScopeRegistration registrations[] = {
        { __hidden_graphics_runtime_frame::s_GraphicsFrameCpuTimingScope, m_frameTimingScope },
        { __hidden_graphics_runtime_frame::s_GraphicsAnimateCpuTimingScope, m_animateTimingScope },
        { __hidden_graphics_runtime_frame::s_GraphicsBeginFrameCpuTimingScope, m_beginFrameTimingScope },
        { __hidden_graphics_runtime_frame::s_GraphicsFramePreambleCpuTimingScope, m_framePreambleTimingScope },
        { __hidden_graphics_runtime_frame::s_GraphicsPrepareResourcesCpuTimingScope, m_prepareResourcesTimingScope },
        { __hidden_graphics_runtime_frame::s_GraphicsPrepareResourcesFailedCpuTimingScope, m_prepareResourcesFailedTimingScope },
        { __hidden_graphics_runtime_frame::s_GraphicsRenderPassesCpuTimingScope, m_renderPassesTimingScope },
        { __hidden_graphics_runtime_frame::s_GraphicsRenderCpuTimingScope, m_renderTimingScope },
        { __hidden_graphics_runtime_frame::s_GraphicsPresentCpuTimingScope, m_presentTimingScope },
        { __hidden_graphics_runtime_frame::s_GraphicsGarbageCollectCpuTimingScope, m_garbageCollectTimingScope },
    };
    for(const ScopeRegistration& registration : registrations){
        if(registration.id.valid())
            continue;
        const auto scope = m_cpuTiming->registerScope(registration.name);
        if(!scope){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: CPU timing scope registration failed"));
            return false;
        }
        registration.id = *scope;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

