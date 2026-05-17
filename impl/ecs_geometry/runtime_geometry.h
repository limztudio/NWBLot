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


namespace MeshSourceLayout{
    enum Enum : u32{
        GeometryVertex = 0u,
        SkinnedGeometryVertex = 1u,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RuntimeGeometryDesc{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    Name geometryKey = NAME_NONE;
    Core::BufferHandle shaderVertexBuffer;
    Core::BufferHandle shaderIndexBuffer;
    u32 indexCount = 0u;
    u32 sourceVertexLayout = MeshSourceLayout::GeometryVertex;
    u64 version = 0u;

    [[nodiscard]] bool valid()const noexcept{
        return
            entity.valid()
            && geometryKey != NAME_NONE
            && shaderVertexBuffer != nullptr
            && shaderIndexBuffer != nullptr
            && indexCount > 0u
            && (indexCount % 3u) == 0u
        ;
    }
};
using RuntimeGeometryVisitor = Function<void(const RuntimeGeometryDesc&)>;

class IRuntimeGeometryProvider{
public:
    virtual ~IRuntimeGeometryProvider() = default;

public:
    [[nodiscard]] virtual usize runtimeGeometryCandidateCount() = 0;
    virtual void forEachRuntimeGeometry(const RuntimeGeometryVisitor& visitor) = 0;
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

