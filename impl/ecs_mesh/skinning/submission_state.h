// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_mesh/runtime/mesh.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Captures CPU state true only when the producing command list was accepted.
// 4-byte member first, then 1-byte tail to avoid padding.
struct MeshSkinningSubmissionCommit{
    u32 editRevision = 0u;
    RuntimeMeshDirtyFlags handledDirtyFlags = RuntimeMeshDirtyFlag::None;
    bool bindlessResourceSlotsUploadRecorded = false;
    // NOTE: tail kept packed; append new small fields here.

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

