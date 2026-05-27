// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset_ref.h>
#include <core/ecs/entity_id.h>
#include <core/graphics/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Geometry;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct RuntimeGeometryDesc{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    Name geometryKey = NAME_NONE;
    Core::BufferHandle positionBuffer;
    Core::BufferHandle normalBuffer;
    Core::BufferHandle tangentBuffer;
    Core::BufferHandle uv0Buffer;
    Core::BufferHandle colorBuffer;
    Core::BufferHandle vertexRefBuffer;
    Core::BufferHandle meshletDescBuffer;
    Core::BufferHandle meshletBoundsBuffer;
    Core::BufferHandle meshletVertexRefBuffer;
    Core::BufferHandle meshletPrimitiveIndexBuffer;
    u32 meshletCount = 0u;
    u64 version = 0u;

    [[nodiscard]] bool valid()const noexcept{
        return
            entity.valid()
            && geometryKey != NAME_NONE
            && positionBuffer != nullptr
            && normalBuffer != nullptr
            && tangentBuffer != nullptr
            && uv0Buffer != nullptr
            && colorBuffer != nullptr
            && vertexRefBuffer != nullptr
            && meshletDescBuffer != nullptr
            && meshletBoundsBuffer != nullptr
            && meshletVertexRefBuffer != nullptr
            && meshletPrimitiveIndexBuffer != nullptr
            && meshletCount > 0u
        ;
    }
};

class IRuntimeGeometryProvider{
public:
    virtual ~IRuntimeGeometryProvider() = default;

public:
    [[nodiscard]] virtual bool resolveRuntimeGeometry(Core::ECS::EntityID entity, RuntimeGeometryDesc& outGeometry) = 0;
    [[nodiscard]] virtual bool containsRuntimeGeometry(const Name& geometryKey, u64 version) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RenderableGeometryDesc{
    Core::Assets::AssetRef<Geometry> geometry;
    RuntimeGeometryDesc runtimeGeometry;
    bool runtime = false;

    [[nodiscard]] bool valid()const noexcept{
        return runtime
            ? runtimeGeometry.valid()
            : geometry.valid()
        ;
    }
};

class IRuntimeGeometryRegistry{
public:
    virtual ~IRuntimeGeometryRegistry() = default;

public:
    virtual void registerRuntimeGeometryProvider(IRuntimeGeometryProvider& provider) = 0;
    virtual void unregisterRuntimeGeometryProvider(IRuntimeGeometryProvider& provider) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

