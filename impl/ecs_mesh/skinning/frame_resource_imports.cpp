// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_resource_imports.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuGraphResourceId> MeshSkinningSystem::FrameResourceImports::importBuffer(const Core::BufferHandle& buffer, const AStringView label){
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

Expected<Core::GpuGraphPipelineId> MeshSkinningSystem::FrameResourceImports::importComputePipeline(const Core::ComputePipelineHandle& pipeline, const Name identity, const AStringView label){
    return m_graph.importComputePipeline(pipeline, Core::GpuGraphPipelineDesc{}.setIdentity(identity).setMarkerLabel(label).setType(Core::GpuGraphPipelineType::Compute));
}

b8 MeshSkinningSystem::FrameResourceImports::import(const MeshSkinningRuntimeInstance& instance, const RuntimeResources& resources, MeshSkinningGraphDispatchPlan& plan){
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

    bindlessResourceSlotsResourceResult = importBuffer(plan.bindlessResourceSlotsBuffer, s_BindlessSlotsLabel);
    if(hasActiveSkin || copiesRestStreams){
        restPositionResourceResult = importBuffer(instance.restPositionBuffer, s_RestPositionsLabel);
        restNormalResourceResult = importBuffer(instance.restNormalBuffer, s_RestNormalsLabel);
        restTangentResourceResult = importBuffer(instance.restTangentBuffer, s_RestTangentsLabel);
        skinnedPositionResourceResult = importBuffer(instance.skinnedPositionBuffer, s_SkinnedPositionsLabel);
        skinnedNormalResourceResult = importBuffer(instance.skinnedNormalBuffer, s_SkinnedNormalsLabel);
        skinnedTangentResourceResult = importBuffer(instance.skinnedTangentBuffer, s_SkinnedTangentsLabel);
    }
    const auto importBufferIfNeeded = [this](Expected<Core::GpuGraphResourceId>& slot, const Core::BufferHandle& buffer, const AStringView label){
        if(!slot.has_value())
            slot = importBuffer(buffer, label);
    };
    if(updatesMeshletBounds){
        importBufferIfNeeded(skinnedPositionResourceResult, instance.skinnedPositionBuffer, s_SkinnedPositionsLabel);
        meshletDescResourceResult = importBuffer(instance.meshletDescBuffer, s_MeshletDescriptorsLabel);
        meshletPositionRefDeltaResourceResult = importBuffer(instance.meshletPositionRefDeltaBuffer, s_MeshletPositionDeltasLabel);
        meshletLocalVertexRefResourceResult = importBuffer(instance.meshletLocalVertexRefBuffer, s_MeshletLocalVertexRefsLabel);
        meshletPrimitiveIndexResourceResult = importBuffer(instance.meshletPrimitiveIndexBuffer, s_MeshletPrimitiveIndicesLabel);
        meshletBoundsResourceResult = importBuffer(instance.meshletBoundsBuffer, s_MeshletBoundsLabel);
        meshletLocalBoundsResourceResult = importBuffer(instance.meshletLocalBoundsBuffer, s_MeshletLocalBoundsLabel);
        localBoundsResourceResult = importBuffer(instance.localBoundsBuffer, s_LocalBoundsLabel);
    }
    if(hasActiveSkin){
        meshletAttributeRefDeltaResourceResult = importBuffer(instance.meshletAttributeRefDeltaBuffer, s_MeshletAttributeDeltasLabel);
        attributeSkinResourceResult = importBuffer(instance.attributeSkinBuffer, s_AttributeSkinsLabel);
        skinResourceResult = importBuffer(resources.skinBuffer, s_SkinningInfluencesLabel);
        jointPaletteResourceResult = importBuffer(resources.jointPaletteBuffer, s_SkinningJointPaletteLabel);
    }
    if(plan.repacksNormals){
        importBufferIfNeeded(skinnedNormalResourceResult, instance.skinnedNormalBuffer, s_SkinnedNormalsLabel);
        importBufferIfNeeded(meshletDescResourceResult, instance.meshletDescBuffer, s_MeshletDescriptorsLabel);
        importBufferIfNeeded(meshletPrimitiveIndexResourceResult, instance.meshletPrimitiveIndexBuffer, s_MeshletPrimitiveIndicesLabel);
        importBufferIfNeeded(meshletAttributeRefDeltaResourceResult, instance.meshletAttributeRefDeltaBuffer, s_MeshletAttributeDeltasLabel);
        importBufferIfNeeded(meshletLocalVertexRefResourceResult, instance.meshletLocalVertexRefBuffer, s_MeshletLocalVertexRefsLabel);
        attributeResourceResult = importBuffer(instance.attributeBuffer, s_RayTraceAttributesLabel);
    }

    boundsPipelineResult = importComputePipeline(m_system.m_boundsComputePipeline, s_GraphBoundsPipelineName, s_BoundsPipelineLabel);
    localBoundsPipelineResult = importComputePipeline(m_system.m_localBoundsComputePipeline, s_GraphLocalBoundsPipelineName, s_LocalBoundsPipelineLabel);
    if(hasActiveSkin)
        skinningPipelineResult = importComputePipeline(m_system.m_skinningComputePipeline, s_GraphSkinningPipelineName, s_SkinningPipelineLabel);
    if(plan.repacksNormals)
        repackPipelineResult = importComputePipeline(m_system.m_repackComputePipeline, s_GraphRepackPipelineName, s_RepackPipelineLabel);

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

