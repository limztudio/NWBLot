// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/ref.h>
#include <core/ecs/entity_id.h>
#include <core/graphics/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Mesh;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct RuntimeMeshDesc{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    Name meshKey = NAME_NONE;
    Core::BufferHandle positionBuffer;
    Core::BufferHandle normalBuffer;
    Core::BufferHandle tangentBuffer;
    Core::BufferHandle uv0Buffer;
    Core::BufferHandle colorBuffer;
    Core::BufferHandle meshletDescBuffer;
    Core::BufferHandle meshletBoundsBuffer;
    Core::BufferHandle meshletPositionRefBuffer;
    Core::BufferHandle meshletAttributeRefBuffer;
    Core::BufferHandle meshletLocalVertexRefBuffer;
    Core::BufferHandle meshletPrimitiveIndexBuffer;
    u32 meshletCount = 0u;
    u64 version = 0u;

    [[nodiscard]] bool valid()const noexcept{
        return
            entity.valid()
            && meshKey != NAME_NONE
            && positionBuffer != nullptr
            && normalBuffer != nullptr
            && tangentBuffer != nullptr
            && uv0Buffer != nullptr
            && colorBuffer != nullptr
            && meshletDescBuffer != nullptr
            && meshletBoundsBuffer != nullptr
            && meshletPositionRefBuffer != nullptr
            && meshletAttributeRefBuffer != nullptr
            && meshletLocalVertexRefBuffer != nullptr
            && meshletPrimitiveIndexBuffer != nullptr
            && meshletCount > 0u
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

