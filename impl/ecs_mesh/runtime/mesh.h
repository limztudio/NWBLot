// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../../global.h"

#include <core/alloc/general.h>
#include <core/assets/ref.h>
#include <core/ecs/entity_id.h>
#include <core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Mesh;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RuntimeMeshDirtyFlag{
    enum Enum : u8{
        None = 0,
        TopologyDirty = 1u << 0u,
        AttributesDirty = 1u << 1u,
        SkinningInputDirty = 1u << 2u,
        GpuUploadDirty = 1u << 3u,
        MeshletBoundsDirty = 1u << 4u,
        All = TopologyDirty | AttributesDirty | SkinningInputDirty | GpuUploadDirty | MeshletBoundsDirty,
    };
};
using RuntimeMeshDirtyFlags = u8;

struct RuntimeMeshHandle{
    u64 value = 0;

    [[nodiscard]] bool valid()const{ return value != 0u; }
    [[nodiscard]] explicit operator bool()const{ return valid(); }
    void reset(){ value = 0; }
};

[[nodiscard]] NWB_INLINE bool operator==(const RuntimeMeshHandle& lhs, const RuntimeMeshHandle& rhs){
    return lhs.value == rhs.value;
}
[[nodiscard]] NWB_INLINE bool operator!=(const RuntimeMeshHandle& lhs, const RuntimeMeshHandle& rhs){
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RuntimeMeshBuffers{
    Core::BufferHandle positionBuffer;
    Core::BufferHandle normalBuffer;
    Core::BufferHandle tangentBuffer;
    Core::BufferHandle uv0Buffer;
    Core::BufferHandle colorBuffer;
    Core::BufferHandle meshletDescBuffer;
    Core::BufferHandle meshletBoundsBuffer;
    Core::BufferHandle meshletPositionRefDeltaBuffer;
    Core::BufferHandle meshletAttributeRefDeltaBuffer;
    Core::BufferHandle meshletLocalVertexRefBuffer;
    Core::BufferHandle meshletPrimitiveIndexBuffer;

    [[nodiscard]] bool buffersValid()const noexcept{
        return
            positionBuffer != nullptr
            && normalBuffer != nullptr
            && tangentBuffer != nullptr
            && uv0Buffer != nullptr
            && colorBuffer != nullptr
            && meshletDescBuffer != nullptr
            && meshletBoundsBuffer != nullptr
            && meshletPositionRefDeltaBuffer != nullptr
            && meshletAttributeRefDeltaBuffer != nullptr
            && meshletLocalVertexRefBuffer != nullptr
            && meshletPrimitiveIndexBuffer != nullptr
        ;
    }
};

inline constexpr i32 s_RuntimeMeshBoundsValidFlag = 1 << 0;

struct RuntimeMeshLocalBounds{
    Float3Int minBounds = Float3Int(0.f, 0.f, 0.f, 0);
    Float3Int maxBounds = Float3Int(0.f, 0.f, 0.f, 0);

    [[nodiscard]] bool valid()const noexcept{ return (minBounds.w & s_RuntimeMeshBoundsValidFlag) != 0; }
};

static_assert(alignof(RuntimeMeshLocalBounds) >= alignof(Float4), "RuntimeMeshLocalBounds must stay SIMD-friendly");
static_assert(IsStandardLayout_V<RuntimeMeshLocalBounds>, "RuntimeMeshLocalBounds must stay layout-stable");
static_assert(IsTriviallyCopyable_V<RuntimeMeshLocalBounds>, "RuntimeMeshLocalBounds must stay cheap to pass by value");

struct RuntimeMeshDesc : public RuntimeMeshBuffers{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    u32 meshletCount = 0u;
    Name meshKey = NAME_NONE;
    Core::BufferHandle triangleIndexBuffer;   // RT-only; null when ray tracing is unsupported
    Core::BufferHandle attributeBuffer;       // RT-only flat per-triangle-corner trace attributes; null when ray tracing is unsupported
    bool dynamicMeshletBoundsFresh = false;
    bool dynamicMeshletConesFresh = false;
    RuntimeMeshLocalBounds localBounds;
    u64 version = 0u;

    [[nodiscard]] bool valid()const noexcept{
        return
            entity.valid()
            && meshKey != NAME_NONE
            && buffersValid()
            && meshletCount > 0u
            && localBounds.valid()
        ;
    }
};

class IRuntimeMeshProvider{
public:
    virtual ~IRuntimeMeshProvider() = default;

public:
    [[nodiscard]] virtual bool resolveRuntimeMesh(Core::ECS::EntityID entity, RuntimeMeshDesc& outMesh) = 0;
    [[nodiscard]] virtual bool containsRuntimeMesh(const Name& meshKey, u64 version) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RenderableMeshDesc{
    Core::Assets::AssetRef<Mesh> mesh;
    RuntimeMeshDesc runtimeMesh;
    bool runtime = false;

    [[nodiscard]] bool valid()const noexcept{
        return runtime
            ? runtimeMesh.valid()
            : mesh.valid()
        ;
    }
};

class IRuntimeMeshRegistry{
public:
    virtual ~IRuntimeMeshRegistry() = default;

public:
    virtual void registerRuntimeMeshProvider(IRuntimeMeshProvider& provider) = 0;
    virtual void unregisterRuntimeMeshProvider(IRuntimeMeshProvider& provider) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

