// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_mesh/runtime/mesh.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Captures the CPU state that becomes true only when the command list that produced it was accepted by the GPU queue.
// Keeping this separate from recording ensures a rejected dispatch remains retryable on the next frame.
struct MeshSkinningSubmissionCommit{
    u32 editRevision = 0u;
    RuntimeMeshDirtyFlags handledDirtyFlags = RuntimeMeshDirtyFlag::None;
    bool bindlessResourceSlotsUploadRecorded = false;

    [[nodiscard]] bool empty()const noexcept{
        return handledDirtyFlags == RuntimeMeshDirtyFlag::None && !bindlessResourceSlotsUploadRecorded;
    }
};


inline void ApplyMeshSkinningSubmissionCommit(
    const bool submissionAccepted,
    const u32 currentEditRevision,
    RuntimeMeshDirtyFlags& inOutDirtyFlags,
    bool& inOutBindlessResourceSlotsUploaded,
    const MeshSkinningSubmissionCommit& commit
)noexcept{
    if(!submissionAccepted || currentEditRevision != commit.editRevision)
        return;

    inOutDirtyFlags = static_cast<RuntimeMeshDirtyFlags>(inOutDirtyFlags & ~commit.handledDirtyFlags);
    if(commit.bindlessResourceSlotsUploadRecorded)
        inOutBindlessResourceSlotsUploaded = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

