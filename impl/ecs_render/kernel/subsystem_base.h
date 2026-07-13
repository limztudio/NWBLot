
#pragma once


#include <impl/ecs_render/kernel/components.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/shared/renderer_state.h>

#include <core/assets/global.h>
#include <core/graphics/module.h>
#include <impl/assets/graphics/mesh/binding_slots.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_csg/frame_state.h>
#include <impl/ecs_csg/shape_registry.h>


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


template<typename RendererT>
class RendererSystemSubsystemBase : NoCopy{
protected:
    explicit RendererSystemSubsystemBase(RendererT& renderer)
        : m_renderer(renderer)
    {}


protected:
    [[nodiscard]] Core::Alloc::GlobalArena& arena()const noexcept{ return m_renderer.arena(); }
    [[nodiscard]] Core::ECS::World& world()const noexcept{ return m_renderer.world(); }
    [[nodiscard]] Core::Graphics& graphics()const noexcept{ return m_renderer.graphics(); }
    [[nodiscard]] Core::Assets::AssetManager& assetManager()const noexcept{ return m_renderer.assetManager(); }
    [[nodiscard]] RendererShaderPathResolveCallback& shaderPathResolver()const noexcept{ return m_renderer.shaderPathResolver(); }
    [[nodiscard]] CsgShapeRegistry& csgShapeRegistry()const noexcept{ return m_renderer.csgShapeRegistry(); }
    [[nodiscard]] RendererMeshState& meshState()const noexcept{ return m_renderer.meshState(); }
    [[nodiscard]] RendererMaterialState& materialState()const noexcept{ return m_renderer.materialState(); }
    [[nodiscard]] RendererDrawState& drawState()const noexcept{ return m_renderer.drawState(); }
    [[nodiscard]] RendererCsgState& csgState()const noexcept{ return m_renderer.csgState(); }
    [[nodiscard]] RendererDeferredState& deferredState()const noexcept{ return m_renderer.deferredState(); }
    [[nodiscard]] RendererAvboitState& avboitState()const noexcept{ return m_renderer.avboitState(); }
    [[nodiscard]] RendererRayTracingState& rayTracingState()const noexcept{ return m_renderer.rayTracingState(); }


protected:
    RendererT& m_renderer;

protected:
    static constexpr u32 s_MeshInstanceBindingSlot = NWB_MESH_BINDING_INSTANCE;
    static constexpr u32 s_MeshViewBindingSlot = NWB_MESH_BINDING_VIEW;
    static constexpr u32 s_MeshGeneratedVertexBindingSlot = NWB_MESH_BINDING_GENERATED_VERTEX;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

