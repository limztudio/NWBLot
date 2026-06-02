// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/alloc/general.h>
#include <core/assets/ref.h>
#include <core/ecs/entity_id.h>
#include <core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Mesh;


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

struct RuntimeMeshCapSourceVertex{
    Float4 position;
    Float4 normal;
    Float4 tangent;
    Float4 uv0;
    Float4 color;
};

struct RuntimeMeshCapSourceTriangle{
    RuntimeMeshCapSourceVertex vertices[3];
};

static_assert(sizeof(RuntimeMeshCapSourceVertex) == sizeof(Float4) * 5u, "RuntimeMeshCapSourceVertex must stay tightly packed");
static_assert(sizeof(RuntimeMeshCapSourceTriangle) == sizeof(RuntimeMeshCapSourceVertex) * 3u, "RuntimeMeshCapSourceTriangle must stay tightly packed");
static_assert(alignof(RuntimeMeshCapSourceVertex) >= alignof(Float4), "RuntimeMeshCapSourceVertex must stay SIMD-aligned");
static_assert(alignof(RuntimeMeshCapSourceTriangle) >= alignof(Float4), "RuntimeMeshCapSourceTriangle must stay SIMD-aligned");
static_assert(IsStandardLayout_V<RuntimeMeshCapSourceVertex>, "RuntimeMeshCapSourceVertex must stay GPU-friendly");
static_assert(IsTriviallyCopyable_V<RuntimeMeshCapSourceVertex>, "RuntimeMeshCapSourceVertex must stay GPU-friendly");
static_assert(IsStandardLayout_V<RuntimeMeshCapSourceTriangle>, "RuntimeMeshCapSourceTriangle must stay GPU-friendly");
static_assert(IsTriviallyCopyable_V<RuntimeMeshCapSourceTriangle>, "RuntimeMeshCapSourceTriangle must stay GPU-friendly");

using RuntimeMeshCapSourceTriangleVector = Vector<RuntimeMeshCapSourceTriangle, Core::Alloc::GlobalArena>;

struct RuntimeMeshDesc : public RuntimeMeshBuffers{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    Name meshKey = NAME_NONE;
    const RuntimeMeshCapSourceTriangle* capSourceTriangles = nullptr;
    u32 meshletCount = 0u;
    usize capSourceTriangleCount = 0u;
    u64 version = 0u;
    bool dynamicMeshletBoundsFresh = false;
    bool dynamicMeshletConesFresh = false;

    [[nodiscard]] bool valid()const noexcept{
        return
            entity.valid()
            && meshKey != NAME_NONE
            && buffersValid()
            && meshletCount > 0u
            && ((capSourceTriangles == nullptr) == (capSourceTriangleCount == 0u))
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

