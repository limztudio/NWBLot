// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "runtime_cache.h"
#include "skin_payload.h"
#include "timing_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/task_graph.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshSkinningSystem::prepareRuntimeMeshResources(
    MeshSkinningRuntimeInstance& instance,
    const SkeletonJointPaletteComponent* jointPalette,
    const SkeletonPoseComponent* skeletonPose,
    Core::Alloc::ScratchArena& scratchArena){
    RuntimeSkinPayloadScratch payload{ scratchArena };
    if(!MeshSkinningPayload::BuildRuntimeSkinPayload(instance, jointPalette, skeletonPose, payload))
        return false;

    const bool hasActiveSkin = payload.hasActiveSkin();
    const bool skinnedMeshInputDirty = (instance.dirtyFlags & RuntimeMeshDirtyFlag::SkinningInputDirty) != 0u;
    const bool meshletBoundsDirty = (instance.dirtyFlags & RuntimeMeshDirtyFlag::MeshletBoundsDirty) != 0u;
    const auto foundRuntimeResources = m_runtimeResources.find(instance.handle.value);
    const bool hadSkinningResources = foundRuntimeResources != m_runtimeResources.end() && foundRuntimeResources.value().usesSkinning();
    if(!hasActiveSkin && !skinnedMeshInputDirty && !meshletBoundsDirty && !hadSkinningResources)
        return true;
    if(instance.meshlets.empty())
        return true;

    if(hasActiveSkin && !ensureSkinningPipeline())
        return false;
    if(!ensureBoundsPipeline())
        return false;
    // RT attribute buffer exists only with ray tracing; build repack pipeline only then.
    if(instance.attributeBuffer && !ensureRepackPipeline())
        return false;

    RuntimeResources* resources = nullptr;
    bool resourcesRebuilt = false;
    if(!ensureRuntimeResources(
        instance,
        payload,
        scratchArena,
        resources,
        resourcesRebuilt
    ))
        return false;

    // Releasing a pose rebuilds descriptors; force one rest->skinned copy.
    if(!hasActiveSkin && hadSkinningResources && resourcesRebuilt){
        instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
            instance.dirtyFlags | RuntimeMeshDirtyFlag::SkinningInputDirty
        );
    }
    return true;
}

bool MeshSkinningSystem::recordGraphOwnedSkinningDeformation(
    const MeshSkinningGraphDispatchPlan& plan,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    if(!plan.hasActiveSkin)
        return false;

    const auto resolveBuffer = [&](const Core::GpuGraphResourceId resource){
        return context.declarations.bufferForResource(resource);
    };

    Core::Buffer* const bindlessResourceSlots = resolveBuffer(plan.bindlessResourceSlotsResource);
    Core::Buffer* const skinnedPosition = resolveBuffer(plan.skinnedPositionResource);
    Core::Buffer* const restPosition = resolveBuffer(plan.restPositionResource);
    Core::Buffer* const restNormal = resolveBuffer(plan.restNormalResource);
    Core::Buffer* const restTangent = resolveBuffer(plan.restTangentResource);
    Core::Buffer* const skinnedNormal = resolveBuffer(plan.skinnedNormalResource);
    Core::Buffer* const skinnedTangent = resolveBuffer(plan.skinnedTangentResource);
    Core::Buffer* const meshletDesc = resolveBuffer(plan.meshletDescResource);
    Core::Buffer* const meshletPositionRefDeltas = resolveBuffer(plan.meshletPositionRefDeltaResource);
    Core::Buffer* const meshletAttributeRefDeltas = resolveBuffer(plan.meshletAttributeRefDeltaResource);
    Core::Buffer* const attributeSkins = resolveBuffer(plan.attributeSkinResource);
    Core::Buffer* const skinInfluences = resolveBuffer(plan.skinResource);
    Core::Buffer* const jointPalette = resolveBuffer(plan.jointPaletteResource);
    Core::ComputePipeline* const skinningPipeline = context.declarations.computePipelineFor(plan.skinningPipeline);
    if(
        !bindlessResourceSlots
        || !skinnedPosition
        || !restPosition
        || !restNormal
        || !restTangent
        || !skinnedNormal
        || !skinnedTangent
        || !meshletDesc
        || !meshletPositionRefDeltas
        || !meshletAttributeRefDeltas
        || !attributeSkins
        || !skinInfluences
        || !jointPalette
        || !skinningPipeline
    )
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    // Graph establishes inputs and declares UAV outputs; callback records dispatch only.
    Core::ComputeState computeState;
    computeState.setPipeline(skinningPipeline);
    commandList.setComputeState(computeState);
    heap.bindCompute(commandList, *skinningPipeline);

    MeshSkinningPushConstants pushConstants;
    pushConstants.meshletCount = plan.meshletCount;
    pushConstants.skinCount = plan.skinCount;
    pushConstants.jointCount = plan.jointCount;
    pushConstants.skinningMode = plan.skinningMode;
    pushConstants.attributeCount = plan.attributeCount;
    pushConstants.bindlessResourceSlots = plan.bindlessResourceSlots;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_Skinning, device, commandList);

        commandList.dispatch(pushConstants.meshletCount, 1u, 1u);
    }

    return true;
}


bool MeshSkinningSystem::recordGraphOwnedSkinningPostDispatch(
    const MeshSkinningGraphDispatchPlan& plan,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    if(!plan.updatesMeshletBounds)
        return false;

    const auto resolveBuffer = [&](const Core::GpuGraphResourceId resource){
        return context.declarations.bufferForResource(resource);
    };

    Core::Buffer* const bindlessResourceSlots = resolveBuffer(plan.bindlessResourceSlotsResource);
    Core::Buffer* const skinnedPosition = resolveBuffer(plan.skinnedPositionResource);
    Core::Buffer* const meshletDesc = resolveBuffer(plan.meshletDescResource);
    Core::Buffer* const meshletPositionRefDeltas = resolveBuffer(plan.meshletPositionRefDeltaResource);
    Core::Buffer* const meshletLocalVertexRefs = resolveBuffer(plan.meshletLocalVertexRefResource);
    Core::Buffer* const meshletPrimitiveIndices = resolveBuffer(plan.meshletPrimitiveIndexResource);
    Core::Buffer* const meshletBounds = resolveBuffer(plan.meshletBoundsResource);
    Core::ComputePipeline* const boundsPipeline = context.declarations.computePipelineFor(plan.boundsPipeline);
    if(
        !bindlessResourceSlots
        || !skinnedPosition
        || !meshletDesc
        || !meshletPositionRefDeltas
        || !meshletLocalVertexRefs
        || !meshletPrimitiveIndices
        || !meshletBounds
        || !boundsPipeline
    )
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    // Bounds consumes declared skinned positions and writes declared bounds output.
    Core::ComputeState computeState;
    computeState.setPipeline(boundsPipeline);
    commandList.setComputeState(computeState);
    heap.bindCompute(commandList, *boundsPipeline);

    MeshletBoundsPushConstants boundsPushConstants;
    boundsPushConstants.meshletCount = plan.meshletCount;
    boundsPushConstants.bindlessResourceSlots = plan.bindlessResourceSlots;
    commandList.setPushConstants(&boundsPushConstants, sizeof(boundsPushConstants));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_MeshletBounds, device, commandList);

        commandList.dispatch(boundsPushConstants.meshletCount, 1u, 1u);
    }

    if(plan.repacksNormals){
        Core::Buffer* const skinnedNormal = resolveBuffer(plan.skinnedNormalResource);
        Core::Buffer* const meshletAttributeRefDeltas = resolveBuffer(plan.meshletAttributeRefDeltaResource);
        Core::Buffer* const attributeBuffer = resolveBuffer(plan.attributeResource);
        Core::ComputePipeline* const repackPipeline = context.declarations.computePipelineFor(plan.repackPipeline);
        if(!skinnedNormal || !meshletAttributeRefDeltas || !attributeBuffer || !repackPipeline)
            return false;

        // Graph lowers the handoff before repack; publishes packed attributes as output.
        computeState.setPipeline(repackPipeline);
        commandList.setComputeState(computeState);
        heap.bindCompute(commandList, *repackPipeline);

        MeshletRepackPushConstants repackPushConstants;
        repackPushConstants.meshletCount = plan.meshletCount;
        repackPushConstants.bindlessResourceSlots = plan.bindlessResourceSlots;
        commandList.setPushConstants(&repackPushConstants, sizeof(repackPushConstants));
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_RepackNormals, device, commandList);

            commandList.dispatch(repackPushConstants.meshletCount, 1u, 1u);
        }
    }
    return true;
}

void MeshSkinningSystem::confirmGraphOwnedSkinningDispatch(const MeshSkinningGraphDispatchPlan& plan)noexcept{
    MeshSkinningRuntimeInstance* const instance = m_runtimeMeshCache.findInstance(plan.handle);
    if(!instance || instance->editRevision != plan.submissionCommit.editRevision)
        return;

    const auto foundResources = m_runtimeResources.find(plan.handle.value);
    if(foundResources == m_runtimeResources.end())
        return;
    RuntimeResources& resources = foundResources.value();
    if(
        resources.editRevision != plan.submissionCommit.editRevision
        || resources.bindlessResourceSlotsBuffer.get() != plan.bindlessResourceSlotsBuffer.get()
        || resources.bindlessHeapHandles.resourceSlots != plan.bindlessResourceSlotsDescriptor
        || resources.bindlessResourceSlots != plan.bindlessResourceSlotsPayload
    )
        return;

    ApplyMeshSkinningSubmissionCommit(
        true,
        instance->editRevision,
        instance->dirtyFlags,
        resources.bindlessResourceSlotsUploaded,
        plan.submissionCommit
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

