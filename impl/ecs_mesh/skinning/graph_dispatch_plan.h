// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "submission_state.h"

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/task_graph/types.h>
#include <impl/ecs_mesh/runtime/mesh.h>
#include <impl/ecs_skeleton/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Mirrors NwbSkinnedMeshBindlessResources exactly: four std140 uint4 lanes. Every handle below is a persistent
// StorageBuffer-heap registration. The selector payload itself is a UniformBuffer heap entry.
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

    [[nodiscard]] constexpr bool operator==(const MeshSkinningBindlessResourceSlots&)const = default;
};
static_assert(sizeof(MeshSkinningBindlessResourceSlots) == sizeof(u32) * 16u, "MeshSkinning bindless resource slots must stay four uint4 lanes");


// The graph tasks retain only immutable per-mesh dispatch inputs. They resolve imported buffers and pipelines
// from graph-owned IDs while recording, then publish the dirty-state and selector-residency commit only after
// the containing primary-Graphics packet is accepted.
struct MeshSkinningGraphDispatchPlan{
    RuntimeMeshHandle handle;
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
    Core::GpuGraphPipelineId skinningPipeline;
    Core::GpuGraphPipelineId boundsPipeline;
    Core::GpuGraphPipelineId repackPipeline;
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
static_assert(sizeof(MeshSkinningGraphDispatchPlan) == 448u, "Graph-owned skinning plans should stay compact");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

