// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/deferred/graph_types.adhd.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Tail tasks retain explicit publication destinations; root orchestration owns graph compilation.
struct DeferredFrameTailInputs{
    Core::GpuTimingFrameTransaction& frameTimingTransaction;
    Core::QueueSubmissionToken& historyCopySubmissionToken;
    b8& recoveryArmed;
    b8& recoveryRetiresFrameTiming;
    Core::GpuTaskId terminalPresentationTask;
    Core::GpuGraphResourceId historyCopyShadowVisibility;
    Core::GpuGraphResourceId historyCopyCausticIrradiance;
    Core::GpuGraphResourceId historyCopySurfelIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationShadowVisibility;
    Core::GpuGraphResourceId historyCopyDestinationCausticIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationSurfelIrradiance;
    b8 capturesLaggedLightingHistory = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeferredFrameTailBuilder final : NoCopy{
public:
    explicit DeferredFrameTailBuilder(Core::GpuTaskGraph& graph)noexcept
        : m_graph(graph)
    {}


public:
    [[nodiscard]] Expected<DeferredFrameTailResult> declare(const DeferredFrameTailInputs& inputs);


private:
    Core::GpuTaskGraph& m_graph;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

