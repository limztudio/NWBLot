// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_plan_builder.h"
#include "frame_graph_tasks.h"
#include "skin_payload.h"

#include <impl/ecs_skeleton/runtime_helpers.h>

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
    plan.submissionCommit.handledDirtyFlags =
        (skinningInputDirty ? RuntimeMeshDirtyFlag::SkinningInputDirty : RuntimeMeshDirtyFlag::None)
        | (meshletBoundsDirty ? RuntimeMeshDirtyFlag::MeshletBoundsDirty : RuntimeMeshDirtyFlag::None)
    ;
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

    if(!importPlanResources(*instance, resources, plan))
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

Expected<Core::GpuGraphResourceId> MeshSkinningSystem::FramePlanBuilder::importBuffer(const Core::BufferHandle& buffer, const AStringView label){
    if(!buffer)
        return MakeUnexpected(Failure{});
    const Core::BufferDesc& description = buffer->getCreationDescription();
    if(!description.debugName)
        return MakeUnexpected(Failure{});
    return m_graph.importBuffer(
        buffer,
        Core::GpuGraphResourceDesc{}
            .setIdentity(description.debugName)
            .setMarkerLabel(label)
            .setType(Core::GpuGraphResourceType::Buffer)
            .setInitialState(description.initialState)
            .setQueueSharing(description.queueSharing)
    );
}

b8 MeshSkinningSystem::FramePlanBuilder::importPlanResources(const MeshSkinningRuntimeInstance& instance, const RuntimeResources& resources, MeshSkinningGraphDispatchPlan& plan){
    const b8 hasActiveSkin = plan.hasActiveSkin;
    const b8 copiesRestStreams = plan.copiedRestStreams;
    const b8 updatesMeshletBounds = plan.updatesMeshletBounds;

    Expected<Core::GpuGraphResourceId> bindlessResourceSlotsResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> restPositionResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> restNormalResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> restTangentResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> skinnedPositionResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> skinnedNormalResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> skinnedTangentResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> meshletDescResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> meshletPositionRefDeltaResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> meshletLocalVertexRefResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> meshletPrimitiveIndexResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> meshletBoundsResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> meshletLocalBoundsResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> localBoundsResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> meshletAttributeRefDeltaResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> attributeSkinResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> skinResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> jointPaletteResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphResourceId> attributeResourceResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphPipelineId> boundsPipelineResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphPipelineId> localBoundsPipelineResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphPipelineId> skinningPipelineResult = MakeUnexpected(Failure{});
    Expected<Core::GpuGraphPipelineId> repackPipelineResult = MakeUnexpected(Failure{});

    bindlessResourceSlotsResourceResult = importBuffer(plan.bindlessResourceSlotsBuffer, "Skinning Bindless Slots");
    if(hasActiveSkin || copiesRestStreams){
        restPositionResourceResult = importBuffer(instance.restPositionBuffer, "Runtime Rest Positions");
        restNormalResourceResult = importBuffer(instance.restNormalBuffer, "Runtime Rest Normals");
        restTangentResourceResult = importBuffer(instance.restTangentBuffer, "Runtime Rest Tangents");
        skinnedPositionResourceResult = importBuffer(instance.skinnedPositionBuffer, s_SkinnedPositionsLabel);
        skinnedNormalResourceResult = importBuffer(instance.skinnedNormalBuffer, s_SkinnedNormalsLabel);
        skinnedTangentResourceResult = importBuffer(instance.skinnedTangentBuffer, "Runtime Skinned Tangents");
    }
    const auto importBufferIfNeeded = [this](Expected<Core::GpuGraphResourceId>& slot, const Core::BufferHandle& buffer, const AStringView label){
        if(!slot.has_value())
            slot = importBuffer(buffer, label);
    };
    if(updatesMeshletBounds){
        importBufferIfNeeded(skinnedPositionResourceResult, instance.skinnedPositionBuffer, s_SkinnedPositionsLabel);
        meshletDescResourceResult = importBuffer(instance.meshletDescBuffer, s_MeshletDescriptorsLabel);
        meshletPositionRefDeltaResourceResult = importBuffer(instance.meshletPositionRefDeltaBuffer, "Runtime Meshlet Position Deltas");
        meshletLocalVertexRefResourceResult = importBuffer(instance.meshletLocalVertexRefBuffer, s_MeshletLocalVertexRefsLabel);
        meshletPrimitiveIndexResourceResult = importBuffer(instance.meshletPrimitiveIndexBuffer, s_MeshletPrimitiveIndicesLabel);
        meshletBoundsResourceResult = importBuffer(instance.meshletBoundsBuffer, "Runtime Meshlet Bounds");
        meshletLocalBoundsResourceResult = importBuffer(instance.meshletLocalBoundsBuffer, "Runtime Meshlet Local Bounds");
        localBoundsResourceResult = importBuffer(instance.localBoundsBuffer, "Runtime Local Bounds");
    }
    if(hasActiveSkin){
        meshletAttributeRefDeltaResourceResult = importBuffer(instance.meshletAttributeRefDeltaBuffer, s_MeshletAttributeDeltasLabel);
        attributeSkinResourceResult = importBuffer(instance.attributeSkinBuffer, "Runtime Attribute Skins");
        skinResourceResult = importBuffer(resources.skinBuffer, "Skinning Influences");
        jointPaletteResourceResult = importBuffer(resources.jointPaletteBuffer, "Skinning Joint Palette");
    }
    if(plan.repacksNormals){
        importBufferIfNeeded(skinnedNormalResourceResult, instance.skinnedNormalBuffer, s_SkinnedNormalsLabel);
        importBufferIfNeeded(meshletDescResourceResult, instance.meshletDescBuffer, s_MeshletDescriptorsLabel);
        importBufferIfNeeded(meshletPrimitiveIndexResourceResult, instance.meshletPrimitiveIndexBuffer, s_MeshletPrimitiveIndicesLabel);
        importBufferIfNeeded(meshletAttributeRefDeltaResourceResult, instance.meshletAttributeRefDeltaBuffer, s_MeshletAttributeDeltasLabel);
        importBufferIfNeeded(meshletLocalVertexRefResourceResult, instance.meshletLocalVertexRefBuffer, s_MeshletLocalVertexRefsLabel);
        attributeResourceResult = importBuffer(instance.attributeBuffer, "Runtime Ray Trace Attributes");
    }

    boundsPipelineResult = importComputePipeline(m_system.m_boundsComputePipeline, s_GraphBoundsPipelineName, "Skinning Bounds Pipeline");
    localBoundsPipelineResult = importComputePipeline(m_system.m_localBoundsComputePipeline, s_GraphLocalBoundsPipelineName, "Skinning Local Bounds Pipeline");
    if(hasActiveSkin){
        skinningPipelineResult = importComputePipeline(m_system.m_skinningComputePipeline, s_GraphSkinningPipelineName, "Skinning Compute Pipeline");
    }
    if(plan.repacksNormals){
        repackPipelineResult = importComputePipeline(m_system.m_repackComputePipeline, s_GraphRepackPipelineName, "Skinning Repack Pipeline");
    }

    const b8 importsValid =
        bindlessResourceSlotsResourceResult.has_value()
        && skinnedPositionResourceResult.has_value()
        && meshletDescResourceResult.has_value()
        && meshletPositionRefDeltaResourceResult.has_value()
        && meshletLocalVertexRefResourceResult.has_value()
        && meshletPrimitiveIndexResourceResult.has_value()
        && meshletBoundsResourceResult.has_value()
        && boundsPipelineResult.has_value()
        && meshletLocalBoundsResourceResult.has_value()
        && localBoundsResourceResult.has_value()
        && localBoundsPipelineResult.has_value()
        && (!hasActiveSkin || (
            restPositionResourceResult.has_value()
            && restNormalResourceResult.has_value()
            && restTangentResourceResult.has_value()
            && skinnedNormalResourceResult.has_value()
            && skinnedTangentResourceResult.has_value()
            && meshletAttributeRefDeltaResourceResult.has_value()
            && attributeSkinResourceResult.has_value()
            && skinResourceResult.has_value()
            && jointPaletteResourceResult.has_value()
            && skinningPipelineResult.has_value()
        ))
        && (!copiesRestStreams || (restPositionResourceResult.has_value() && restNormalResourceResult.has_value() && restTangentResourceResult.has_value() && skinnedNormalResourceResult.has_value() && skinnedTangentResourceResult.has_value()))
        && (!plan.repacksNormals || (skinnedNormalResourceResult.has_value() && meshletAttributeRefDeltaResourceResult.has_value() && attributeResourceResult.has_value() && repackPipelineResult.has_value()))
    ;
    if(!importsValid){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has no graph-importable dispatch resource"), instance.handle.value);
        return false;
    }

    if(bindlessResourceSlotsResourceResult)
        plan.bindlessResourceSlotsResource = *bindlessResourceSlotsResourceResult;
    if(restPositionResourceResult)
        plan.restPositionResource = *restPositionResourceResult;
    if(restNormalResourceResult)
        plan.restNormalResource = *restNormalResourceResult;
    if(restTangentResourceResult)
        plan.restTangentResource = *restTangentResourceResult;
    if(skinnedPositionResourceResult)
        plan.skinnedPositionResource = *skinnedPositionResourceResult;
    if(skinnedNormalResourceResult)
        plan.skinnedNormalResource = *skinnedNormalResourceResult;
    if(skinnedTangentResourceResult)
        plan.skinnedTangentResource = *skinnedTangentResourceResult;
    if(meshletDescResourceResult)
        plan.meshletDescResource = *meshletDescResourceResult;
    if(meshletPositionRefDeltaResourceResult)
        plan.meshletPositionRefDeltaResource = *meshletPositionRefDeltaResourceResult;
    if(meshletLocalVertexRefResourceResult)
        plan.meshletLocalVertexRefResource = *meshletLocalVertexRefResourceResult;
    if(meshletPrimitiveIndexResourceResult)
        plan.meshletPrimitiveIndexResource = *meshletPrimitiveIndexResourceResult;
    if(meshletBoundsResourceResult)
        plan.meshletBoundsResource = *meshletBoundsResourceResult;
    if(meshletLocalBoundsResourceResult)
        plan.meshletLocalBoundsResource = *meshletLocalBoundsResourceResult;
    if(localBoundsResourceResult)
        plan.localBoundsResource = *localBoundsResourceResult;
    if(meshletAttributeRefDeltaResourceResult)
        plan.meshletAttributeRefDeltaResource = *meshletAttributeRefDeltaResourceResult;
    if(attributeSkinResourceResult)
        plan.attributeSkinResource = *attributeSkinResourceResult;
    if(skinResourceResult)
        plan.skinResource = *skinResourceResult;
    if(jointPaletteResourceResult)
        plan.jointPaletteResource = *jointPaletteResourceResult;
    if(attributeResourceResult)
        plan.attributeResource = *attributeResourceResult;
    if(boundsPipelineResult)
        plan.boundsPipeline = *boundsPipelineResult;
    if(localBoundsPipelineResult)
        plan.localBoundsPipeline = *localBoundsPipelineResult;
    if(skinningPipelineResult)
        plan.skinningPipeline = *skinningPipelineResult;
    if(repackPipelineResult)
        plan.repackPipeline = *repackPipelineResult;
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
        }
    );
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
        "mesh_skinning_bindless_slots_upload",
        "Skinning Bindless Slots Upload",
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
    const auto copyByteCounts = MeshSkinningSystem::ResolveRestToSkinnedCopyByteCounts(instance);
    if(!copyByteCounts || copyByteCounts->positionBytes == 0u || copyByteCounts->normalBytes == 0u || copyByteCounts->tangentBytes == 0u){
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
    const auto copyDescIdentity = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, "mesh_skinning_rest_to_skinned_copy");
    if(!copyDescIdentity){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive graph identity for rest-to-skinned copy"));
        return false;
    }
    copyDesc.setIdentity(copyDescIdentity).setMarkerLabel("Skinning Rest-to-Skinned Copy").setScheduling(MeshSkinningGraphDetail::JointPaletteUploadScheduling());

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
    usize jointPaletteBytes = 0u;
    if(jointPaletteUpload.size() > Limit<usize>::s_Max / sizeof(SkeletonJointMatrix) || (jointPaletteBytes = jointPaletteUpload.size() * sizeof(SkeletonJointMatrix)) == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette payload byte size overflows"));
        return false;
    }
    return declareUploadTask(
        instance,
        "mesh_skinning_joint_palette_upload",
        "Skinning Joint Palette Upload",
        BinaryByteView{ reinterpret_cast<const u8*>(jointPaletteUpload.data()), jointPaletteBytes },
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

