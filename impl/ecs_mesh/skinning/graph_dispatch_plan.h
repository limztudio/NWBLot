// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "submission_state.h"

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/task/gpu/types.h>
#include <impl/ecs_mesh/runtime/mesh.h>
#include <impl/ecs_skeleton/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Mirrors bindless resources: five uint4 lanes; handles are StorageBuffer registrations.
struct MeshSkinningBindlessResourceSlots{
    u32 restPosition = 0u;
    u32 skinnedPosition = 0u;
    u32 restNormal = 0u;
    u32 skinnedNormal = 0u;

    u32 restTangent = 0u;
    u32 skinnedTangent = 0u;
    u32 meshletDesc = 0u;
    u32 positionRefDeltas = 0u;

    u32 attributeRefDeltas = 0u;
    u32 attributeSkins = 0u;
    u32 skinInfluences = 0u;
    u32 jointPalette = 0u;

    u32 localVertexRefs = 0u;
    u32 primitiveIndices = 0u;
    u32 meshletBounds = 0u;
    u32 attributeBuffer = 0u;

    u32 meshletLocalBounds = 0u;
    u32 localBounds = 0u;
    u32 reserved0 = 0u;
    u32 reserved1 = 0u;

    [[nodiscard]] constexpr bool operator==(const MeshSkinningBindlessResourceSlots&)const = default;
};
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
    u32 attributeCount = 0u;
    u32 bindlessResourceSlots = 0u;
    Core::GpuDescriptorHandle bindlessResourceSlotsDescriptor = Core::GpuDescriptorHandle::invalid();
    MeshSkinningSubmissionCommit submissionCommit;
    // Acceptance validates this exact selector generation before setting its residency bit.
    MeshSkinningBindlessResourceSlots bindlessResourceSlotsPayload;
    bool hasActiveSkin = false;
    bool copiedRestStreams = false;
    bool updatesMeshletBounds = false;
    bool repacksNormals = false;
};
static constexpr usize s_MeshSkinningGraphDispatchPlanByteSize = 520u;
static_assert(sizeof(MeshSkinningGraphDispatchPlan) == s_MeshSkinningGraphDispatchPlanByteSize, "Graph-owned skinning plans should stay compact");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

