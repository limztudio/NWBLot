// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/task/gpu/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrameExecuteRecovery;
class RendererFramePipeline;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Records the optional tasks that follow accepted presentation: the surfel counter readback and the lagged lighting-history copy.
class FrameExecutePresentTails final : NoCopy{
public:
    FrameExecutePresentTails(RendererFramePipeline& renderer, FrameExecuteRecovery& recovery, const Core::GpuCompiledGraph::ReadView& compiledPlan, const Core::QueueSubmissionToken& finalPresentationSubmissionToken)noexcept
        : m_renderer(renderer)
        , m_recovery(recovery)
        , m_compiledPlan(compiledPlan)
        , m_finalPresentationSubmissionToken(finalPresentationSubmissionToken)
    {}


public:
    // Returns false when an accepted readback lost its retained state and the frame must stop.
    [[nodiscard]] bool executeSurfelCounterReadback(const Core::GpuPhysicalQueueInfo* readbackQueue, const Core::BufferHandle* counterBuffers, usize counterBufferCount);
    void executeLaggedLightingHistoryCapture(DeferredFrameTargets& targets, const Core::GpuPhysicalQueueInfo* historyQueue, bool requestsCapture, bool captureDeclared, bool laggedAsyncLightingSchedule);


private:
    RendererFramePipeline& m_renderer;
    FrameExecuteRecovery& m_recovery;
    const Core::GpuCompiledGraph::ReadView& m_compiledPlan;
    const Core::QueueSubmissionToken& m_finalPresentationSubmissionToken;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

