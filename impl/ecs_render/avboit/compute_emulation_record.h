// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/material/renderer_pipeline_types.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared AVBOIT alias-free plus CSG compute-emulation record core. Occupancy, extinction, and
// accumulation run the same guard plus materialize plus split-timing plus draw ladder and differ
// only in which timing slot, timing scope, pipeline pass, and AVBOIT framebuffer they target.
struct AvboitComputeEmulationRecordTrait{
    const Core::GpuTimingScopeDefinition* timingScope = nullptr;
    MaterialPipelinePass::Enum pipelinePass = MaterialPipelinePass::AvboitOccupancy;
    Core::FramebufferHandle AvboitFrameTargets::* framebuffer = nullptr;
};

struct AvboitComputeEmulationRecordInputs{
    Core::GraphicsRuntime* graphics = nullptr;
    RendererMaterialSystem* materialSystem = nullptr;
    DeferredFrameTargets* targets = nullptr;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* timing = nullptr;
    const ECSRenderDetail::MeshFrameBindingSnapshot* frameBindings = nullptr;
    const ECSRenderDetail::CsgGraphResourceSnapshot* csgResources = nullptr;
    const ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan* plan = nullptr;
    const ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan* csgPlan = nullptr;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool materialDrawBuffersUploaded = false;
    bool csgFrameBuffersUploaded = false;
    bool csgIntervalSampleImageStatesGraphOwned = false;
    bool csgClipBufferStatesGraphOwned = false;
    bool materialFrameStatesGraphOwned = false;
    bool materialGeometryStatesGraphOwned = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RecordAvboitComputeEmulation(
    const AvboitComputeEmulationRecordInputs& inputs,
    Core::CommandList& commandList,
    const AvboitComputeEmulationRecordTrait& trait
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


enum class AvboitSharedComputeEmulationPhase : u8{
    Generate,
    Raster,
};

struct AvboitSharedComputeEmulationRecordInputs{
    Core::GraphicsRuntime* graphics = nullptr;
    RendererMaterialSystem* materialSystem = nullptr;
    DeferredFrameTargets* targets = nullptr;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* timing = nullptr;
    const ECSRenderDetail::MeshFrameBindingSnapshot* frameBindings = nullptr;
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan* plan = nullptr;
    usize drawIndex = 0u;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool materialDrawBuffersUploaded = false;
    bool materialFrameStatesGraphOwned = false;
    bool materialGeometryStatesGraphOwned = false;
    bool beginTiming = false;
    bool finishTiming = false;
    AvboitSharedComputeEmulationPhase phase = AvboitSharedComputeEmulationPhase::Generate;
};

struct AvboitSharedComputeEmulationRecordTrait{
    const Core::GpuTimingScopeDefinition* timingScope = nullptr;
    MaterialPipelinePass::Enum pipelinePass = MaterialPipelinePass::AvboitOccupancy;
    Core::FramebufferHandle AvboitFrameTargets::* framebuffer = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RecordAvboitSharedComputeEmulation(
    const AvboitSharedComputeEmulationRecordInputs& inputs,
    Core::CommandList& commandList,
    const AvboitSharedComputeEmulationRecordTrait& trait
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
