// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deformation_state.h"

#include "runtime_instance.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MeshSkinningResourceBuffers CaptureMeshSkinningResourceBuffers(const MeshSkinningRuntimeInstance& instance)noexcept{
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
        instance.meshletLocalBoundsBuffer,
        instance.localBoundsBuffer,
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MeshSkinningDeformationState::invalidateCurrent()noexcept{
    m_current = false;
    m_pendingCandidate = 0u;
}

void MeshSkinningDeformationState::refreshCurrent(const MeshSkinningDeformationInputs& inputs, const Span<const SkeletonJointMatrix> joints, const b8 inputDirty)noexcept{
    invalidateCurrent();
    const usize jointCount = joints.size();
    if(
        inputDirty
        || m_acceptedRevision == 0u
        || !inputs.resourceSlotsBuffer
        || inputs.resourceSlotsBuffer != m_acceptedInputs.resourceSlotsBuffer
        || inputs.editRevision != m_acceptedInputs.editRevision
        || inputs.skinningMode != m_acceptedInputs.skinningMode
        || jointCount != m_acceptedJoints.size()
        || (jointCount != 0u && !joints.data())
    )
        return;
    m_current = jointCount == 0u || NWB_MEMCMP(joints.data(), m_acceptedJoints.data(), jointCount * sizeof(SkeletonJointMatrix)) == 0;
}

Expected<u64> MeshSkinningDeformationState::stage(const MeshSkinningDeformationInputs& inputs, const Span<const SkeletonJointMatrix> joints){
    invalidateCurrent();
    const usize jointCount = joints.size();
    if(!inputs.resourceSlotsBuffer || (jointCount != 0u && !joints.data()) || jointCount > Limit<usize>::s_Max / sizeof(SkeletonJointMatrix) || m_candidateSerial == Limit<u64>::s_Max || m_acceptedRevision == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningDeformationState: invalid or exhausted deformation candidate"));
        return MakeUnexpected(Failure{});
    }
    if(jointCount == 0u)
        m_pendingJoints.clear();
    else
        m_pendingJoints.assign(joints.begin(), joints.end());
    m_pendingInputs = inputs;
    m_pendingCandidate = ++m_candidateSerial;
    return m_pendingCandidate;
}

b8 MeshSkinningDeformationState::accept(const u64 candidate)noexcept{
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

