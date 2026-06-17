// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "arena_names.h"
#include "runtime_cache.h"
#include "skin_payload.h"
#include "timing_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/module.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BufferPayloadBytes(const usize count, const usize stride, usize& outBytes, const tchar* label){
    if(RuntimeMeshBufferUpload::PayloadByteCount(count, stride, outBytes))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: {} payload byte size overflows"), label);
    return false;
}

static bool ResolveRestBufferBytes(
    const MeshSkinningRuntimeInstance& instance,
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

struct RuntimeSkinPayloadScratch{
    Vector<MeshSkinningInfluenceGpu, Core::Alloc::ScratchArena> skinInfluences;
    Vector<SkeletonJointMatrix, Core::Alloc::ScratchArena> jointMatrices;
    Vector<SkeletonJointMatrix, Core::Alloc::ScratchArena> poseJoints;
    u32 resolvedSkinningMode = SkeletonSkinningMode::LinearBlend;

    explicit RuntimeSkinPayloadScratch(Core::Alloc::ScratchArena& scratchArena)
        : skinInfluences(scratchArena)
        , jointMatrices(scratchArena)
        , poseJoints(scratchArena)
    {}

    [[nodiscard]] bool hasActiveSkin()const{
        return !skinInfluences.empty() && !jointMatrices.empty();
    }
};

static bool BuildRuntimeSkinPayload(
    MeshSkinningRuntimeInstance& instance,
    const SkeletonJointPaletteComponent* jointPalette,
    const SkeletonPoseComponent* skeletonPose,
    RuntimeSkinPayloadScratch& payload
){
    payload.resolvedSkinningMode = jointPalette ? jointPalette->skinningMode : SkeletonSkinningMode::LinearBlend;
    if(SkeletonRuntime::HasSkeletonPose(skeletonPose)){
        if(!SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(*skeletonPose, payload.poseJoints, payload.resolvedSkinningMode)){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' skeleton pose is invalid"), instance.handle.value);
            return false;
        }
        return MeshSkinningPayload::BuildSkinPayloadFromJointMatrices(
            instance,
            payload.poseJoints,
            payload.resolvedSkinningMode,
            payload.skinInfluences,
            payload.jointMatrices
        );
    }

    return MeshSkinningPayload::BuildSkinPayload(
        instance,
        jointPalette,
        payload.skinInfluences,
        payload.jointMatrices
    );
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
    __hidden_skinning::RuntimeSkinPayloadScratch payload{ scratchArena };
    if(!__hidden_skinning::BuildRuntimeSkinPayload(instance, jointPalette, skeletonPose, payload))
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

    RuntimeResources* resources = nullptr;
    bool resourcesRebuilt = false;
    return ensureRuntimeResources(
        instance,
        payloadViews,
        resources,
        resourcesRebuilt
    );
}

bool MeshSkinningSystem::dispatchRuntimeMesh(
    Core::CommandList& commandList,
    MeshSkinningRuntimeInstance& instance,
    const SkeletonJointPaletteComponent* jointPalette,
    const SkeletonPoseComponent* skeletonPose
){
    Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_DispatchRuntimeArena);
    __hidden_skinning::RuntimeSkinPayloadScratch payload{ scratchArena };
    if(!__hidden_skinning::BuildRuntimeSkinPayload(instance, jointPalette, skeletonPose, payload))
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
    NWB_ASSERT(resources->boundsBindingSet);
    NWB_ASSERT(!hasActiveSkin || resources->skinningBindingSet);
    NWB_ASSERT(!hasActiveSkin || resources->skinBuffer);
    NWB_ASSERT(!hasActiveSkin || resources->jointPaletteBuffer);

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
            instance.dirtyFlags & ~(RuntimeMeshDirtyFlag::SkinningInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)
        );
        return true;
    }

    usize jointPaletteBytes = 0;
    if(!__hidden_skinning::BufferPayloadBytes(
        payload.jointMatrices.size(),
        sizeof(SkeletonJointMatrix),
        jointPaletteBytes,
        NWB_TEXT("joint palette")
    ))
        return false;

    commandList.setBufferState(resources->jointPaletteBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(resources->jointPaletteBuffer.get(), payload.jointMatrices.data(), jointPaletteBytes);

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

    MeshSkinningPushConstants pushConstants;
    pushConstants.meshletCount = static_cast<u32>(instance.meshlets.size());
    pushConstants.skinCount = static_cast<u32>(payload.skinInfluences.size());
    pushConstants.jointCount = static_cast<u32>(payload.jointMatrices.size());
    pushConstants.skinningMode = payload.resolvedSkinningMode;
    pushConstants.attributeCount = instance.meshletAttributeRefCount;
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

    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags | RuntimeMeshDirtyFlag::MeshletBoundsDirty
    );
    if(!dispatchMeshletBounds(commandList, instance, *resources))
        return false;

    instance.dirtyFlags = static_cast<RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~(RuntimeMeshDirtyFlag::SkinningInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)
    );
    return true;
}

bool MeshSkinningSystem::copyRestToSkinned(Core::CommandList& commandList, MeshSkinningRuntimeInstance& instance){
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

bool MeshSkinningSystem::dispatchMeshletBounds(
    Core::CommandList& commandList,
    MeshSkinningRuntimeInstance& instance,
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
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), MeshSkinningGpuTimingScope::s_MeshletBounds, m_graphics.getDevice(), commandList);

        commandList.dispatch(pushConstants.meshletCount, 1, 1);
    }

    commandList.setBufferState(instance.meshletBoundsBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

