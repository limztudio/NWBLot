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


struct AvboitRecordInputsBase{
    static constexpr usize s_StreamUseCount = 7u;
    static constexpr usize s_CsgIntervalUseCount = 4u;
    static constexpr usize s_EmulationBaseCount = 4u;
    static constexpr usize s_EmulationPlanUseCount = 8u;
    static constexpr usize s_MaterialResourceSetUseCapacity = 3u;
    static constexpr usize s_ComputeEmulationResourceSetUseCapacity = 3u;

    DeferredFrameTargets& targets;
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
    b8 producesReusableGeometry = false;
    b8 intervalOutputsGraphOwned = false;
    b8 csgStreamsUploaded = false;
    b8 regularComputeEmulationPlanCaptured = false;
    b8 csgComputeEmulationPlanCaptured = false;
    b8 sharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan sharedComputeEmulationPlan;
    usize sharedComputeEmulationInstanceCount = 0u;
    usize sharedComputeEmulationMaterialTypedByteCount = 0u;


public:
    explicit AvboitRecordInputsBase(DeferredFrameTargets& targetsIn)noexcept
        : targets(targetsIn)
    {}


public:
    // Reuse requires the regular emulation plan and excludes CSG/shared plans.
    [[nodiscard]] b8 generatedGeometryReusePlansValid()const noexcept{
        const b8 generatedGeometryReused = reusedGeometryProducer.valid();
        if((generatedGeometryReused || producesReusableGeometry) && (!regularComputeEmulationPlanCaptured || csgComputeEmulationPlanCaptured || sharedComputeEmulationPlanCaptured))
            return false;
        return true;
    }

    [[nodiscard]] b8 generatedGeometryReused()const noexcept{
        return reusedGeometryProducer.valid();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

