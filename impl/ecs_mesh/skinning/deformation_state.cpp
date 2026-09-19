// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformation_state.h"

#include "runtime_instance.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshSkinningResourceBuffers CaptureMeshSkinningResourceBuffers(const MeshSkinningRuntimeInstance& instance){
    return {
        instance.restPositionBuffer,
        instance.restNormalBuffer,
        instance.restTangentBuffer,
        instance.skinnedPositionBuffer,
        instance.skinnedNormalBuffer,
        instance.skinnedTangentBuffer,
        instance.uv0Buffer,
        instance.colorBuffer,
        instance.meshletDescBuffer,
        instance.meshletBoundsBuffer,
        instance.meshletPositionRefDeltaBuffer,
        instance.meshletAttributeRefDeltaBuffer,
        instance.meshletLocalVertexRefBuffer,
        instance.meshletPrimitiveIndexBuffer,
        instance.attributeSkinBuffer,
        instance.triangleIndexBuffer,
        instance.attributeBuffer,
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshSkinningDeformationState::MeshSkinningDeformationState(Core::Alloc::GlobalArena& arena)
    : m_acceptedJoints(arena)
    , m_pendingJoints(arena)
{}

void MeshSkinningDeformationState::invalidateCurrent()noexcept{
    m_current = false;
    m_pendingCandidate = 0u;
}

void MeshSkinningDeformationState::refreshCurrent(
    const MeshSkinningDeformationInputs& inputs,
    const SkeletonJointMatrix* const joints,
    const usize jointCount,
    const bool inputDirty
)noexcept{
    invalidateCurrent();
    if(
        inputDirty
        || m_acceptedRevision == 0u
        || !inputs.resourceSlotsBuffer
        || inputs.resourceSlotsBuffer != m_acceptedInputs.resourceSlotsBuffer
        || inputs.editRevision != m_acceptedInputs.editRevision
        || inputs.skinningMode != m_acceptedInputs.skinningMode
        || jointCount != m_acceptedJoints.size()
        || (jointCount != 0u && !joints)
    )
        return;
    m_current = jointCount == 0u || NWB_MEMCMP(joints, m_acceptedJoints.data(), jointCount * sizeof(SkeletonJointMatrix)) == 0;
}

u64 MeshSkinningDeformationState::stage(
    const MeshSkinningDeformationInputs& inputs,
    const SkeletonJointMatrix* const joints,
    const usize jointCount
){
    invalidateCurrent();
    if(
        !inputs.resourceSlotsBuffer
        || (jointCount != 0u && !joints)
        || jointCount > Limit<usize>::s_Max / sizeof(SkeletonJointMatrix)
        || m_candidateSerial == Limit<u64>::s_Max
        || m_acceptedRevision == Limit<u64>::s_Max
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningDeformationState: invalid or exhausted deformation candidate"));
        return 0u;
    }
    if(jointCount == 0u)
        m_pendingJoints.clear();
    else
        m_pendingJoints.assign(joints, joints + jointCount);
    m_pendingInputs = inputs;
    m_pendingCandidate = ++m_candidateSerial;
    return m_pendingCandidate;
}

bool MeshSkinningDeformationState::accept(const u64 candidate)noexcept{
    if(candidate == 0u || candidate != m_pendingCandidate)
        return false;
    m_acceptedJoints.swap(m_pendingJoints);
    m_acceptedInputs = m_pendingInputs;
    ++m_acceptedRevision;
    m_pendingCandidate = 0u;
    m_current = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

