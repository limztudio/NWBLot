// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared AVBOIT record inputs core. Occupancy, extinction, and accumulation share these fields and differ only in their effect-specific targets, tasks, timing slots, and upload flags.
struct AvboitRecordInputsBase{
    DeferredFrameTargets* targets = nullptr;
    Core::GpuGraphResourceId albedo;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId refractionInstance;
    Core::GpuGraphResourceId avboitMaterialDomain;
    Core::GpuGraphResourceId avboitCsgDomain;
    Core::GpuGraphResourceId meshView;
    Core::GpuGraphResourceId materialInstances;
    Core::GpuGraphResourceId materialTyped;
    Core::GpuGraphResourceId csgReceiverRanges;
    Core::GpuGraphResourceId csgCutters;
    Core::GpuGraphResourceId csgClipContextSlots;
    Core::GpuGraphResourceId csgIntervalSampleState;
    Core::GpuGraphResourceId csgRemovedIntervalDepth;
    Core::GpuGraphResourceId csgRemovedIntervalCapNormal;
    Core::GpuGraphResourceId csgRemovedIntervalData;
    Core::GpuGraphResourceId csgRemovedIntervalCount;
    Core::TextureSubresourceSet csgRemovedIntervalSubresources;
    Core::TextureSubresourceSet csgRemovedIntervalCountSubresources;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuTaskId uploadTask;
    Core::GpuTaskId reusedGeometryProducer;
    Core::GpuGraphResourceSetId materialGeometrySet;
    Core::GpuGraphResourceSetId materialSampledTextureSet;
    Core::BufferRange instanceRange;
    Core::BufferRange materialTypedRange;
    Core::BufferRange receiverRange;
    Core::BufferRange cutterRange;
    bool producesReusableGeometry = false;
    bool intervalOutputsGraphOwned = false;
    bool csgStreamsUploaded = false;
    bool regularComputeEmulationPlanCaptured = false;
    bool csgComputeEmulationPlanCaptured = false;
    bool sharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan sharedComputeEmulationPlan;
    usize sharedComputeEmulationInstanceCount = 0u;
    usize sharedComputeEmulationMaterialTypedByteCount = 0u;

    explicit AvboitRecordInputsBase(Core::Alloc::GlobalArena& arena){
        static_cast<void>(arena);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

