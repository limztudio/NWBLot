// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"
#include "material_instance.h"
#include "renderer_state.h"

#include <core/assets/global.h>
#include <core/graphics/module.h>
#include <impl/assets/graphics/mesh/binding_slots.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_csg/frame_state.h>
#include <impl/ecs_csg/shape_registry.h>
#include <impl/ecs_scene/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem;
class RendererShaderSystem;
class RendererMeshSystem;
class RendererMaterialSystem;
class RendererCsgSystem;
class RendererDeferredSystem;
class RendererAvboitSystem;
class Shader;
class Mesh;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
#if defined(NWB_DEBUG)
    struct MaterialTypedInstanceRangeVector;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using RendererShaderPathResolveCallback = Function<bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RendererMaterialInstanceOverrideField{
    const MaterialTypedLayoutField* field = nullptr;
    u32 blockByteBegin = 0u;
    bool mutableBlock = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystemSubsystemBase : NoCopy{
protected:
    explicit RendererSystemSubsystemBase(RendererSystem& renderer);


protected:
    [[nodiscard]] RendererShaderSystem& shaderSystem()noexcept;
    [[nodiscard]] const RendererShaderSystem& shaderSystem()const noexcept;
    [[nodiscard]] RendererMeshSystem& meshSystem()noexcept;
    [[nodiscard]] const RendererMeshSystem& meshSystem()const noexcept;
    [[nodiscard]] RendererMaterialSystem& materialSystem()noexcept;
    [[nodiscard]] const RendererMaterialSystem& materialSystem()const noexcept;
    [[nodiscard]] RendererCsgSystem& csgSystem()noexcept;
    [[nodiscard]] const RendererCsgSystem& csgSystem()const noexcept;
    [[nodiscard]] RendererDeferredSystem& deferredSystem()noexcept;
    [[nodiscard]] const RendererDeferredSystem& deferredSystem()const noexcept;
    [[nodiscard]] RendererAvboitSystem& avboitSystem()noexcept;
    [[nodiscard]] const RendererAvboitSystem& avboitSystem()const noexcept;


protected:
    RendererSystem& m_renderer;
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    RendererShaderPathResolveCallback& m_shaderPathResolver;
    CsgShapeRegistry& m_csgShapeRegistry;
    RendererMeshState& m_meshState;
    RendererMaterialState& m_materialState;
    RendererDrawState& m_drawState;
    RendererCsgState& m_csgState;
    RendererDeferredState& m_deferredState;
    RendererAvboitState& m_avboitState;

protected:
    static constexpr u32 s_MeshInstanceBindingSlot = NWB_MESH_BINDING_INSTANCE;
    static constexpr u32 s_MeshViewBindingSlot = NWB_MESH_BINDING_VIEW;
    static constexpr u32 s_MeshGeneratedVertexBindingSlot = NWB_MESH_BINDING_GENERATED_VERTEX;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
