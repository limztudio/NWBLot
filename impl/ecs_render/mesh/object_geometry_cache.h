// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/mesh/instance_gpu_data.adhd.h>

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

using NWB::Impl::ObjectGeometryCacheLayout;

struct ObjectGeometryCacheState{
    Core::BufferHandle buffer;
    Core::ComputePipelineHandle decoderPipeline;
    u64 acceptedContentRevision = 0u;
    Core::GpuDescriptorHandle heapHandle = Core::GpuDescriptorHandle::Invalid();
    u32 indexByteOffset = 0u;
    u32 indexCount = 0u;
    b8 acceptedContent = false;
    b8 initialized = false;
};

// The declaration retains the buffer and current content generation; acceptance validates them against the mesh owner.
struct ObjectGeometryCacheSnapshot{
    Core::BufferHandle buffer;
    Core::ComputePipelineHandle decoderPipeline;
    u64 sourceRevision = 0u;
    Core::GpuDescriptorHandle heapHandle = Core::GpuDescriptorHandle::Invalid();
    u32 indexByteOffset = 0u;
    u32 indexCount = 0u;
    b8 initialized = false;
    b8 requiresDecode = true;


public:
    [[nodiscard]] b8 valid()const noexcept;
};

[[nodiscard]] inline Expected<ObjectGeometryCacheLayout> ResolveObjectGeometryCacheLayout(const u64 localVertexRefByteSize, const u32 primitiveIndexCount)noexcept{
    ObjectGeometryCacheLayout layout{};
    if(!TryResolveObjectGeometryCacheLayout(localVertexRefByteSize, primitiveIndexCount, layout))
        return MakeUnexpected(Failure{});
    return layout;
}

[[nodiscard]] b8 AcceptObjectGeometryCacheWrite(MeshResources& mesh, const RuntimeMeshBuffers& sourceBuffers, const ObjectGeometryCacheSnapshot& expected, b8 runtimeMesh)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

