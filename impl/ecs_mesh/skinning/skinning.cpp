// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "arena_names.h"
#include "runtime_cache.h"
#include "skin_payload.h"
#include "timing_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>
#include <core/graphics/module.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/task_graph/compiled_graph.h>
#include <core/graphics/task_graph/task_graph.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void SetRestBufferStates(
    Core::CommandList& commandList,
    MeshSkinningRuntimeInstance& instance,
    const Core::ResourceStates::Mask state
){
    commandList.setBufferState(instance.restPositionBuffer.get(), state);
    commandList.setBufferState(instance.restNormalBuffer.get(), state);
    commandList.setBufferState(instance.restTangentBuffer.get(), state);
}

static void SetSkinnedBufferStates(
    Core::CommandList& commandList,
    MeshSkinningRuntimeInstance& instance,
    const Core::ResourceStates::Mask state
){
    commandList.setBufferState(instance.skinnedPositionBuffer.get(), state);
    commandList.setBufferState(instance.skinnedNormalBuffer.get(), state);
    commandList.setBufferState(instance.skinnedTangentBuffer.get(), state);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshSkinningSystem::prepareRuntimeMeshResources(
    MeshSkinningRuntimeInstance& instance,
    const SkeletonJointPaletteComponent* jointPalette,
    const SkeletonPoseComponent* skeletonPose
){
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_PrepareRuntimeArena);
    RuntimeSkinPayloadScratch payload{ scratchArena };
    if(!MeshSkinningPayload::BuildRuntimeSkinPayload(instance, jointPalette, skeletonPose, payload))
        return false;

    const bool hasActiveSkin = payload.hasActiveSkin();
    RuntimePayloadViews payloadViews;
    if(hasActiveSkin){
        payloadViews.skinInfluences = payload.skinInfluences.data();
        payloadViews.jointPalette = payload.jointMatrices.data();
        payloadViews.skinInfluenceCount = payload.skinInfluences.size();
        payloadViews.jointPaletteCount = payload.jointMatrices.size();
    }

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
    // The RT attribute buffer exists only when ray tracing is supported; build the repack pipeline only then so the
    // runtime resource registration has its layout, and so no-RT runs do not pay for an unused pipeline.
    if(instance.attributeBuffer && !ensureRepackPipeline())
        return false;

    RuntimeResources* resources = nullptr;
    bool resourcesRebuilt = false;
    if(!ensureRuntimeResources(
        instance,
        payloadViews,
        resources,
        resourcesRebuilt
    ))
        return false;

    // Releasing a pose rebuilds the per-dispatch descriptors before the subsequent render pass can observe the
    // previous skinning generation. Force one rest->skinned copy so the output streams no longer retain that pose.
    if(!hasActiveSkin && hadSkinningResources && resourcesRebuilt){
        instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
            instance.dirtyFlags | RuntimeMeshDirtyFlag::SkinningInputDirty
        );
    }
    return true;
}

bool MeshSkinningSystem::dispatchRuntimeMesh(
    Core::CommandList& commandList,
    MeshSkinningRuntimeInstance& instance,
    const SkeletonJointPaletteComponent* jointPalette,
    const SkeletonPoseComponent* skeletonPose,
    MeshSkinningSubmissionCommit& outCommit
){
    outCommit = MeshSkinningSubmissionCommit{};
    outCommit.editRevision = instance.editRevision;

    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_DispatchRuntimeArena);
    RuntimeSkinPayloadScratch payload{ scratchArena };
    if(!MeshSkinningPayload::BuildRuntimeSkinPayload(instance, jointPalette, skeletonPose, payload))
        return false;

    const bool hasActiveSkin = payload.hasActiveSkin();

    const bool skinnedMeshInputDirty = (instance.dirtyFlags & RuntimeMeshDirtyFlag::SkinningInputDirty) != 0u;
    const bool meshletBoundsDirty = (instance.dirtyFlags & RuntimeMeshDirtyFlag::MeshletBoundsDirty) != 0u;
    const auto foundRuntimeResources = m_runtimeResources.find(instance.handle.value);
    const bool hadSkinningResources = foundRuntimeResources != m_runtimeResources.end() && foundRuntimeResources.value().usesSkinning();
    if(!hasActiveSkin && !skinnedMeshInputDirty && !meshletBoundsDirty && !hadSkinningResources)
        return false;
    if(instance.meshlets.empty())
        return false;

    if(hasActiveSkin && (!m_skinningBindingLayout || !m_skinningComputePipeline))
        return false;
    if(!m_boundsBindingLayout || !m_boundsComputePipeline)
        return false;

    if(foundRuntimeResources == m_runtimeResources.end())
        return false;
    RuntimeResources* resources = &foundRuntimeResources.value();
    NWB_ASSERT(resources != nullptr);
    NWB_ASSERT(resources->bindlessHeapHandles.resourceSlots.valid());
    NWB_ASSERT(!hasActiveSkin || resources->skinBuffer);
    NWB_ASSERT(!hasActiveSkin || resources->jointPaletteBuffer);
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    bool bindlessResourceSlotsUploadRecorded = false;
    if(!uploadRuntimeResourceBindlessSlots(commandList, *resources, bindlessResourceSlotsUploadRecorded))
        return false;

    if(!hasActiveSkin){
        const bool copiesRestBuffers = skinnedMeshInputDirty || hadSkinningResources;
        if(copiesRestBuffers){
            if(hasGraphOwnedRestCopyPlan(instance))
                transitionGraphCopiedRestStreams(commandList, instance);
            else if(!copyRestToSkinned(commandList, instance))
                return false;
        }

        if(meshletBoundsDirty || copiesRestBuffers){
            if(!dispatchMeshletBounds(commandList, instance, *resources))
                return false;
        }
        // A released pose restores skinned normals from rest data, but the ray-tracing attribute stream still
        // contains the prior active pose until its normal repack runs once more.
        if(copiesRestBuffers && !dispatchRepackNormals(commandList, instance, *resources))
            return false;

        outCommit.handledDirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
            (skinnedMeshInputDirty ? RuntimeMeshDirtyFlag::SkinningInputDirty : RuntimeMeshDirtyFlag::None)
            | (meshletBoundsDirty ? RuntimeMeshDirtyFlag::MeshletBoundsDirty : RuntimeMeshDirtyFlag::None)
        );
        outCommit.bindlessResourceSlotsUploadRecorded = bindlessResourceSlotsUploadRecorded;
        return true;
    }

    __hidden_skinning::SetRestBufferStates(
        commandList,
        instance,
        Core::ResourceStates::ShaderResource
    );
    __hidden_skinning::SetSkinnedBufferStates(
        commandList,
        instance,
        Core::ResourceStates::UnorderedAccess
    );
    commandList.setBufferState(instance.meshletDescBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletPositionRefDeltaBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletAttributeRefDeltaBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.attributeSkinBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(resources->skinBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(resources->jointPaletteBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_skinningComputePipeline.get());
    // Set 0 contains only the push range; all persistent inputs, outputs, and selector payloads are heap entries.
    commandList.setComputeState(computeState);
    heap.bindCompute(commandList, *m_skinningComputePipeline.get());

    MeshSkinningPushConstants pushConstants;
    pushConstants.meshletCount = static_cast<u32>(instance.meshlets.size());
    pushConstants.skinCount = static_cast<u32>(payload.skinInfluences.size());
    pushConstants.jointCount = static_cast<u32>(payload.jointMatrices.size());
    pushConstants.skinningMode = payload.resolvedSkinningMode;
    pushConstants.attributeCount = instance.meshletAttributeRefCount;
    pushConstants.bindlessResourceSlots = resources->bindlessHeapHandles.resourceSlots.slot();
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_Skinning, m_graphics.getDevice(), commandList);

        commandList.dispatch(pushConstants.meshletCount, 1, 1);
    }

    __hidden_skinning::SetSkinnedBufferStates(
        commandList,
        instance,
        Core::ResourceStates::ShaderResource
    );
    commandList.commitBarriers();

    if(!dispatchMeshletBounds(commandList, instance, *resources))
        return false;
    // Re-derive the RT attribute buffer's shading normals from this frame's deformed normals so the shadow + caustic
    // traces bend on the live pose. A released active pose also repacks once after rest data is copied back; steady
    // no-active frames leave skinned==rest and the attribute buffer already holds bind-pose normals.
    if(!dispatchRepackNormals(commandList, instance, *resources))
        return false;

    outCommit.handledDirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        (skinnedMeshInputDirty ? RuntimeMeshDirtyFlag::SkinningInputDirty : RuntimeMeshDirtyFlag::None)
        | (meshletBoundsDirty ? RuntimeMeshDirtyFlag::MeshletBoundsDirty : RuntimeMeshDirtyFlag::None)
    );
    outCommit.bindlessResourceSlotsUploadRecorded = bindlessResourceSlotsUploadRecorded;
    return true;
}

bool MeshSkinningSystem::hasGraphOwnedRestCopyPlan(const MeshSkinningRuntimeInstance& instance)const{
    usize positionBytes = 0u;
    usize normalBytes = 0u;
    usize tangentBytes = 0u;
    if(!resolveRestToSkinnedCopyByteCounts(instance, positionBytes, normalBytes, tangentBytes))
        return false;

    for(const GraphOwnedRestCopyPlan& plan : m_graphOwnedRestCopyPlans){
        if(
            plan.handle == instance.handle
            && plan.editRevision == instance.editRevision
            && plan.restPositionBuffer.get() == instance.restPositionBuffer.get()
            && plan.restNormalBuffer.get() == instance.restNormalBuffer.get()
            && plan.restTangentBuffer.get() == instance.restTangentBuffer.get()
            && plan.skinnedPositionBuffer.get() == instance.skinnedPositionBuffer.get()
            && plan.skinnedNormalBuffer.get() == instance.skinnedNormalBuffer.get()
            && plan.skinnedTangentBuffer.get() == instance.skinnedTangentBuffer.get()
            && plan.positionBytes == positionBytes
            && plan.normalBytes == normalBytes
            && plan.tangentBytes == tangentBytes
        )
            return true;
    }
    return false;
}

bool MeshSkinningSystem::recordGraphOwnedSkinningDeformation(
    const GraphOwnedSkinningDispatchPlan& plan,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    if(!plan.hasActiveSkin)
        return false;

    const auto resolveBuffer = [&](const Core::GpuGraphResourceId resource){
        return context.taskGraph.bufferForResource(resource);
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
    Core::ComputePipeline* const skinningPipeline = context.taskGraph.computePipelineFor(plan.skinningPipeline);
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

    // The graph establishes the selector and static inputs before this callback, and its deformation task declares
    // every generated skinned stream as an UnorderedAccess write. The callback only records the compute dispatch.
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
    const GraphOwnedSkinningDispatchPlan& plan,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    if(!plan.updatesMeshletBounds)
        return false;

    const auto resolveBuffer = [&](const Core::GpuGraphResourceId resource){
        return context.taskGraph.bufferForResource(resource);
    };

    Core::Buffer* const bindlessResourceSlots = resolveBuffer(plan.bindlessResourceSlotsResource);
    Core::Buffer* const skinnedPosition = resolveBuffer(plan.skinnedPositionResource);
    Core::Buffer* const meshletDesc = resolveBuffer(plan.meshletDescResource);
    Core::Buffer* const meshletPositionRefDeltas = resolveBuffer(plan.meshletPositionRefDeltaResource);
    Core::Buffer* const meshletLocalVertexRefs = resolveBuffer(plan.meshletLocalVertexRefResource);
    Core::Buffer* const meshletPrimitiveIndices = resolveBuffer(plan.meshletPrimitiveIndexResource);
    Core::Buffer* const meshletBounds = resolveBuffer(plan.meshletBoundsResource);
    Core::ComputePipeline* const boundsPipeline = context.taskGraph.computePipelineFor(plan.boundsPipeline);
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

    // Bounds consumes graph-declared descriptor-visible skinned positions and static meshlet inputs, then writes
    // graph-declared meshlet-bounds UAV output.
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
        Core::ComputePipeline* const repackPipeline = context.taskGraph.computePipelineFor(plan.repackPipeline);
        if(!skinnedNormal || !meshletAttributeRefDeltas || !attributeBuffer || !repackPipeline)
            return false;

        // The graph lowers the deformation/copy handoff before normal repack, then publishes packed attributes as
        // this task's UnorderedAccess output.
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

void MeshSkinningSystem::confirmGraphOwnedSkinningDispatch(const GraphOwnedSkinningDispatchPlan& plan)noexcept{
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

void MeshSkinningSystem::transitionGraphCopiedRestStreams(
    Core::CommandList& commandList,
    MeshSkinningRuntimeInstance& instance
)const{
    // The graph-owned primitive copy publishes CopyDest. The native bounds pass consumes only positions, while the
    // renderer later consumes normals and tangents too, so preserve the direct-copy helper's complete SRV handoff.
    __hidden_skinning::SetSkinnedBufferStates(
        commandList,
        instance,
        Core::ResourceStates::ShaderResource
    );
    commandList.commitBarriers();
}

bool MeshSkinningSystem::copyRestToSkinned(Core::CommandList& commandList, MeshSkinningRuntimeInstance& instance){
    usize positionBytes = 0;
    usize normalBytes = 0;
    usize tangentBytes = 0;
    if(!resolveRestToSkinnedCopyByteCounts(
        instance,
        positionBytes,
        normalBytes,
        tangentBytes
    ))
        return false;

    __hidden_skinning::SetRestBufferStates(
        commandList,
        instance,
        Core::ResourceStates::CopySource
    );
    __hidden_skinning::SetSkinnedBufferStates(
        commandList,
        instance,
        Core::ResourceStates::CopyDest
    );
    commandList.commitBarriers();
    commandList.copyBuffer(instance.skinnedPositionBuffer.get(), 0, instance.restPositionBuffer.get(), 0, positionBytes);
    commandList.copyBuffer(instance.skinnedNormalBuffer.get(), 0, instance.restNormalBuffer.get(), 0, normalBytes);
    commandList.copyBuffer(instance.skinnedTangentBuffer.get(), 0, instance.restTangentBuffer.get(), 0, tangentBytes);
    __hidden_skinning::SetSkinnedBufferStates(
        commandList,
        instance,
        Core::ResourceStates::ShaderResource
    );
    commandList.commitBarriers();
    return true;
}

bool MeshSkinningSystem::dispatchMeshletBounds(
    Core::CommandList& commandList,
    MeshSkinningRuntimeInstance& instance,
    const RuntimeResources& resources
){
    NWB_ASSERT(resources.bindlessHeapHandles.resourceSlots.valid());
    commandList.setBufferState(instance.skinnedPositionBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletDescBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletPositionRefDeltaBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletLocalVertexRefBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletPrimitiveIndexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletBoundsBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_boundsComputePipeline.get());
    commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_boundsComputePipeline.get());

    MeshletBoundsPushConstants pushConstants;
    pushConstants.meshletCount = static_cast<u32>(instance.meshlets.size());
    pushConstants.bindlessResourceSlots = resources.bindlessHeapHandles.resourceSlots.slot();
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_MeshletBounds, m_graphics.getDevice(), commandList);

        commandList.dispatch(pushConstants.meshletCount, 1, 1);
    }

    commandList.setBufferState(instance.meshletBoundsBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

bool MeshSkinningSystem::dispatchRepackNormals(
    Core::CommandList& commandList,
    MeshSkinningRuntimeInstance& instance,
    const RuntimeResources& resources
){
    // RT unsupported (no attribute buffer) -> nothing to repack; not an error. Active skinning and a released pose
    // both enter with skinnedNormalBuffer holding this frame's intended normals in ShaderResource state. This pass
    // overwrites the triangle-corner normal slots of attributeBuffer in place; the single-instance buffer is safe to
    // write here because the frame-begin fence retires all prior in-flight frames before this command list records,
    // and the renderer's RT reads run later (skinning render pass precedes the renderer pass), exactly as for the
    // skinned position/normal buffers.
    if(!instance.attributeBuffer || !resources.bindlessHeapHandles.resourceSlots.valid() || !m_repackComputePipeline)
        return true;

    commandList.setBufferState(instance.skinnedNormalBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletDescBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletPrimitiveIndexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletAttributeRefDeltaBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletLocalVertexRefBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.attributeBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_repackComputePipeline.get());
    commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_repackComputePipeline.get());

    MeshletRepackPushConstants pushConstants;
    pushConstants.meshletCount = static_cast<u32>(instance.meshlets.size());
    pushConstants.bindlessResourceSlots = resources.bindlessHeapHandles.resourceSlots.slot();
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_RepackNormals, m_graphics.getDevice(), commandList);

        commandList.dispatch(pushConstants.meshletCount, 1, 1);
    }

    commandList.setBufferState(instance.attributeBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

