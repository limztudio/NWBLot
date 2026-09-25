// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/pipeline.h>
#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshResources;
struct RuntimeMeshBuffers;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Core::ResourceStates::Mask s_ObjectGeometryRasterState = Core::ResourceStates::VertexBuffer | Core::ResourceStates::IndexBuffer;

struct ObjectGeometryCacheLayout{
    u64 bufferByteSize = 0u;
    u32 indexByteOffset = 0u;
    u32 indexCount = 0u;
};

struct ObjectGeometryCacheState{
    Core::BufferHandle buffer;
    Core::ComputePipelineHandle decoderPipeline;
    u64 acceptedContentRevision = 0u;
    Core::GpuDescriptorHandle heapHandle = Core::GpuDescriptorHandle::invalid();
    u32 indexByteOffset = 0u;
    u32 indexCount = 0u;
    bool acceptedContent = false;
    bool initialized = false;
};

// The declaration retains the buffer and current content generation; acceptance validates them against the mesh owner.
struct ObjectGeometryCacheSnapshot{
    Core::BufferHandle buffer;
    Core::ComputePipelineHandle decoderPipeline;
    u64 sourceRevision = 0u;
    Core::GpuDescriptorHandle heapHandle = Core::GpuDescriptorHandle::invalid();
    u32 indexByteOffset = 0u;
    u32 indexCount = 0u;
    bool initialized = false;
    bool requiresDecode = true;

    [[nodiscard]] bool valid()const noexcept;
};

[[nodiscard]] bool ResolveObjectGeometryCacheLayout(
    u64 localVertexRefByteSize,
    u32 primitiveIndexCount,
    ObjectGeometryCacheLayout& outLayout)noexcept;
[[nodiscard]] bool AcceptObjectGeometryCacheWrite(
    MeshResources& mesh,
    const RuntimeMeshBuffers& sourceBuffers,
    const ObjectGeometryCacheSnapshot& expected,
    bool runtimeMesh)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

