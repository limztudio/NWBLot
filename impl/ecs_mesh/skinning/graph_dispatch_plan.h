// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_mesh/runtime/mesh.h>
#include <impl/ecs_mesh/skinning/submission_state.adhd.h>
#include <impl/ecs_skeleton/components.adhd.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/task/gpu/types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(MeshSkinningBindlessResourceSlots) == sizeof(u32) * 20u, "MeshSkinning bindless resource slots must stay five uint4 lanes");

// Graph tasks retain immutable dispatch inputs; publish commits only after packet accepts.
struct MeshSkinningGraphDispatchPlan{
    RuntimeMeshHandle handle;
    u64 deformationCandidate = 0u;
    Core::BufferHandle bindlessResourceSlotsBuffer;
    Core::GpuGraphResourceId bindlessResourceSlotsResource;
    Core::GpuGraphResourceId restPositionResource;
    Core::GpuGraphResourceId restNormalResource;
    Core::GpuGraphResourceId restTangentResource;
    Core::GpuGraphResourceId skinnedPositionResource;
    Core::GpuGraphResourceId skinnedNormalResource;
    Core::GpuGraphResourceId skinnedTangentResource;
    Core::GpuGraphResourceId meshletDescResource;
    Core::GpuGraphResourceId meshletBoundsResource;
    Core::GpuGraphResourceId meshletPositionRefDeltaResource;
    Core::GpuGraphResourceId meshletAttributeRefDeltaResource;
    Core::GpuGraphResourceId meshletLocalVertexRefResource;
    Core::GpuGraphResourceId meshletPrimitiveIndexResource;
    Core::GpuGraphResourceId attributeSkinResource;
    Core::GpuGraphResourceId skinResource;
    Core::GpuGraphResourceId jointPaletteResource;
    Core::GpuGraphResourceId attributeResource;
    Core::GpuGraphResourceId meshletLocalBoundsResource;
    Core::GpuGraphResourceId localBoundsResource;
    Core::GpuGraphPipelineId skinningPipeline;
    Core::GpuGraphPipelineId boundsPipeline;
    Core::GpuGraphPipelineId repackPipeline;
    Core::GpuGraphPipelineId localBoundsPipeline;
    u32 meshletCount = 0u;
    u32 skinCount = 0u;
    u32 jointCount = 0u;
    u32 skinningMode = SkeletonSkinningMode::LinearBlend;
    u32 bindlessResourceSlots = 0u;
    Core::GpuDescriptorHandle bindlessResourceSlotsDescriptor = Core::GpuDescriptorHandle::Invalid();
    MeshSkinningSubmissionCommit submissionCommit;
    // Acceptance validates this exact selector generation before setting its residency bit.
    MeshSkinningBindlessResourceSlots bindlessResourceSlotsPayload;
    b8 hasActiveSkin = false;
    b8 copiedRestStreams = false;
    b8 updatesMeshletBounds = false;
    b8 repacksNormals = false;
};
static_assert(sizeof(MeshSkinningGraphDispatchPlan) == s_MeshSkinningGraphDispatchPlanByteSize, "Graph-owned skinning plans should stay compact");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

