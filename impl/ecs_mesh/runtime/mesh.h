// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_mesh/runtime/handle.adhd.h>
#include <impl/ecs_mesh/runtime/types.adhd.h>

#include <impl/assets_mesh/payload_types.adhd.h>

#include <core/alloc/general.h>
#include <core/assets/ref.h>
#include <core/ecs/entity_id.h>
#include <core/graphics/api.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Mesh;
class RuntimeMeshRequestSet;


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


public:
    [[nodiscard]] b8 buffersValid()const noexcept{
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

static_assert(alignof(RuntimeMeshLocalBounds) >= alignof(Float4), "RuntimeMeshLocalBounds must stay SIMD-friendly");
static_assert(IsStandardLayout_V<RuntimeMeshLocalBounds>, "RuntimeMeshLocalBounds must stay layout-stable");
static_assert(IsTriviallyCopyable_V<RuntimeMeshLocalBounds>, "RuntimeMeshLocalBounds must stay cheap to pass by value");

struct RuntimeMeshDesc : public RuntimeMeshBuffers{
    RuntimeMeshLocalBounds localBounds;
    // Optional 32-byte runtime_bounds_constants.h GPU record for this accepted geometryContentRevision; null while pending.
    Core::BufferHandle localBoundsBuffer;
    // Optional meshletCount records with the same accepted bounds ABI; each record carries its own VALID proof.
    Core::BufferHandle meshletLocalBoundsBuffer;
    Core::ECS::EntityID entity = Core::ECS::s_InvalidEntityId;
    u32 meshletCount = 0u;
    // Logical corner count; backing buffer is word-padded, never use its byte size.
    u32 meshletPrimitiveIndexCount = 0u;
    b8 dynamicMeshletBoundsFresh = false;
    b8 dynamicMeshletConesFresh = false;
    u64 version = 0u;
    // Accepted object-space geometry generation; zero requires conservative updates, including pending deformation.
    u64 geometryContentRevision = 0u;
    Name meshKey = s_NameNone;
    Core::BufferHandle triangleIndexBuffer;   // RT-only; null when ray tracing is unsupported
    Core::BufferHandle attributeBuffer;       // RT-only flat per-triangle-corner trace attributes; null when ray tracing is unsupported
    Span<const u32> solidTriangleWords;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return
            entity.valid()
            && meshKey != s_NameNone
            && buffersValid()
            && meshletCount > 0u
            && meshletPrimitiveIndexCount > 0u
            && solidTriangleWords.size() == (static_cast<usize>(meshletPrimitiveIndexCount) / s_MeshletTriangleIndexCount + s_MeshletSolidTriangleWordRoundUp) / s_MeshletSolidTriangleWordBits
            && localBounds.valid()
        ;
    }
};

class IRuntimeMeshProvider{
public:
    virtual ~IRuntimeMeshProvider()noexcept = default;


public:
    [[nodiscard]] virtual Expected<RuntimeMeshDesc> resolveRuntimeMesh(Core::ECS::EntityID entity) = 0;
    // Attachment identity remains available when buffers or an evaluated pose are not ready yet.
    [[nodiscard]] virtual b8 hasRuntimeMeshBinding(Core::ECS::EntityID entity)const = 0;
    virtual void markLiveRuntimeMeshes(RuntimeMeshRequestSet& requests) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RenderableMeshDesc{
    Core::Assets::AssetRef<Mesh> mesh;
    RuntimeMeshDesc runtimeMesh;
    b8 runtime = false;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return runtime ? runtimeMesh.valid() : mesh.valid();
    }
};

class IRuntimeMeshRegistry{
public:
    virtual ~IRuntimeMeshRegistry()noexcept = default;


public:
    virtual void registerRuntimeMeshProvider(IRuntimeMeshProvider& provider) = 0;
    virtual void unregisterRuntimeMeshProvider(IRuntimeMeshProvider& provider) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
