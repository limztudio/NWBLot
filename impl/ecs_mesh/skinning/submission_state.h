// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_mesh/runtime/mesh.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Captures CPU state true only when the producing command list was accepted.
struct MeshSkinningSubmissionCommit{
    u32 editRevision = 0u;
    b8 bindlessResourceSlotsUploadRecorded = false;
    RuntimeMeshDirtyFlags handledDirtyFlags = RuntimeMeshDirtyFlag::None;


public:
    [[nodiscard]] b8 empty()const noexcept{
        return handledDirtyFlags == RuntimeMeshDirtyFlag::None && !bindlessResourceSlotsUploadRecorded;
    }
};


inline void ApplyMeshSkinningSubmissionCommit(const b8 submissionAccepted, const u32 currentEditRevision, const MeshSkinningSubmissionCommit& commit, RuntimeMeshDirtyFlags& inOutDirtyFlags, b8& inOutBindlessResourceSlotsUploaded)noexcept{
    if(!submissionAccepted || currentEditRevision != commit.editRevision)
        return;

    inOutDirtyFlags = static_cast<RuntimeMeshDirtyFlags>(inOutDirtyFlags & ~commit.handledDirtyFlags);
    if(commit.bindlessResourceSlotsUploadRecorded)
        inOutBindlessResourceSlotsUploaded = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

