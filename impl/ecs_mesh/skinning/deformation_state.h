// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_mesh/runtime/mesh.h>
#include <impl/ecs_skeleton/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshSkinningRuntimeInstance;
inline constexpr usize s_MeshSkinningResourceBufferCount = 19u;
using MeshSkinningResourceBuffers = Array<Core::BufferHandle, s_MeshSkinningResourceBufferCount>;

// Retains exact buffer identities so resource replacement cannot reuse old descriptor bindings.
[[nodiscard]] MeshSkinningResourceBuffers CaptureMeshSkinningResourceBuffers(const MeshSkinningRuntimeInstance& instance);

struct MeshSkinningDeformationInputs{
    Core::BufferHandle resourceSlotsBuffer;
    u32 editRevision = 0u;
    u32 skinningMode = SkeletonSkinningMode::LinearBlend;
};

// Accepted object-space output is reusable only for the same resolved GPU palette and resource generation.
class MeshSkinningDeformationState final{
public:
    explicit MeshSkinningDeformationState(Core::Alloc::GlobalArena& arena);


public:
    void invalidateCurrent()noexcept;
    void refreshCurrent(
        const MeshSkinningDeformationInputs& inputs,
        const SkeletonJointMatrix* joints,
        usize jointCount,
        bool inputDirty
    )noexcept;
    [[nodiscard]] u64 stage(
        const MeshSkinningDeformationInputs& inputs,
        const SkeletonJointMatrix* joints,
        usize jointCount
    );
    [[nodiscard]] bool accept(u64 candidate)noexcept;
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
    bool m_current = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

