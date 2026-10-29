// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_mesh/runtime/mesh.h>
#include <impl/ecs_mesh/skinning/submission_state.adhd.h>
#include <impl/ecs_skeleton/components.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshSkinningRuntimeInstance;
using MeshSkinningResourceBuffers = Array<Core::BufferHandle, s_MeshSkinningResourceBufferCount>;

// Retains exact buffer identities so resource replacement cannot reuse old descriptor bindings.
[[nodiscard]] MeshSkinningResourceBuffers CaptureMeshSkinningResourceBuffers(const MeshSkinningRuntimeInstance& instance)noexcept;

struct MeshSkinningDeformationInputs{
    Core::BufferHandle resourceSlotsBuffer;
    u32 editRevision = 0u;
    u32 skinningMode = SkeletonSkinningMode::LinearBlend;
};

// Accepted object-space output is reusable only for the same resolved GPU palette and resource generation.
class MeshSkinningDeformationState final{
public:
    explicit MeshSkinningDeformationState(Core::Alloc::GlobalArena& arena)noexcept;


public:
    void invalidateCurrent()noexcept;
    void refreshCurrent(const MeshSkinningDeformationInputs& inputs, const SkeletonJointMatrix* joints, usize jointCount, b8 inputDirty)noexcept;
    [[nodiscard]] Expected<u64> stage(const MeshSkinningDeformationInputs& inputs, const SkeletonJointMatrix* joints, usize jointCount);
    [[nodiscard]] b8 accept(u64 candidate)noexcept;
    // Zero also covers a changed pose awaiting this frame's graph submission.
    [[nodiscard]] u64 contentRevision()const noexcept{ return m_current ? m_acceptedRevision : 0u; }


private:
    Vector<SkeletonJointMatrix, Core::Alloc::GlobalArena> m_acceptedJoints;
    Vector<SkeletonJointMatrix, Core::Alloc::GlobalArena> m_pendingJoints;
    MeshSkinningDeformationInputs m_acceptedInputs;
    MeshSkinningDeformationInputs m_pendingInputs;
    u64 m_acceptedRevision = 0u;
    u64 m_candidateSerial = 0u;
    u64 m_pendingCandidate = 0u;
    b8 m_current = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

