// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_plan_builder.h"
#include "frame_graph_tasks.h"
#include "resource_names.h"
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

static constexpr AStringView s_SkinnedPositionsLabel = "Runtime Skinned Positions";
static constexpr AStringView s_SkinnedNormalsLabel = "Runtime Skinned Normals";
static constexpr AStringView s_MeshletDescriptorsLabel = "Runtime Meshlet Descriptors";
static constexpr AStringView s_MeshletLocalVertexRefsLabel = "Runtime Meshlet Local Vertex Refs";
static constexpr AStringView s_MeshletPrimitiveIndicesLabel = "Runtime Meshlet Primitive Indices";
static constexpr AStringView s_MeshletAttributeDeltasLabel = "Runtime Meshlet Attribute Deltas";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshSkinningSystem::FramePlanBuilder::declare(const Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
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

    const bool hasActiveSkin = payload.hasActiveSkin();
    Span<const SkeletonJointMatrix> jointPaletteUpload;
    if(hasActiveSkin){
        const auto upload = MeshSkinningPayload::ResolveGpuJointPalette(*instance, payload);
        if(!upload){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: current joint palette does not match Skin upload bounds for runtime mesh '{}'"), instance->handle.value);
            return false;
        }
        jointPaletteUpload = *upload;
    }
    const bool skinningInputDirty = (instance->dirtyFlags & RuntimeMeshDirtyFlag::SkinningInputDirty) != 0u;
    const bool meshletBoundsDirty = (instance->dirtyFlags & RuntimeMeshDirtyFlag::MeshletBoundsDirty) != 0u;
    if(instance->meshlets.empty())
        return true;

    if(foundResources == m_system.m_runtimeResources.end()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' has no prepared graph resources"), instance->handle.value);
        return false;
    }
    const RuntimeResources& resources = foundResources.value();
    const MeshSkinningDeformationInputs deformationInputs{ .resourceSlotsBuffer = resources.bindlessResourceSlotsBuffer, .editRevision = instance->editRevision, .skinningMode = payload.resolvedSkinningMode, };
    instance->deformationState.refreshCurrent(deformationInputs, payload.jointMatrices.data(), payload.jointMatrices.size(), skinningInputDirty || meshletBoundsDirty);
    const bool updatesMeshletBounds = instance->deformationState.contentRevision() == 0u;
    if(!updatesMeshletBounds)
        return true;
    const bool copiesRestStreams = !hasActiveSkin;
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
    plan.submissionCommit.handledDirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        (skinningInputDirty ? RuntimeMeshDirtyFlag::SkinningInputDirty : RuntimeMeshDirtyFlag::None)
        | (meshletBoundsDirty ? RuntimeMeshDirtyFlag::MeshletBoundsDirty : RuntimeMeshDirtyFlag::None)
    );
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

    const auto deformationCandidate = instance->deformationState.stage(deformationInputs, payload.jointMatrices.data(), payload.jointMatrices.size());
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

Expected<Core::GpuGraphPipelineId> MeshSkinningSystem::FramePlanBuilder::importComputePipeline(const Core::ComputePipelineHandle& pipeline, const Name identity, const AStringView label){
    return m_graph.importComputePipeline(pipeline, Core::GpuGraphPipelineDesc{}.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphPipelineType::Compute));
}

bool MeshSkinningSystem::FramePlanBuilder::importPlanResources(const MeshSkinningRuntimeInstance& instance, const RuntimeResources& resources, MeshSkinningGraphDispatchPlan& plan){
    const bool hasActiveSkin = plan.hasActiveSkin;
    const bool copiesRestStreams = plan.copiedRestStreams;
    const bool updatesMeshletBounds = plan.updatesMeshletBounds;

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
        skinnedPositionResourceResult = importBuffer(instance.skinnedPositionBuffer, __hidden_frame_plan_builder::s_SkinnedPositionsLabel);
        skinnedNormalResourceResult = importBuffer(instance.skinnedNormalBuffer, __hidden_frame_plan_builder::s_SkinnedNormalsLabel);
        skinnedTangentResourceResult = importBuffer(instance.skinnedTangentBuffer, "Runtime Skinned Tangents");
    }
    if(updatesMeshletBounds){
        if(!skinnedPositionResourceResult.has_value())
            skinnedPositionResourceResult = importBuffer(instance.skinnedPositionBuffer, __hidden_frame_plan_builder::s_SkinnedPositionsLabel);
        meshletDescResourceResult = importBuffer(instance.meshletDescBuffer, __hidden_frame_plan_builder::s_MeshletDescriptorsLabel);
        meshletPositionRefDeltaResourceResult = importBuffer(instance.meshletPositionRefDeltaBuffer, "Runtime Meshlet Position Deltas");
        meshletLocalVertexRefResourceResult = importBuffer(instance.meshletLocalVertexRefBuffer, __hidden_frame_plan_builder::s_MeshletLocalVertexRefsLabel);
        meshletPrimitiveIndexResourceResult = importBuffer(instance.meshletPrimitiveIndexBuffer, __hidden_frame_plan_builder::s_MeshletPrimitiveIndicesLabel);
        meshletBoundsResourceResult = importBuffer(instance.meshletBoundsBuffer, "Runtime Meshlet Bounds");
        meshletLocalBoundsResourceResult = importBuffer(instance.meshletLocalBoundsBuffer, "Runtime Meshlet Local Bounds");
        localBoundsResourceResult = importBuffer(instance.localBoundsBuffer, "Runtime Local Bounds");
    }
    if(hasActiveSkin){
        meshletAttributeRefDeltaResourceResult = importBuffer(instance.meshletAttributeRefDeltaBuffer, __hidden_frame_plan_builder::s_MeshletAttributeDeltasLabel);
        attributeSkinResourceResult = importBuffer(instance.attributeSkinBuffer, "Runtime Attribute Skins");
        skinResourceResult = importBuffer(resources.skinBuffer, "Skinning Influences");
        jointPaletteResourceResult = importBuffer(resources.jointPaletteBuffer, "Skinning Joint Palette");
    }
    if(plan.repacksNormals){
        if(!skinnedNormalResourceResult.has_value())
            skinnedNormalResourceResult = importBuffer(instance.skinnedNormalBuffer, __hidden_frame_plan_builder::s_SkinnedNormalsLabel);
        if(!meshletDescResourceResult.has_value())
            meshletDescResourceResult = importBuffer(instance.meshletDescBuffer, __hidden_frame_plan_builder::s_MeshletDescriptorsLabel);
        if(!meshletPrimitiveIndexResourceResult.has_value())
            meshletPrimitiveIndexResourceResult = importBuffer(instance.meshletPrimitiveIndexBuffer, __hidden_frame_plan_builder::s_MeshletPrimitiveIndicesLabel);
        if(!meshletAttributeRefDeltaResourceResult.has_value())
            meshletAttributeRefDeltaResourceResult = importBuffer(instance.meshletAttributeRefDeltaBuffer, __hidden_frame_plan_builder::s_MeshletAttributeDeltasLabel);
        if(!meshletLocalVertexRefResourceResult.has_value())
            meshletLocalVertexRefResourceResult = importBuffer(instance.meshletLocalVertexRefBuffer, __hidden_frame_plan_builder::s_MeshletLocalVertexRefsLabel);
        attributeResourceResult = importBuffer(instance.attributeBuffer, "Runtime Ray Trace Attributes");
    }

    boundsPipelineResult = importComputePipeline(m_system.m_boundsComputePipeline, Name("mesh_skinning.graph_bounds_pipeline"), "Skinning Bounds Pipeline");
    localBoundsPipelineResult = importComputePipeline(m_system.m_localBoundsComputePipeline, Name("mesh_skinning.graph_local_bounds_pipeline"), "Skinning Local Bounds Pipeline");
    if(hasActiveSkin){
        skinningPipelineResult = importComputePipeline(m_system.m_skinningComputePipeline, Name("mesh_skinning.graph_skinning_pipeline"), "Skinning Compute Pipeline");
    }
    if(plan.repacksNormals){
        repackPipelineResult = importComputePipeline(m_system.m_repackComputePipeline, Name("mesh_skinning.graph_repack_pipeline"), "Skinning Repack Pipeline");
    }

    const bool importsValid =
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

bool MeshSkinningSystem::FramePlanBuilder::declareBindlessSlotsUpload(const MeshSkinningRuntimeInstance& instance, const RuntimeResources& resources, MeshSkinningGraphDispatchPlan& plan){
    const auto uploadIdentity = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, "mesh_skinning_bindless_slots_upload");
    const auto selectorSourceResult = m_graph.copyUploadData(&resources.bindlessResourceSlots, sizeof(resources.bindlessResourceSlots), alignof(MeshSkinningBindlessResourceSlots));
    if(!uploadIdentity || !selectorSourceResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned bindless slots for runtime mesh '{}'"), instance.handle.value);
        return false;
    }
    const Core::GpuUploadBlobId selectorSource = *selectorSourceResult;

    Core::GpuTaskDesc selectorUploadDesc;
    selectorUploadDesc.setIdentity(*uploadIdentity).setMarkerLabel("Skinning Bindless Slots Upload").setScheduling(MeshSkinningGraphDetail::JointPaletteUploadScheduling());
    if(m_terminalTask.valid())
        selectorUploadDesc.setDependencies(&m_terminalTask, 1u);

    const auto selectorUploadTaskResult = m_graph.addUploadBufferTask(
        selectorUploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = selectorSource,
            .destination = plan.bindlessResourceSlotsResource,
            // The following graph-owned compute task declares the descriptor-visible ConstantBuffer state.
            .finalState = Core::ResourceStates::Common, }
    );
    if(!selectorUploadTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned bindless slots for runtime mesh '{}'"), instance.handle.value);
        return false;
    }
    const Core::GpuTaskId selectorUploadTask = *selectorUploadTaskResult;
    plan.submissionCommit.bindlessResourceSlotsUploadRecorded = true;
    m_terminalTask = selectorUploadTask;
    return true;
}

bool MeshSkinningSystem::FramePlanBuilder::declareRestToSkinnedCopy(const MeshSkinningRuntimeInstance& instance, const MeshSkinningGraphDispatchPlan& plan){
    const auto copyByteCounts = MeshSkinningSystem::ResolveRestToSkinnedCopyByteCounts(instance);
    if(!copyByteCounts || copyByteCounts->positionBytes == 0u || copyByteCounts->normalBytes == 0u || copyByteCounts->tangentBytes == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to resolve rest-to-skinned copy sizes for runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    const Core::GpuCopyBufferTaskRegion copyRegions[] = {
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
    copyDesc.setIdentity(*copyDescIdentity).setMarkerLabel("Skinning Rest-to-Skinned Copy").setScheduling(MeshSkinningGraphDetail::JointPaletteUploadScheduling());

    if(m_terminalTask.valid())
        copyDesc.setDependencies(&m_terminalTask, 1u);

    const auto copyTaskResult = m_graph.addCopyBufferTask(copyDesc, Core::GpuCopyBufferTaskDesc{ .regions = copyRegions, .regionCount = LengthOf(copyRegions), });
    if(!copyTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned rest-to-skinned copy for runtime mesh '{}'"), instance.handle.value);
        return false;
    }
    const Core::GpuTaskId copyTask = *copyTaskResult;
    m_terminalTask = copyTask;
    return true;
}

bool MeshSkinningSystem::FramePlanBuilder::declareJointPaletteUpload(const MeshSkinningRuntimeInstance& instance, const Span<const SkeletonJointMatrix> jointPaletteUpload, const MeshSkinningGraphDispatchPlan& plan){
    usize jointPaletteBytes = 0u;
    if(jointPaletteUpload.size() > Limit<usize>::s_Max / sizeof(SkeletonJointMatrix) || (jointPaletteBytes = jointPaletteUpload.size() * sizeof(SkeletonJointMatrix)) == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: joint palette payload byte size overflows"));
        return false;
    }
    const auto jointPaletteSourceResult = m_graph.copyUploadData(jointPaletteUpload.data(), jointPaletteBytes, alignof(SkeletonJointMatrix));
    if(!jointPaletteSourceResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to retain graph-owned joint palette data for runtime mesh '{}'"), instance.handle.value);
        return false;
    }
    const Core::GpuUploadBlobId jointPaletteSource = *jointPaletteSourceResult;

    Core::GpuTaskDesc jointPaletteUploadDesc;
    const auto jointPaletteUploadDescIdentity = DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, "mesh_skinning_joint_palette_upload");
    if(!jointPaletteUploadDescIdentity){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to derive graph identity for joint palette upload"));
        return false;
    }
    jointPaletteUploadDesc.setIdentity(*jointPaletteUploadDescIdentity).setMarkerLabel("Skinning Joint Palette Upload").setScheduling(MeshSkinningGraphDetail::JointPaletteUploadScheduling());

    if(m_terminalTask.valid())
        jointPaletteUploadDesc.setDependencies(&m_terminalTask, 1u);

    const auto jointPaletteUploadTaskResult = m_graph.addUploadBufferTask(
        jointPaletteUploadDesc,
        Core::GpuUploadBufferTaskDesc{ .source = jointPaletteSource, .destination = plan.jointPaletteResource, .finalState = Core::ResourceStates::ShaderResource, }
    );
    if(!jointPaletteUploadTaskResult){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to declare graph-owned joint palette upload for runtime mesh '{}'"), instance.handle.value);
        return false;
    }
    const Core::GpuTaskId jointPaletteUploadTask = *jointPaletteUploadTaskResult;
    m_terminalTask = jointPaletteUploadTask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

