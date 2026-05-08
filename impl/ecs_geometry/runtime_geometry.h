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


class Material;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshSourceLayout{
    enum Enum : u32{
        GeometryVertex = 0u,
        DeformableVertex = 1u,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RuntimeGeometryDesc{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    Core::Assets::AssetRef<Material> material;
    Name geometryKey = NAME_NONE;
    Core::BufferHandle shaderVertexBuffer;
    Core::BufferHandle shaderIndexBuffer;
    u32 indexCount = 0u;
    u32 sourceVertexLayout = MeshSourceLayout::GeometryVertex;
    u64 version = 0u;
    bool visible = true;

    [[nodiscard]] bool valid()const noexcept{
        return
            visible
            && entity.valid()
            && material.valid()
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
    [[nodiscard]] virtual bool containsRuntimeGeometry(const Name& geometryKey, u64 version) = 0;
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

