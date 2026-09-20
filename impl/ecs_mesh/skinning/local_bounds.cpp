// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "local_bounds.h"

#include "resource_names.h"
#include "runtime_instance.h"
#include "timing_names.h"

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/vulkan/backend.h>
#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/task_graph.h>
#include <impl/assets/graphics/mesh/runtime_bounds_constants.h>
#include <impl/assets/graphics/skinned_mesh/constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_local_bounds{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LocalBoundsPushConstants{
    u32 meshletCount = 0u;
    u32 bindlessResourceSlots = 0u;
    u32 reserved[2] = {};
};
static_assert(sizeof(LocalBoundsPushConstants) == NWB_SKINNED_MESH_BOUNDS_PUSH_CONSTANT_BYTE_SIZE);
static_assert(offsetof(LocalBoundsPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_BOUNDS_PUSH_MESHLET_COUNT);
static_assert(offsetof(LocalBoundsPushConstants, bindlessResourceSlots) == sizeof(u32) * NWB_SKINNED_MESH_BOUNDS_PUSH_BINDLESS_RESOURCES_SLOT);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CreateMeshSkinningLocalBoundsBuffers(Core::GraphicsRuntime& graphics, MeshSkinningRuntimeInstance& instance){
    instance.deformationState.invalidateCurrent();
    if(instance.meshlets.empty() || instance.meshlets.size() > Limit<u32>::s_Max / NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime local bounds partial count is empty or exceeds shader addressing"));
        return false;
    }
    const Name partialName = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, "meshlet_local_bounds");
    const Name outputName = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, "local_bounds");
    if(!partialName || !outputName){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive runtime local bounds buffer names"));
        return false;
    }
    Core::BufferDesc desc;
    desc
        .setByteSize(static_cast<u64>(instance.meshlets.size()) * NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE)
        .setCanHaveUAVs(true)
        .setCanHaveRawViews(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(partialName)
    ;
    Core::BufferHandle partials = graphics.createBuffer(desc);
    if(!partials){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create runtime local bounds partials"));
        return false;
    }
    desc.setByteSize(NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE).setDebugName(outputName);
    Core::BufferHandle output = graphics.createBuffer(desc);
    if(!output){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create runtime local bounds output"));
        return false;
    }
    instance.meshletLocalBoundsBuffer = Move(partials);
    instance.localBoundsBuffer = Move(output);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshSkinningLocalBoundsTask::Payload::Payload(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics,
    Core::GpuTimingSubmissionTicket& timingTicket)
    : graphics(graphics)
    , timingTicket(timingTicket)
    , dispatches(arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshSkinningLocalBoundsTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context){
    if(payload.dispatches.empty())
        return false;
    auto& device = payload.graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::GpuTimingSubmissionTicket::RecordingScope recording(payload.timingTicket);
    for(const MeshSkinningLocalBoundsDispatch& plan : payload.dispatches){
        Core::ComputePipeline* const pipeline = context.declarations.computePipelineFor(plan.localBoundsPipeline);
        if(
            !pipeline
            || !context.declarations.bufferForResource(plan.bindlessResourceSlotsResource)
            || !context.declarations.bufferForResource(plan.meshletLocalBoundsResource)
            || !context.declarations.bufferForResource(plan.localBoundsResource)
        )
            return false;
        Core::ComputeState state;
        state.setPipeline(pipeline);
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *pipeline);
        const __hidden_skinning_local_bounds::LocalBoundsPushConstants push{plan.meshletCount, plan.bindlessResourceSlots};
        commandList.setPushConstants(&push, sizeof(push));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_LocalBounds, device, commandList);

        commandList.dispatch(1u, 1u, 1u);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

