// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_instance.h"

#include <impl/assets_mesh/meshlet_triangle_indices.h>
#include <impl/assets_mesh/meshlet_vertex_attributes.h>
#include <impl/ecs_mesh/runtime/types.adhd.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshSkinningTraceGeometry final{
public:
    [[nodiscard]] static Expected<Vector<u32, Core::Alloc::ScratchArena>> BuildIndices(const MeshSkinningRuntimeInstance& instance, Core::Alloc::ScratchArena& scratchArena){
        const usize indexCount = instance.meshletPrimitiveIndices.size();
        auto triangleIndices = BuildMeshletTriangleIndices(instance.meshlets, instance.meshletLocalVertexRefs, instance.meshletPositionRefDeltas, instance.meshletPrimitiveIndices, instance.restPositions.size(), scratchArena);
        if(!triangleIndices){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to reconstruct ray tracing triangle indices for runtime mesh '{}'"), instance.handle.value);
            return MakeUnexpected(Failure{});
        }
        if(!ValidateReconstructedRuntimeTriangleIndexCount(triangleIndices->size(), indexCount, instance.handle.value))
            return MakeUnexpected(Failure{});
        return Move(*triangleIndices);
    }
    [[nodiscard]] static Expected<Vector<AttribGpu, Core::Alloc::ScratchArena>> BuildAttributes(const MeshSkinningRuntimeInstance& instance, Core::Alloc::ScratchArena& scratchArena){
        auto triangleAttributes = BuildMeshletTriangleAttributes(instance.meshlets, instance.meshletLocalVertexRefs, instance.meshletAttributeRefDeltas, instance.meshletPrimitiveIndices, instance.restNormals, instance.uv0, scratchArena);
        if(!triangleAttributes){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to reconstruct shadow trace triangle attributes for runtime mesh '{}'"), instance.handle.value);
            return MakeUnexpected(Failure{});
        }
        return Move(*triangleAttributes);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

