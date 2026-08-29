// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/components.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/shader/shader_path_resolver.h>
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


class RendererFramePipeline;
class Shader;
class Mesh;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
    struct MaterialTypedInstanceRangeVector;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RendererMaterialInstanceOverrideField{
    const MaterialTypedLayoutField* field = nullptr;
    u32 blockByteBegin = 0u;
    bool mutableBlock = false;
};

template<typename RendererT>
class RendererFramePipelineSubsystemBase : NoCopy{
protected:
    explicit RendererFramePipelineSubsystemBase(RendererT& renderer)
        : m_renderer(renderer)
    {}


protected:
    [[nodiscard]] Core::Alloc::GlobalArena& arena()const noexcept{ return m_renderer.arena(); }
    [[nodiscard]] Core::ECS::World& world()const noexcept{ return m_renderer.world(); }
    [[nodiscard]] Core::Graphics& graphics()const noexcept{ return m_renderer.graphics(); }
    [[nodiscard]] Core::Assets::AssetManager& assetManager()const noexcept{ return m_renderer.assetManager(); }
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

