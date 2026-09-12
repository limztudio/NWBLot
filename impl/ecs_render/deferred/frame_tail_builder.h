// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Tail tasks retain explicit publication destinations; root orchestration owns graph compilation.
struct DeferredFrameTailInputs{
    Core::GpuTimingFrameTransaction& frameTimingTransaction;
    Core::QueueSubmissionToken& historyCopySubmissionToken;
    bool& recoveryArmed;
    bool& recoveryRetiresFrameTiming;
    Core::GpuTaskId terminalPresentationTask;
    Core::GpuGraphResourceId historyCopyShadowVisibility;
    Core::GpuGraphResourceId historyCopyCausticIrradiance;
    Core::GpuGraphResourceId historyCopySurfelIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationShadowVisibility;
    Core::GpuGraphResourceId historyCopyDestinationCausticIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationSurfelIrradiance;
    bool capturesLaggedLightingHistory = false;
};

struct DeferredFrameTailResult{
    Core::GpuTaskId historyCopyTask;
    Core::GpuTaskId recoveryTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeferredFrameTailBuilder final : NoCopy{
public:
    explicit DeferredFrameTailBuilder(Core::GpuTaskGraph& graph);


public:
    [[nodiscard]] bool declare(
        const DeferredFrameTailInputs& inputs,
        DeferredFrameTailResult& outResult
    );


private:
    Core::GpuTaskGraph& m_graph;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

