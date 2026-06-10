// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_csg_types.h"

#include <core/graphics/api.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/ecs_mesh/runtime/mesh.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Mesh;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct InstanceGpuData{
    Float4 rotation = Float4(0.f, 0.f, 0.f, 1.f);
    Float3UInt translation = Float3UInt(0.f, 0.f, 0.f, 0u);
    Float4 scale = Float4(1.f, 1.f, 1.f, 0.f);
};
static_assert(offsetof(InstanceGpuData, rotation) == sizeof(f32) * NWB_MESH_INSTANCE_ROTATION_FLOAT_OFFSET, "InstanceGpuData rotation must be first");
static_assert(offsetof(InstanceGpuData, translation) == sizeof(f32) * NWB_MESH_INSTANCE_TRANSLATION_FLOAT_OFFSET, "InstanceGpuData translation must follow rotation");
static_assert(
    offsetof(InstanceGpuData, translation) + offsetof(Float3UInt, w) == sizeof(f32) * NWB_MESH_INSTANCE_MATERIAL_MUTABLE_BYTE_OFFSET_FLOAT_OFFSET,
    "InstanceGpuData mutable offset must pack into translation.w"
);
static_assert(offsetof(InstanceGpuData, scale) == sizeof(f32) * NWB_MESH_INSTANCE_SCALE_FLOAT_OFFSET, "InstanceGpuData scale must follow translation payload");
static_assert(sizeof(InstanceGpuData) == sizeof(f32) * NWB_MESH_INSTANCE_FLOAT_COUNT, "InstanceGpuData stride must match the mesh shaders");
static_assert(alignof(InstanceGpuData) >= alignof(Float4), "InstanceGpuData must stay SIMD-aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using InstanceGpuDataVector = Vector<InstanceGpuData, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshResources : public RuntimeMeshBuffers{
    Name meshName = NAME_NONE;
    Core::BufferHandle emulationVertexBuffer;
    Core::BindingSetHandle meshBindingSet;
    Core::BindingSetHandle computeBindingSet;
    u32 meshletCount = 0;
    u32 meshletPrimitiveIndexCount = 0;
    bool runtimeMesh = false;
    bool dynamicMeshletBoundsFresh = false;
    bool dynamicMeshletConesFresh = false;
    u64 runtimeMeshVersion = 0u;
    CsgReceiverCpuBounds csgLocalBounds;

    explicit MeshResources(Core::Alloc::GlobalArena& arena)
    {
        static_cast<void>(arena);
    }

    [[nodiscard]] bool valid()const noexcept{
        return
            meshName != NAME_NONE
            && buffersValid()
            && meshletCount > 0
            && meshletPrimitiveIndexCount > 0
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

