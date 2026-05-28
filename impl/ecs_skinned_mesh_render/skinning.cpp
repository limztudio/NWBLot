// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "runtime_cache.h"
#include "skin_payload.h"
#include "timing_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/module.h>
#include <impl/ecs_mesh_runtime/buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BufferPayloadBytes(const usize count, const usize stride, usize& outBytes, const tchar* label){
    if(RuntimeMeshBufferUpload::PayloadByteCount(count, stride, outBytes))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: {} payload byte size overflows"), label);
    return false;
}

static bool ResolveRestBufferBytes(
    const SkinnedMeshRuntimeMeshInstance& instance,
    usize& outPositionBytes,
    usize& outNormalBytes,
    usize& outTangentBytes
){
    outPositionBytes = 0u;
    outNormalBytes = 0u;
    outTangentBytes = 0u;
    return
        BufferPayloadBytes(
            instance.restPositions.size(),
            sizeof(Float3U),
            outPositionBytes,
            NWB_TEXT("rest position")
        )
        && BufferPayloadBytes(
            instance.restNormals.size(),
            sizeof(Half4U),
            outNormalBytes,
            NWB_TEXT("rest normal")
        )
        && BufferPayloadBytes(
            instance.restTangents.size(),
            sizeof(Half4U),
            outTangentBytes,
            NWB_TEXT("rest tangent")
        )
    ;
}

static void SetRestBufferStates(
    Core::ICommandList& commandList,
    SkinnedMeshRuntimeMeshInstance& instance,
    const Core::ResourceStates::Mask state
){
    commandList.setBufferState(instance.restPositionBuffer.get(), state);
    commandList.setBufferState(instance.restNormalBuffer.get(), state);
    commandList.setBufferState(instance.restTangentBuffer.get(), state);
}

static void SetSkinnedBufferStates(
    Core::ICommandList& commandList,
    SkinnedMeshRuntimeMeshInstance& instance,
    const Core::ResourceStates::Mask state
){
    commandList.setBufferState(instance.skinnedPositionBuffer.get(), state);
    commandList.setBufferState(instance.skinnedNormalBuffer.get(), state);
    commandList.setBufferState(instance.skinnedTangentBuffer.get(), state);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedMeshSystem::dispatchRuntimeMesh(
    Core::ICommandList& commandList,
    SkinnedMeshRuntimeMeshInstance& instance,
    const SkinnedMeshJointPaletteComponent* jointPalette,
    const SkinnedMeshSkeletonPoseComponent* skeletonPose
){
    Core::Alloc::ScratchArena scratchArena;
    Vector<SkinnedMeshSkinInfluenceGpu, Core::Alloc::ScratchArena> skinInfluences{ scratchArena };
    Vector<SkinnedMeshJointMatrix, Core::Alloc::ScratchArena> jointMatrices{ scratchArena };
    u32 resolvedSkinningMode = jointPalette ? jointPalette->skinningMode : SkinnedMeshSkinningMode::LinearBlend;
    if(SkinnedMeshRuntime::HasSkeletonPose(skeletonPose)){
        Vector<SkinnedMeshJointMatrix, Core::Alloc::ScratchArena> poseJoints{ scratchArena };
        if(!SkinnedMeshRuntime::BuildJointPaletteFromSkeletonPose(*skeletonPose, poseJoints, resolvedSkinningMode)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: runtime mesh '{}' skeleton pose is invalid"), instance.handle.value);
            return false;
        }
        if(!SkinnedMeshSkinPayload::BuildSkinPayloadFromJointMatrices(
            instance,
            poseJoints,
            resolvedSkinningMode,
            skinInfluences,
            jointMatrices
        ))
            return false;
    }
    else{
        if(!SkinnedMeshSkinPayload::BuildSkinPayload(instance, jointPalette, skinInfluences, jointMatrices))
            return false;
    }

    const bool hasActiveSkin = !skinInfluences.empty() && !jointMatrices.empty();
    RuntimePayloadViews payloadViews;
    if(hasActiveSkin){
        payloadViews.skinInfluences = skinInfluences.data();
        payloadViews.jointPalette = jointMatrices.data();
        payloadViews.skinInfluenceCount = skinInfluences.size();
        payloadViews.jointPaletteCount = jointMatrices.size();
    }

    const bool skinnedMeshInputDirty = (instance.dirtyFlags & RuntimeMeshDirtyFlag::SkinnedMeshInputDirty) != 0u;
    const bool meshletBoundsDirty = (instance.dirtyFlags & RuntimeMeshDirtyFlag::MeshletBoundsDirty) != 0u;
    const auto foundRuntimeResources = m_runtimeResources.find(instance.handle.value);
    const bool hadSkinningResources = foundRuntimeResources != m_runtimeResources.end() && foundRuntimeResources.value().usesSkinning();
    if(!hasActiveSkin && !skinnedMeshInputDirty && !meshletBoundsDirty && !hadSkinningResources)
        return false;
    if(instance.meshlets.empty())
        return false;

    if(hasActiveSkin && !ensureSkinningPipeline())
        return false;
    if(!ensureBoundsPipeline())
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
#if defined(NWB_DEBUG)
    if(
        !resources
        || !resources->boundsBindingSet
    )
        return false;
    if(
        hasActiveSkin
        && (
            !resources->skinningBindingSet
            || !resources->skinBuffer
            || !resources->jointPaletteBuffer
        )
    )
        return false;
#endif

    if(!hasActiveSkin){
        if(skinnedMeshInputDirty || hadSkinningResources){
            if(!copyRestToSkinned(commandList, instance))
                return false;
            instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
                instance.dirtyFlags | RuntimeMeshDirtyFlag::MeshletBoundsDirty
            );
        }

        if((instance.dirtyFlags & RuntimeMeshDirtyFlag::MeshletBoundsDirty) != 0u){
            if(!dispatchMeshletBounds(commandList, instance, *resources))
                return false;
        }

        instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
            instance.dirtyFlags & ~(RuntimeMeshDirtyFlag::SkinnedMeshInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)
        );
        return true;
    }

    usize jointPaletteBytes = 0;
    if(!resourcesRebuilt){
        if(!__hidden_skinning::BufferPayloadBytes(
            jointMatrices.size(),
            sizeof(SkinnedMeshJointMatrix),
            jointPaletteBytes,
            NWB_TEXT("joint palette")
        ))
            return false;

        commandList.setBufferState(resources->jointPaletteBuffer.get(), Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.writeBuffer(resources->jointPaletteBuffer.get(), jointMatrices.data(), jointPaletteBytes);
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
    computeState.addBindingSet(resources->skinningBindingSet.get());
    commandList.setComputeState(computeState);

    SkinnedMeshPushConstants pushConstants;
    pushConstants.meshletCount = static_cast<u32>(instance.meshlets.size());
    pushConstants.skinCount = static_cast<u32>(skinInfluences.size());
    pushConstants.jointCount = static_cast<u32>(jointMatrices.size());
    pushConstants.skinningMode = resolvedSkinningMode;
    pushConstants.attributeCount = instance.meshletAttributeRefCount;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), SkinnedMeshGpuTimingScope::Skinning(), m_graphics.getDevice(), commandList);

        commandList.dispatch(pushConstants.meshletCount, 1, 1);
    }

    __hidden_skinning::SetSkinnedBufferStates(
        commandList,
        instance,
        Core::ResourceStates::ShaderResource
    );
    commandList.commitBarriers();

    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags | RuntimeMeshDirtyFlag::MeshletBoundsDirty
    );
    if(!dispatchMeshletBounds(commandList, instance, *resources))
        return false;

    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~(RuntimeMeshDirtyFlag::SkinnedMeshInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)
    );
    return true;
}

bool SkinnedMeshSystem::copyRestToSkinned(Core::ICommandList& commandList, SkinnedMeshRuntimeMeshInstance& instance){
    usize positionBytes = 0;
    usize normalBytes = 0;
    usize tangentBytes = 0;
    if(!__hidden_skinning::ResolveRestBufferBytes(
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

bool SkinnedMeshSystem::dispatchMeshletBounds(
    Core::ICommandList& commandList,
    SkinnedMeshRuntimeMeshInstance& instance,
    const RuntimeResources& resources
){
    commandList.setBufferState(instance.skinnedPositionBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletDescBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletPositionRefDeltaBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletLocalVertexRefBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletPrimitiveIndexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instance.meshletBoundsBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_boundsComputePipeline.get());
    computeState.addBindingSet(resources.boundsBindingSet.get());
    commandList.setComputeState(computeState);

    MeshletBoundsPushConstants pushConstants;
    pushConstants.meshletCount = static_cast<u32>(instance.meshlets.size());
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), SkinnedMeshGpuTimingScope::MeshletBounds(), m_graphics.getDevice(), commandList);

        commandList.dispatch(pushConstants.meshletCount, 1, 1);
    }

    commandList.setBufferState(instance.meshletBoundsBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

