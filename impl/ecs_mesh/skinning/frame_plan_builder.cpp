// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_plan_builder.h"
#include "frame_resource_imports.h"
#include "frame_graph_tasks.h"
#include "skin_payload.h"

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_plan_builder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ResolveSkeletonComponents(Core::ECS::World& world, const Core::ECS::EntityID fallbackEntity, const Core::ECS::EntityID skeletonEntity, const SkeletonJointPaletteComponent*& outJointPalette, const SkeletonPoseComponent*& outSkeletonPose)noexcept{
    const Core::ECS::EntityID resolvedEntity = skeletonEntity.valid() ? skeletonEntity : fallbackEntity;
    outJointPalette = world.tryGetComponent<SkeletonJointPaletteComponent>(resolvedEntity);
    outSkeletonPose = world.tryGetComponent<SkeletonPoseComponent>(resolvedEntity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshSkinningSystem::FramePlanBuilder::declare(const Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
    if(!binding.runtimeMesh.valid())
        return true;

    MeshSkinningRuntimeInstance* const instance = m_system.m_runtimeMeshCache.findInstance(binding.runtimeMesh);
    if(!instance)
        return true;
    NWB_ASSERT(instance->valid());
    const SkeletonJointPaletteComponent* jointPalette = nullptr;
    const SkeletonPoseComponent* skeletonPose = nullptr;
    __hidden_frame_plan_builder::ResolveSkeletonComponents(m_system.m_world, entity, binding.skeletonEntity, jointPalette, skeletonPose);
    const auto foundResources = m_system.m_runtimeResources.find(instance->handle.value);
    RuntimeSkinPayloadScratch payload{ m_scratchArena };
    // Preserve the direct path's per-mesh retry semantics: an invalid pose never prevents another ready mesh from declaring its immutable packet plan.
    if(!MeshSkinningPayload::BuildRuntimeSkinPayload(*instance, jointPalette, skeletonPose, payload)){
        instance->deformationState.invalidateCurrent();
        return true;
    }

    const b8 hasActiveSkin = payload.hasActiveSkin();
    Span<const SkeletonJointMatrix> jointPaletteUpload;
    if(hasActiveSkin){
        const auto upload = MeshSkinningPayload::ResolveGpuJointPalette(*instance, payload);
        if(!upload){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: current joint palette does not match Skin upload bounds for runtime mesh '{}'"), instance->handle.value);
            return false;
        }
        jointPaletteUpload = *upload;
    }
    const b8 skinningInputDirty = (instance->dirtyFlags & RuntimeMeshDirtyFlag::SkinningInputDirty) != 0u;
    const b8 meshletBoundsDirty = (instance->dirtyFlags & RuntimeMeshDirtyFlag::MeshletBoundsDirty) != 0u;
    if(instance->meshlets.empty())
        return true;

    if(foundResources == m_system.m_runtimeResources.end()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has no prepared graph resources"), instance->handle.value);
        return false;
    }
    const RuntimeResources& resources = foundResources.value();
    const MeshSkinningDeformationInputs deformationInputs{ .resourceSlotsBuffer = resources.bindlessResourceSlotsBuffer, .editRevision = instance->editRevision, .skinningMode = payload.resolvedSkinningMode, };
    instance->deformationState.refreshCurrent(deformationInputs, payload.jointMatrices, skinningInputDirty || meshletBoundsDirty);
    const b8 updatesMeshletBounds = instance->deformationState.contentRevision() == 0u;
    if(!updatesMeshletBounds)
        return true;
    const b8 copiesRestStreams = !hasActiveSkin;
    if(
        !resources.bindlessResourceSlotsBuffer
        || !resources.bindlessHeapHandles.resourceSlots.valid()
        || resources.bindlessHeapHandles.resourceSlots.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
        || !m_system.m_boundsComputePipeline || !m_system.m_localBoundsComputePipeline
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has incomplete graph-owned dispatch state"), instance->handle.value);
        return false;
    }
    if(hasActiveSkin && (!resources.skinBuffer || !resources.jointPaletteBuffer || !m_system.m_skinningComputePipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: active runtime mesh '{}' has no graph-owned skinning pipeline or payload"), instance->handle.value);
        return false;
    }

    MeshSkinningGraphDispatchPlan plan;
    plan.handle = instance->handle;
    plan.submissionCommit.editRevision = instance->editRevision;
    plan.submissionCommit.handledDirtyFlags = ResolveHandledSkinningDirtyFlags(skinningInputDirty, meshletBoundsDirty);
    plan.hasActiveSkin = hasActiveSkin;
    plan.copiedRestStreams = copiesRestStreams;
    plan.updatesMeshletBounds = updatesMeshletBounds;
    plan.repacksNormals = (hasActiveSkin || copiesRestStreams) && instance->attributeBuffer != nullptr;
    plan.meshletCount = static_cast<u32>(instance->meshlets.size());
    plan.skinCount = static_cast<u32>(payload.skinInfluenceCount);
    plan.jointCount = static_cast<u32>(jointPaletteUpload.size());
    plan.skinningMode = payload.resolvedSkinningMode;
    plan.bindlessResourceSlots = resources.bindlessHeapHandles.resourceSlots.slot();
    plan.bindlessResourceSlotsBuffer = resources.bindlessResourceSlotsBuffer;
    plan.bindlessResourceSlotsDescriptor = resources.bindlessHeapHandles.resourceSlots;
    plan.bindlessResourceSlotsPayload = resources.bindlessResourceSlots;

    if(plan.repacksNormals && !m_system.m_repackComputePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' requires a missing normal-repack pipeline"), instance->handle.value);
        return false;
    }

    if(!FrameResourceImports(m_system, m_graph).import(*instance, resources, plan))
        return false;
    if(!resources.bindlessResourceSlotsUploaded && !declareBindlessSlotsUpload(*instance, resources, plan))
        return false;
    if(copiesRestStreams && !declareRestToSkinnedCopy(*instance, plan))
        return false;
    if(hasActiveSkin && !declareJointPaletteUpload(*instance, jointPaletteUpload, plan))
        return false;

    const auto deformationCandidate = instance->deformationState.stage(deformationInputs, payload.jointMatrices);
    if(!deformationCandidate)
        return false;
    plan.deformationCandidate = *deformationCandidate;
    m_plans.push_back(Move(plan));
    return true;
}

b8 MeshSkinningSystem::FramePlanBuilder::declareUploadTask(
    const MeshSkinningRuntimeInstance& instance,
    const AStringView identitySuffix,
    const AStringView markerLabel,
    const BinaryByteView data,
    const usize dataAlignment,
    const Core::GpuGraphResourceId destination,
    const Core::ResourceStates::Mask finalState,
    const TStringView retainErrorLabel,
    const TStringView declareErrorLabel
){
    const auto uploadIdentity = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, identitySuffix);
    const auto uploadSourceResult = m_graph.copyUploadData(data.data(), data.size(), dataAlignment);
    if(!uploadIdentity || !uploadSourceResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain {} for runtime mesh '{}'"), retainErrorLabel, instance.handle.value);
        return false;
    }

    Core::GpuTaskDesc uploadDesc;
    uploadDesc.setIdentity(uploadIdentity).setMarkerLabel(markerLabel).setScheduling(MeshSkinningGraphDetail::JointPaletteUploadScheduling());
    if(m_terminalTask.valid())
        uploadDesc.setDependencies({ &m_terminalTask, 1u });

    const auto uploadTaskResult = m_graph.addUploadBufferTask(
        uploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = *uploadSourceResult,
            .destination = destination,
            .finalState = finalState,
        });
    if(!uploadTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare {} for runtime mesh '{}'"), declareErrorLabel, instance.handle.value);
        return false;
    }
    m_terminalTask = *uploadTaskResult;
    return true;
}

b8 MeshSkinningSystem::FramePlanBuilder::declareBindlessSlotsUpload(const MeshSkinningRuntimeInstance& instance, const RuntimeResources& resources, MeshSkinningGraphDispatchPlan& plan){
    // The following graph-owned compute task declares the descriptor-visible ConstantBuffer state.
    if(!declareUploadTask(
        instance,
        s_BindlessSlotsUploadSuffix,
        s_BindlessSlotsUploadLabel,
        BinaryByteView{ reinterpret_cast<const u8*>(&resources.bindlessResourceSlots), sizeof(resources.bindlessResourceSlots) },
        alignof(MeshSkinningBindlessResourceSlots),
        plan.bindlessResourceSlotsResource,
        Core::ResourceStates::Common,
        NWB_TEXT("graph-owned bindless slots"),
        NWB_TEXT("graph-owned bindless slots")
    ))
        return false;

    plan.submissionCommit.bindlessResourceSlotsUploadRecorded = true;
    return true;
}

b8 MeshSkinningSystem::FramePlanBuilder::declareRestToSkinnedCopy(const MeshSkinningRuntimeInstance& instance, const MeshSkinningGraphDispatchPlan& plan){
    const Expected<MeshSkinningRestToSkinnedCopyByteCounts> copyByteCounts = TryResolveMeshSkinningRestToSkinnedCopyByteCounts(instance.restPositions.size(), instance.restNormals.size(), instance.restTangents.size());
    if(!copyByteCounts){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to resolve rest-to-skinned copy sizes for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    const Array<Core::GpuCopyBufferTaskRegion, 3> copyRegions = {
        Core::GpuCopyBufferTaskRegion{ .source = plan.restPositionResource, .destination = plan.skinnedPositionResource, .dataSizeBytes = copyByteCounts->positionBytes, },
        Core::GpuCopyBufferTaskRegion{
            .source = plan.restNormalResource,
            .destination = plan.skinnedNormalResource,
            .dataSizeBytes = copyByteCounts->normalBytes,
        },
        Core::GpuCopyBufferTaskRegion{ .source = plan.restTangentResource, .destination = plan.skinnedTangentResource, .dataSizeBytes = copyByteCounts->tangentBytes, },
    };
    Core::GpuTaskDesc copyDesc;
    const auto copyDescIdentity = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, s_RestToSkinnedCopySuffix);
    if(!copyDescIdentity){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive graph identity for rest-to-skinned copy"));
        return false;
    }
    copyDesc.setIdentity(copyDescIdentity).setMarkerLabel(s_RestToSkinnedCopyLabel).setScheduling(MeshSkinningGraphDetail::JointPaletteUploadScheduling());

    if(m_terminalTask.valid())
        copyDesc.setDependencies({ &m_terminalTask, 1u });

    const auto copyTaskResult = m_graph.addCopyBufferTask(copyDesc, Core::GpuCopyBufferTaskDesc{ .regions = copyRegions.data(), .regionCount = copyRegions.size(), });
    if(!copyTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned rest-to-skinned copy for runtime mesh '{}'"), instance.handle.value);
        return false;
    }
    const Core::GpuTaskId copyTask = *copyTaskResult;
    m_terminalTask = copyTask;
    return true;
}

b8 MeshSkinningSystem::FramePlanBuilder::declareJointPaletteUpload(const MeshSkinningRuntimeInstance& instance, const Span<const SkeletonJointMatrix> jointPaletteUpload, const MeshSkinningGraphDispatchPlan& plan){
    const auto jointPaletteBytes = TryComputeRuntimeMeshBufferByteCount(jointPaletteUpload.size(), sizeof(SkeletonJointMatrix));
    if(!jointPaletteBytes || *jointPaletteBytes == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette payload byte size overflows"));
        return false;
    }
    return declareUploadTask(
        instance,
        s_JointPaletteUploadSuffix,
        s_JointPaletteUploadLabel,
        BinaryByteView{ reinterpret_cast<const u8*>(jointPaletteUpload.data()), *jointPaletteBytes },
        alignof(SkeletonJointMatrix),
        plan.jointPaletteResource,
        Core::ResourceStates::ShaderResource,
        NWB_TEXT("graph-owned joint palette data"),
        NWB_TEXT("graph-owned joint palette upload")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

