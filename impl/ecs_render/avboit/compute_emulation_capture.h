// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/shared/task_graph_stage.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared AVBOIT compute-emulation capture owns regular plus CSG plus shared plan capture.
struct AvboitComputeEmulationCaptureInputs{
    MaterialPassDrawItemPartitionsView drawItems{};
    CsgFrameGpuDataView csgFrameData{};
    bool geometryOwned = false;
    bool sampledTexturesCollected = false;
    bool csgStreamsUploaded = false;
    bool intervalOutputsGraphOwned = false;
};

struct AvboitComputeEmulationCaptureResult{
    bool regularCaptured = false;
    bool csgCaptured = false;
    bool sharedCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan sharedPlan;
    usize sharedInstanceCount = 0u;
    usize sharedMaterialTypedByteCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitComputeEmulationCapture final : NoCopy{
public:
    AvboitComputeEmulationCapture()noexcept = default;


public:
    [[nodiscard]] Expected<AvboitComputeEmulationCaptureResult> capture(
        const AvboitComputeEmulationCaptureInputs& inputs,
        usize instanceCount,
        usize materialTypedByteCount,
        Core::Alloc::ScratchArena& scratchArena,
        ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan& plan,
        ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan& csgPlan
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

